#include "services/power_monitor.h"
#include "drivers/ina226/ina226.h"
#include "hal/hal_config.h"
#include "hal/hal_i2c.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

static const char *TAG = "power_monitor";

#define SHUNT_OHM     0.01f /* 10 mOhm */
#define MAX_CURRENT_A 2.0f

#define SLOW_PERIOD_MS 100
#define FAST_PERIOD_MS 20
#define QUEUE_LEN      128 /* 2.5 s of fast samples */
#define TASK_STACK     4096
#define TASK_PRIORITY  5

static ina226_handle_t s_sensor;
static SemaphoreHandle_t s_lock; /* guards the queue and s_latest */
static TaskHandle_t s_task;
static volatile bool s_fast;

static power_sample_t s_queue[QUEUE_LEN];
static size_t s_head;  /* oldest */
static size_t s_count;
static power_sample_t s_latest;
static bool s_have_latest;

/* Slow: 16 averaged 1.1 ms conversions (~35 ms), steady for the wattage label.
 * Fast: single 140 us conversions, so every 20 ms sample is independent. */
static void apply_rate(bool fast)
{
    if (fast) {
        ina226_configure(s_sensor, INA226_AVG_1, INA226_CT_140US, INA226_CT_140US, INA226_MODE_SHUNT_AND_BUS_CONTINUOUS);
    } else {
        ina226_configure(s_sensor, INA226_AVG_16, INA226_CT_1100US, INA226_CT_1100US, INA226_MODE_SHUNT_AND_BUS_CONTINUOUS);
    }
}

static void sample_task(void *arg)
{
    (void)arg;
    bool applied_fast = false;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        bool fast = s_fast;
        if (fast != applied_fast) {
            apply_rate(fast);
            applied_fast = fast;
            last_wake = xTaskGetTickCount();
        }

        power_sample_t sample;
        sample.volts = ina226_read_bus_voltage_v(s_sensor);
        sample.amps = ina226_read_current_a(s_sensor);
        sample.watts = sample.volts * sample.amps;

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_latest = sample;
        s_have_latest = true;
        if (fast) {
            if (s_count == QUEUE_LEN) { /* full: drop the oldest */
                s_head = (s_head + 1) % QUEUE_LEN;
                s_count--;
            }
            s_queue[(s_head + s_count) % QUEUE_LEN] = sample;
            s_count++;
        }
        xSemaphoreGive(s_lock);

        if (fast) {
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FAST_PERIOD_MS));
        } else {
            /* Cut short by power_monitor_set_fast() so entering the scope
             * doesn't wait out the slow period. */
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SLOW_PERIOD_MS));
        }
    }
}

esp_err_t power_monitor_start(void)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_lock = xSemaphoreCreateMutex();
    s_sensor = ina226_init(hal_i2c_power_bus(), HAL_INA226_I2C_ADDRESS);
    if (s_lock == NULL || s_sensor == NULL) {
        return ESP_ERR_NO_MEM;
    }
    ina226_calibrate(s_sensor, SHUNT_OHM, MAX_CURRENT_A);
    apply_rate(false);
    if (xTaskCreate(sample_task, "power_mon", TASK_STACK, NULL, TASK_PRIORITY, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void power_monitor_set_fast(bool fast)
{
    if (s_task == NULL || s_fast == fast) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_head = 0;
    s_count = 0; /* a fresh run starts with an empty queue */
    xSemaphoreGive(s_lock);
    s_fast = fast;
    xTaskNotifyGive(s_task);
    ESP_LOGI(TAG, "%s sampling", fast ? "fast (20 ms)" : "default (1 s)");
}

bool power_monitor_get_latest(power_sample_t *out)
{
    if (s_task == NULL) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = s_have_latest;
    if (ok) {
        *out = s_latest;
    }
    xSemaphoreGive(s_lock);
    return ok;
}

size_t power_monitor_drain(power_sample_t *out, size_t max)
{
    if (s_task == NULL) {
        return 0;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = s_count < max ? s_count : max;
    for (size_t i = 0; i < n; i++) {
        out[i] = s_queue[(s_head + i) % QUEUE_LEN];
    }
    s_head = (s_head + n) % QUEUE_LEN;
    s_count -= n;
    xSemaphoreGive(s_lock);
    return n;
}
