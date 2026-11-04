#ifndef HTTPS_REQUEST_H
#define HTTPS_REQUEST_H

#include <stddef.h>
#include <esp_err.h>

typedef struct {
    int status;   /* HTTP status code, -1 if the request never got a response */
    char *body;   /* NUL-terminated, heap-allocated (PSRAM when available); NULL if empty */
    size_t len;   /* body length without the terminator */
} https_response_t;

/* Blocking HTTPS GET. If header_name and header_value are non-empty, that one
 * extra request header is sent (e.g. "Authorization"/"Bearer ..."). A
 * gzip-compressed body (some APIs compress unconditionally) is inflated
 * transparently. The server is verified against the ESP-IDF certificate
 * bundle (needs a valid system clock). Returns ESP_OK if a response was
 * received (check resp->status yourself). Bodies over 128 KiB are rejected
 * with ESP_ERR_NO_MEM. Call https_response_free() on resp afterwards. */
esp_err_t https_get(const char *url, int timeout_ms, const char *header_name, const char *header_value,
                    https_response_t *resp);

void https_response_free(https_response_t *resp);

#endif // HTTPS_REQUEST_H
