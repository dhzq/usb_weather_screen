#include "services/usb_hub_service.h"
#include "hal/hal_config.h"
#include "hal/hal_i2c.h"

#include <string.h>
#include <esp_io_expander.h>
#include <esp_io_expander_tca95xx_16bit.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

static const char *TAG = "usb_hub";

#define POLL_PERIOD_MS         200
#define USB_HUB_RESET_PULSE_MS 100
#define TASK_STACK             3072
#define TASK_PRIORITY          4

#define PIN_MASK(n) ((uint32_t)(1u << (n)))

static const int8_t s_power_pins[USB_HUB_PORT_COUNT] = HAL_HUB_PORT_POWER_PINS;
static const int8_t s_normal_pins[USB_HUB_PORT_COUNT] = HAL_HUB_PORT_NORMAL_PINS;
static const int8_t s_fault_pins[USB_HUB_PORT_COUNT] = HAL_HUB_PORT_FAULT_PINS;

static esp_io_expander_handle_t s_io;
static SemaphoreHandle_t s_lock; /* serializes expander access, guards s_state/s_cut */
static usb_hub_listener_t s_listener;
static void *s_listener_ctx;
static usb_hub_state_t s_state;
static bool s_state_valid;
static bool s_cut[USB_HUB_PORT_COUNT]; /* ports whose power the MCU is cutting */

static bool asserted(uint32_t levels, int pin)
{
    if (pin < 0) {
        return false;
    }
    bool high = (levels & PIN_MASK(pin)) != 0;
    return HAL_HUB_STATUS_ACTIVE_HIGH ? high : !high;
}

/* Builds the state from a raw input read. Caller holds s_lock. */
static usb_hub_state_t decode(uint32_t levels)
{
    usb_hub_state_t st = { 0 };
    for (int i = 0; i < USB_HUB_PORT_COUNT; i++) {
        st.power_on[i] = !s_cut[i];
        st.normal[i] = asserted(levels, s_normal_pins[i]);
        st.fault[i] = asserted(levels, s_fault_pins[i]);
    }
    st.sleeping = (levels & PIN_MASK(HAL_HUB_PIN_SLEEP)) != 0;
    st.high_speed = (levels & PIN_MASK(HAL_HUB_PIN_SPEED)) != 0;
    return st;
}

/* Re-reads the pins and, if anything changed, stores and reports the new state. */
static esp_err_t refresh(void)
{
    uint32_t levels = 0;
    usb_hub_state_t st = { 0 };
    bool changed = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = esp_io_expander_get_level(s_io, 0xFFFF, &levels);
    if (err == ESP_OK) {
        st = decode(levels);
        changed = !s_state_valid || memcmp(&st, &s_state, sizeof(st)) != 0;
        if (changed) {
            s_state = st;
            s_state_valid = true;
        }
    }
    xSemaphoreGive(s_lock);

    if (err != ESP_OK) {
        hal_i2c_bus_recover(hal_i2c_io_bus());
    } else if (changed) {
        ESP_LOGI(TAG, "pins=0x%04x speed=%s%s | USB1 %s/%s USB2 %s/%s USB3 %s/%s USB4 %s/%s",
                 (unsigned)levels, st.high_speed ? "high" : "full", st.sleeping ? " sleeping" : "",
                 st.power_on[0] ? "on" : "cut", st.fault[0] ? "FAULT" : st.normal[0] ? "ok" : "-",
                 st.power_on[1] ? "on" : "cut", st.fault[1] ? "FAULT" : st.normal[1] ? "ok" : "-",
                 st.power_on[2] ? "on" : "cut", st.fault[2] ? "FAULT" : st.normal[2] ? "ok" : "-",
                 st.power_on[3] ? "on" : "cut", st.normal[3] ? "ok" : "-");
        if (s_listener != NULL) {
            s_listener(&st, s_listener_ctx);
        }
    }
    return err;
}

static void poll_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(POLL_PERIOD_MS));
        refresh();
    }
}

esp_err_t usb_hub_service_start(usb_hub_listener_t listener, void *ctx)
{
    if (s_io != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    /* The driver resets the chip on creation: every pin becomes an input, which
     * is exactly the default we want (all ports powered, hub chip enabled via
     * its external pull-down, status pins readable) and also restores power if
     * a previous run left a port cut. */
    esp_err_t err = esp_io_expander_new_i2c_tca95xx_16bit(hal_i2c_io_bus(), HAL_TCA9555_I2C_ADDRESS, &s_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9555 not responding: %s", esp_err_to_name(err));
        s_io = NULL;
        return ESP_ERR_NOT_FOUND;
    }

    /* USB-UART I/O level select is the only output, 3.3 V like demo/usb_hub.
     * The latch resets to high, so the pin goes straight to 3.3 V. */
    err = esp_io_expander_set_dir(s_io, PIN_MASK(HAL_HUB_PIN_UART_LEVEL), IO_EXPANDER_OUTPUT);
    if (err == ESP_OK) {
        err = esp_io_expander_set_level(s_io, PIN_MASK(HAL_HUB_PIN_UART_LEVEL), 1);
    }
    if (err != ESP_OK) {
        return err;
    }

    s_listener = listener;
    s_listener_ctx = ctx;
    err = refresh(); /* reports the initial state */
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreate(poll_task, "usb_hub", TASK_STACK, NULL, TASK_PRIORITY, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void usb_hub_get_state(usb_hub_state_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_lock);
}

esp_err_t usb_hub_set_port_power(uint8_t port, bool on)
{
    if (s_io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (port >= USB_HUB_PORT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t mask = PIN_MASK(s_power_pins[port]);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err;
    if (on) {
        err = esp_io_expander_set_dir(s_io, mask, IO_EXPANDER_INPUT); /* back to hi-Z */
    } else {
        /* The library only allows set_level on an output, so go through the
         * latch's reset value (high, same as the idle pulled-up input) and then
         * drive low. */
        err = esp_io_expander_set_dir(s_io, mask, IO_EXPANDER_OUTPUT);
        if (err == ESP_OK) {
            if (port == 3) err = esp_io_expander_set_level(s_io, mask, 0);
            else err = esp_io_expander_set_level(s_io, mask, 1);
        }
    }
    if (err == ESP_OK) {
        s_cut[port] = !on;
    }
    xSemaphoreGive(s_lock);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "USB%d power %s", port + 1, on ? "restored" : "cut");
        refresh();
    }
    return err;
}

esp_err_t usb_hub_set_uart_3v3(bool is_3v3)
{
    if (s_io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = esp_io_expander_set_level(s_io, PIN_MASK(HAL_HUB_PIN_UART_LEVEL), is_3v3);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t usb_hub_reset_chip(void)
{
    if (s_io == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t mask = PIN_MASK(HAL_HUB_PIN_CHIP_DISABLE);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    /* The latch is high from reset, so making the pin an output disables the chip. */
    esp_err_t err = esp_io_expander_set_dir(s_io, mask, IO_EXPANDER_OUTPUT);
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(USB_HUB_RESET_PULSE_MS));

    /* Release back to input so the external pull-down re-enables the chip. */
    xSemaphoreTake(s_lock, portMAX_DELAY);
    err = esp_io_expander_set_dir(s_io, mask, IO_EXPANDER_INPUT);
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "hub chip reset pulsed: %s", esp_err_to_name(err));
    return err;
}
