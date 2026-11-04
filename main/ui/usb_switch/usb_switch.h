#ifndef LVGL_USB_SWITCH_H
#define LVGL_USB_SWITCH_H

#include <lvgl.h>
#include <stdint.h>
#include <stdbool.h>

/*builds this page's UI as an independent screen (not auto-loaded); colors come
 *from the shared theme (theme/ui_theme.h), which must already be initialized*/
void usb_switch_ui_init(void);

/*the screen object this page lives on, for the caller to switch to (e.g. via
 *lv_screen_load_anim())*/
lv_obj_t * usb_switch_get_screen(void);

/*when set, tapping a port card does not toggle the card itself; it calls this
 *with the requested state instead, and the card only changes once the real
 *hardware state is reported back through usb_switch_set_port(). Unset (NULL)
 *keeps the self-toggling simulator behavior. Runs on the LVGL task.*/
typedef void (*usb_switch_port_request_cb_t)(uint8_t index, bool on);
void usb_switch_set_port_request_cb(usb_switch_port_request_cb_t cb);

/*programmatically turn a port (0~3) on/off and refresh its visual state*/
void usb_switch_set_port(uint8_t index, bool on);
bool usb_switch_get_port(uint8_t index);

/*global speed mode for the whole hub (not per-port) - simulated, has no
 *effect on port behavior, just drives the on-screen indicator*/
typedef enum {
    USB_SPEED_MODE_FULL,   /*Full Speed - simulated USB 1.1 class*/
    USB_SPEED_MODE_HIGH,   /*High Speed - simulated USB 2.0 class*/
} usb_speed_mode_t;

void usb_switch_set_speed_mode(usb_speed_mode_t mode);
usb_speed_mode_t usb_switch_get_speed_mode(void);

/*port status, independent of the on/off power switch - reflects whether the
 *port's electrical/signal state itself is healthy. "Normal" covers both "a
 *device is plugged in and enumerating fine" and "no device present / the bus
 *is suspended" - neither is a fault. "Fault" covers conditions like
 *over-current.*/
typedef enum {
    USB_PORT_STATUS_NORMAL,
    USB_PORT_STATUS_FAULT,
} usb_port_status_t;

void usb_switch_set_port_status(uint8_t index, usb_port_status_t status);
usb_port_status_t usb_switch_get_port_status(uint8_t index);

/*one-shot snapshot of the power-monitored port's simulated electrical
 *reading, for pages (like the oscilloscope view) that visualize it*/
typedef struct {
    bool on;
    usb_port_status_t status;
    float volts;
    float amps;
} usb_power_reading_t;

usb_power_reading_t usb_switch_get_power_reading(void);

/*requests the power-monitored port on/off (through the port request callback
 *when set, so the real hardware switches; otherwise toggles the simulated
 *state directly) - lets a page like the oscilloscope view offer a quick toggle without
 *needing to know the literal port index behind usb_switch_get_power_reading()*/
void usb_switch_set_power_monitor_port(bool on);

/*the clickable lightning-bolt badge on the power-monitored port, exposed so
 *the caller can wire a tap-to-drill-in navigation (e.g. to an oscilloscope
 *page) without this module needing to know where that navigation goes*/
lv_obj_t * usb_switch_get_power_badge(void);

#endif //LVGL_USB_SWITCH_H
