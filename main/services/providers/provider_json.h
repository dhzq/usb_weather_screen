#ifndef PROVIDER_JSON_H
#define PROVIDER_JSON_H

/* Small cJSON helpers shared by the weather providers. */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>
#include <esp_heap_caps.h>

static inline const char *json_str(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

/* Numeric field, tolerating the API sending numbers as strings. */
static inline double json_num(const cJSON *obj, const char *key, double def)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsNumber(item)) {
        return item->valuedouble;
    }
    if (cJSON_IsString(item) && item->valuestring[0] != '\0') {
        char *end;
        double v = strtod(item->valuestring, &end);
        if (*end == '\0') {
            return v;
        }
    }
    return def;
}

static inline int16_t round_i16(double v)
{
    return (int16_t)lroundf((float)v);
}

/* cJSON allocates one node per value (~1000 here); keep that out of
 * internal RAM. */
static inline void *psram_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != NULL ? p : malloc(size);
}

/* cJSON allocates one node per value; while on, keep that out of internal RAM.
 * Only the weather task uses cJSON, so switching the global hooks is safe. */
static inline void provider_json_use_psram(bool on)
{
    if (on) {
        cJSON_Hooks hooks = { .malloc_fn = psram_malloc, .free_fn = free };
        cJSON_InitHooks(&hooks);
    } else {
        cJSON_InitHooks(NULL);
    }
}

#endif // PROVIDER_JSON_H
