#include "wifi_provision.h"
#include "settings_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "wifi_prov";

#define MAX_SCAN_RESULTS 20
#define FORM_BODY_MAX 512 /* a 32-char CJK SSID url-encodes to ~290 bytes */
#define SCAN_ENTRY_MAX 260 /* worst case: 32 control chars escaped as \uXXXX + keys */

static const char PAGE_HTML[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>WiFi 配网</title><style>"
    "body{font-family:sans-serif;background:#f2f4f7;margin:0;padding:20px}"
    ".c{max-width:420px;margin:0 auto;background:#fff;border-radius:12px;padding:20px;box-shadow:0 2px 8px #0002}"
    "h2{margin-top:0}label{display:block;margin:14px 0 4px;color:#555;font-size:14px}"
    "input,select,button{width:100%;box-sizing:border-box;padding:10px;font-size:16px;border:1px solid #ccc;border-radius:8px}"
    "button{background:#1677ff;color:#fff;border:0;margin-top:20px}button:disabled{background:#999}"
    "#m{margin-top:14px;color:#555;font-size:14px}"
    "</style></head><body><div class=\"c\"><h2>WiFi 配网</h2>"
    "<label>选择网络</label><select id=\"l\"><option value=\"\">扫描中...</option></select>"
    "<label>或手动输入名称</label><input id=\"s\" maxlength=\"32\" placeholder=\"WiFi 名称\">"
    "<label>密码（开放网络留空）</label><input id=\"p\" type=\"password\" maxlength=\"63\">"
    "<button id=\"b\">保存并连接</button><div id=\"m\"></div></div><script>"
    "var l=document.getElementById('l'),s=document.getElementById('s'),p=document.getElementById('p'),"
    "b=document.getElementById('b'),m=document.getElementById('m');"
    "l.onchange=function(){if(l.value)s.value=l.value};"
    "function scan(){fetch('/scan').then(function(r){return r.json()}).then(function(a){"
    "l.innerHTML='<option value=\"\">请选择 ('+a.length+')</option>';"
    "a.forEach(function(n){var o=document.createElement('option');o.value=n.ssid;"
    "o.textContent=n.ssid+'  '+n.rssi+'dBm'+(n.auth?' 🔒':'');l.appendChild(o)})"
    "}).catch(function(){l.innerHTML='<option value=\"\">扫描失败，请手动输入</option>'})}"
    "b.onclick=function(){if(!s.value){m.textContent='请填写 WiFi 名称';return}"
    "b.disabled=true;m.textContent='保存中...';"
    "fetch('/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
    "body:'ssid='+encodeURIComponent(s.value)+'&password='+encodeURIComponent(p.value)})"
    ".then(function(r){if(!r.ok)throw 0;m.textContent='已保存，设备正在重启并连接网络，可关闭此页面。'})"
    ".catch(function(){m.textContent='保存失败，请重试';b.disabled=false})};"
    "scan();</script></body></html>";

static void url_decode(char *dst, size_t dst_len, const char *src, size_t src_len)
{
    size_t o = 0;
    for (size_t i = 0; i < src_len && o + 1 < dst_len; i++) {
        char c = src[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%' && i + 2 < src_len) {
            char hex[3] = { src[i + 1], src[i + 2], 0 };
            char *end;
            long v = strtol(hex, &end, 16);
            if (*end == 0) {
                c = (char)v;
                i += 2;
            }
        }
        dst[o++] = c;
    }
    dst[o] = 0;
}

/* Extracts `key` from an urlencoded body into `out`. Returns false if absent. */
static bool form_get(const char *body, const char *key, char *out, size_t out_len)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        const char *amp = strchr(p, '&');
        size_t seg = amp ? (size_t)(amp - p) : strlen(p);
        if (seg > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            url_decode(out, out_len, p + klen + 1, seg - klen - 1);
            return true;
        }
        p = amp ? amp + 1 : NULL;
    }
    return false;
}

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PAGE_HTML, HTTPD_RESP_USE_STRLEN);
}

/* Appends `s` as a JSON string body (without quotes) into buf at *pos. */
static void json_escape(char *buf, size_t cap, size_t *pos, const char *s)
{
    for (; *s && *pos + 8 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            buf[(*pos)++] = '\\';
            buf[(*pos)++] = c;
        } else if (c < 0x20) {
            *pos += snprintf(buf + *pos, cap - *pos, "\\u%04x", c);
        } else {
            buf[(*pos)++] = c;
        }
    }
}

static esp_err_t scan_handler(httpd_req_t *req)
{
    wifi_scan_config_t scan_cfg = { 0 };
    uint16_t count = MAX_SCAN_RESULTS;
    wifi_ap_record_t *aps = calloc(MAX_SCAN_RESULTS, sizeof(*aps));
    char *json = malloc(MAX_SCAN_RESULTS * SCAN_ENTRY_MAX + 8);
    esp_err_t ret = ESP_FAIL;
    if (aps == NULL || json == NULL) {
        free(aps);
        free(json);
        return httpd_resp_send_500(req);
    }
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK ||
        esp_wifi_scan_get_ap_records(&count, aps) != ESP_OK) {
        count = 0;
    }

    size_t cap = MAX_SCAN_RESULTS * SCAN_ENTRY_MAX + 8;
    size_t pos = 0;
    json[pos++] = '[';
    bool first = true;
    for (uint16_t i = 0; i < count; i++) {
        const char *ssid = (const char *)aps[i].ssid;
        if (ssid[0] == 0) {
            continue; /* hidden network */
        }
        if (!first) json[pos++] = ',';
        first = false;
        pos += snprintf(json + pos, cap - pos, "{\"ssid\":\"");
        json_escape(json, cap, &pos, ssid);
        pos += snprintf(json + pos, cap - pos, "\",\"rssi\":%d,\"auth\":%d}", aps[i].rssi,
                        aps[i].authmode != WIFI_AUTH_OPEN);
    }
    json[pos++] = ']';
    json[pos] = 0;

    httpd_resp_set_type(req, "application/json");
    ret = httpd_resp_send(req, json, pos);
    free(aps);
    free(json);
    return ret;
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(1500)); /* let the HTTP response reach the phone */
    esp_restart();
}

static esp_err_t save_handler(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len >= FORM_BODY_MAX) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad body");
    }
    char body[FORM_BODY_MAX];
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n <= 0) {
            return ESP_FAIL;
        }
        got += n;
    }
    body[got] = 0;

    char ssid[33] = { 0 };
    char password[64] = { 0 };
    if (!form_get(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid required");
    }
    form_get(body, "password", password, sizeof(password));

    if (settings_store_set_wifi(ssid, password) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }
    settings_store_set_config_mode(false);
    ESP_LOGI(TAG, "credentials saved for '%s', rebooting", ssid);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
    xTaskCreate(restart_task, "prov_restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

esp_err_t wifi_provision_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    httpd_handle_t server;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = index_handler },
        { .uri = "/scan", .method = HTTP_GET, .handler = scan_handler },
        { .uri = "/save", .method = HTTP_POST, .handler = save_handler },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    return ESP_OK;
}
