#include "https_request.h"

#include <stdio.h>
#include <string.h>

#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <miniz.h>

static const char *TAG = "https";

#define BODY_INITIAL_CAP (32 * 1024)
#define BODY_MAX_CAP     (128 * 1024)

static void *body_realloc(void *old, size_t size)
{
    void *p = heap_caps_realloc(old, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != NULL ? p : heap_caps_realloc(old, size, MALLOC_CAP_8BIT);
}

/* Inflates a gzip member into a new NUL-terminated buffer. The ROM miniz
 * handles the raw deflate stream; the gzip header/trailer are parsed here
 * (the last 4 bytes hold the uncompressed size). */
static char *gunzip(const char *src, size_t src_len, size_t *out_len)
{
    const uint8_t *p = (const uint8_t *)src;
    if (src_len < 18 || p[2] != 8) {
        ESP_LOGE(TAG, "unsupported gzip stream");
        return NULL;
    }
    uint8_t flags = p[3];
    size_t pos = 10;
    if (flags & 0x04) { /* FEXTRA */
        pos += 2 + (size_t)(p[pos] | (p[pos + 1] << 8));
    }
    if (flags & 0x08) { /* FNAME */
        while (pos < src_len && p[pos++] != 0) {}
    }
    if (flags & 0x10) { /* FCOMMENT */
        while (pos < src_len && p[pos++] != 0) {}
    }
    if (flags & 0x02) { /* FHCRC */
        pos += 2;
    }
    if (pos + 8 >= src_len) {
        ESP_LOGE(TAG, "truncated gzip stream");
        return NULL;
    }

    size_t isize = (size_t)p[src_len - 4] | ((size_t)p[src_len - 3] << 8) |
                   ((size_t)p[src_len - 2] << 16) | ((size_t)p[src_len - 1] << 24);
    if (isize == 0 || isize > BODY_MAX_CAP) {
        ESP_LOGE(TAG, "bad gzip size %u", (unsigned)isize);
        return NULL;
    }
    char *out = body_realloc(NULL, isize + 1);
    if (out == NULL) {
        return NULL;
    }
    /* The decompressor state is ~10 KB; tinfl_decompress_mem_to_mem() would put
     * it on the caller's stack (and overflow a 12 KB task stack), so keep it
     * on the heap and call the low-level entry point directly. */
    tinfl_decompressor *state = body_realloc(NULL, sizeof(*state));
    if (state == NULL) {
        heap_caps_free(out);
        return NULL;
    }
    tinfl_init(state);
    size_t in_size = src_len - pos - 8;
    size_t out_size = isize;
    tinfl_status st = tinfl_decompress(state, p + pos, &in_size, (uint8_t *)out, (uint8_t *)out, &out_size,
                                       TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    heap_caps_free(state);
    if (st != TINFL_STATUS_DONE || out_size != isize) {
        ESP_LOGE(TAG, "gzip inflate failed (status %d)", (int)st);
        heap_caps_free(out);
        return NULL;
    }
    out[out_size] = 0;
    *out_len = out_size;
    return out;
}

esp_err_t https_get(const char *url, int timeout_ms, const char *header_name, const char *header_value,
                    https_response_t *resp)
{
    if (url == NULL || resp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(resp, 0, sizeof(*resp));
    resp->status = -1;

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = timeout_ms,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "usb_weather_screen/1.0",
        .keep_alive_enable = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Accept", "application/json");
    if (header_name != NULL && header_name[0] != 0 && header_value != NULL && header_value[0] != 0) {
        esp_http_client_set_header(client, header_name, header_value);
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
        goto done;
    }
    esp_http_client_fetch_headers(client);
    resp->status = esp_http_client_get_status_code(client);

    size_t cap = BODY_INITIAL_CAP;
    char *buf = body_realloc(NULL, cap);
    if (buf == NULL) {
        err = ESP_ERR_NO_MEM;
        goto done;
    }

    size_t len = 0;
    while (1) {
        if (len + 1 >= cap) {
            if (cap >= BODY_MAX_CAP) {
                ESP_LOGE(TAG, "response larger than %d bytes", BODY_MAX_CAP);
                heap_caps_free(buf);
                err = ESP_ERR_NO_MEM;
                goto done;
            }
            char *grown = body_realloc(buf, cap * 2);
            if (grown == NULL) {
                heap_caps_free(buf);
                err = ESP_ERR_NO_MEM;
                goto done;
            }
            buf = grown;
            cap *= 2;
        }
        int n = esp_http_client_read(client, buf + len, (int)(cap - len - 1));
        if (n < 0) {
            ESP_LOGE(TAG, "read failed");
            heap_caps_free(buf);
            err = ESP_FAIL;
            goto done;
        }
        if (n == 0) {
            break;
        }
        len += (size_t)n;
    }
    buf[len] = 0;

    if (len >= 2 && (uint8_t)buf[0] == 0x1f && (uint8_t)buf[1] == 0x8b) {
        char *plain = gunzip(buf, len, &len);
        heap_caps_free(buf);
        if (plain == NULL) {
            err = ESP_FAIL;
            goto done;
        }
        buf = plain;
    }
    resp->body = buf;
    resp->len = len;

done:
    esp_http_client_cleanup(client);
    return err;
}

void https_response_free(https_response_t *resp)
{
    if (resp != NULL) {
        heap_caps_free(resp->body);
        resp->body = NULL;
        resp->len = 0;
    }
}
