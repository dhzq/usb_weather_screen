#ifndef SERVICES_USB_HUB_SERVICE_H
#define SERVICES_USB_HUB_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

#define USB_HUB_PORT_COUNT 4

/* Everything the TCA9555 tells us about the hub. Ports are 0-based (USB1 = 0). */
typedef struct {
    bool power_on[USB_HUB_PORT_COUNT]; /* false while the MCU is cutting the port power */
    bool normal[USB_HUB_PORT_COUNT];   /* "normal" indicator asserted */
    bool fault[USB_HUB_PORT_COUNT];    /* "fault" indicator asserted; never set for USB4 (no pin) */
    bool sleeping;                     /* hub chip in sleep state */
    bool high_speed;                   /* uplink negotiated high speed (else full speed) */
} usb_hub_state_t;

/* Called from the service polling task (and from the caller task after a
 * successful set_* call) whenever the state changed. Must not block. */
typedef void (*usb_hub_listener_t)(const usb_hub_state_t *state, void *ctx);

/* Brings up the TCA9555 on the I/O bus (hal_i2c_init() first), applies the
 * default pin setup (all ports powered, USB-UART at 3.3 V, hub enabled), reports
 * the initial state to `listener` and starts the polling task. */
esp_err_t usb_hub_service_start(usb_hub_listener_t listener, void *ctx);

/* Latest state. Safe from any task. */
void usb_hub_get_state(usb_hub_state_t *out);

/* Cuts (on=false) or restores a port power. */
esp_err_t usb_hub_set_port_power(uint8_t port, bool on);

/* USB-UART chip I/O level: true = 3.3 V, false = 5 V. */
esp_err_t usb_hub_set_uart_3v3(bool is_3v3);

/* Pulses the hub chip enable pin so it reloads its EEPROM configuration.
 * Blocks for the pulse (100 ms); the hub drops off the USB bus meanwhile. */
esp_err_t usb_hub_reset_chip(void);

#endif // SERVICES_USB_HUB_SERVICE_H
