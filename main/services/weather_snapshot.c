#include "weather_snapshot.h"

#include <stdlib.h>
#include <string.h>

weather_snapshot_t * weather_snapshot_alloc(void)
{
    return calloc(1, sizeof(weather_snapshot_t));
}

void weather_snapshot_free(weather_snapshot_t * snap)
{
    free(snap);
}

const char * weather_snapshot_set_text(char * dst, const char * text)
{
    size_t n = text ? strlen(text) : 0;
    if(n >= WEATHER_SNAPSHOT_TEXT_LEN) {
        n = WEATHER_SNAPSHOT_TEXT_LEN - 1;
        /*back up over UTF-8 continuation bytes so we never cut a character in half*/
        while(n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
    }
    if(n) memcpy(dst, text, n);
    dst[n] = '\0';
    return dst;
}
