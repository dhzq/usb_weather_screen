#include "quote_data.h"
#include <time.h>

/*mock data, keyed by weekday (tm_wday: 0=Sunday ... 6=Saturday). Swap for a
 *real source later behind the same interface.*/
static const char * const quotes[7] = {
    "休息也是一种生产力",
    "新的一周，新的开始",
    "坚持一下，就会看见不一样的风景",
    "慢慢来，比较快",
    "你比想象中更强大",
    "今天的努力，是明天的底气",
    "放慢脚步，感受生活",
};

const char * quote_data_get_today(void)
{
    time_t now = time(NULL);
    struct tm * t = localtime(&now);
    return quotes[t->tm_wday];
}
