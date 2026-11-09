#ifndef HAL_CONFIG_H
#define HAL_CONFIG_H

#include <driver/gpio.h>

/* INA226 power monitor (on I2C_NUM_1) */
#define HAL_INA226_I2C_ADDRESS     0x44
#define HAL_INA226_I2C_SDA_PIN     GPIO_NUM_1
#define HAL_INA226_I2C_SCL_PIN     GPIO_NUM_2

/* CH338X USB hub controller (on I2C_NUM_1) */
#define HAL_CH338X_I2C_ADDRESS     0x16

/* TCA9555 I/O expander (on I2C_NUM_0, shared with touch). Pin numbers are bit
 * indexes into the 16-bit pin mask: Pn.b -> n*8 + b (P04 = 4, P11 = 9). */
#define HAL_TCA9555_I2C_ADDRESS    0x20
#define HAL_TCA9555_INT_PIN        GPIO_NUM_16 /* not used, the hub service polls */

#define HAL_HUB_PIN_SLEEP          1  /* P01: high = hub asleep, low = normal */
#define HAL_HUB_PIN_UART_LEVEL     2  /* P02: USB-UART I/O level, high = 3.3 V, low = 5 V */
#define HAL_HUB_PIN_CHIP_DISABLE   3  /* P03: hub chip enable, active low; externally pulled
                                       * low (= enabled). Pulse high to reload its EEPROM config */
#define HAL_HUB_PIN_SPEED          8  /* P10: uplink speed, high = high speed, low = full speed */

/* USB1..USB4. Power control: left as input normally, driven low to cut the
 * port's power. Status indicators: -1 where the board has none. */
#define HAL_HUB_PORT_COUNT         4
#define HAL_HUB_PORT_POWER_PINS    { 4, 5, 6, 7 }
#define HAL_HUB_PORT_NORMAL_PINS   { 9, 11, 13, 15 }  /* P11, P13, P15, P17 */
#define HAL_HUB_PORT_FAULT_PINS    { 10, 12, 14, -1 } /* P12, P14, P16, (USB4: none) */
#define HAL_HUB_STATUS_ACTIVE_HIGH false /* status indicator pins read high when asserted */

/* Touch (FT6336U 0x38 or GT911 0x5D/0x14, selected by Kconfig TOUCH_CONTROLLER;
 * on I2C_NUM_0, shared with the TCA9555 I/O expander) */
#define HAL_TOUCH_I2C_SDA_PIN      GPIO_NUM_17
#define HAL_TOUCH_I2C_SCL_PIN      GPIO_NUM_18
#define HAL_TOUCH_INT_PIN          GPIO_NUM_8

/* ST7701 RGB panel timing + control pins (3-wire SPI multiplexed onto the
 * RGB timing pins themselves, see display_panel.c) */
#define HAL_LCD_VSYNC_PIN          GPIO_NUM_41
#define HAL_LCD_HSYNC_PIN          GPIO_NUM_38
#define HAL_LCD_DE_PIN             GPIO_NUM_40
#define HAL_LCD_PCLK_PIN           GPIO_NUM_39
#define HAL_LCD_DISP_PIN           GPIO_NUM_NC
#define HAL_LCD_RESET_PIN          GPIO_NUM_42

#define HAL_LCD_DATA0_PIN          GPIO_NUM_0
#define HAL_LCD_DATA1_PIN          GPIO_NUM_47
#define HAL_LCD_DATA2_PIN          GPIO_NUM_21
#define HAL_LCD_DATA3_PIN          GPIO_NUM_14
#define HAL_LCD_DATA4_PIN          GPIO_NUM_13
#define HAL_LCD_DATA5_PIN          GPIO_NUM_12
#define HAL_LCD_DATA6_PIN          GPIO_NUM_11
#define HAL_LCD_DATA7_PIN          GPIO_NUM_10
#define HAL_LCD_DATA8_PIN          GPIO_NUM_9
#define HAL_LCD_DATA9_PIN          GPIO_NUM_46
#define HAL_LCD_DATA10_PIN         GPIO_NUM_3
#define HAL_LCD_DATA11_PIN         GPIO_NUM_15
#define HAL_LCD_DATA12_PIN         GPIO_NUM_7
#define HAL_LCD_DATA13_PIN         GPIO_NUM_6
#define HAL_LCD_DATA14_PIN         GPIO_NUM_5
#define HAL_LCD_DATA15_PIN         GPIO_NUM_4

/* 3-wire SPI control interface, multiplexed onto PCLK/DE/VSYNC pins */
#define HAL_LCD_SPI_CS_PIN         GPIO_NUM_39
#define HAL_LCD_SPI_SCL_PIN        GPIO_NUM_40
#define HAL_LCD_SPI_SDA_PIN        GPIO_NUM_41

#define HAL_DISPLAY_WIDTH          480
#define HAL_DISPLAY_HEIGHT         480

#define HAL_DISPLAY_SWAP_XY        false
#define HAL_DISPLAY_MIRROR_X       false
#define HAL_DISPLAY_MIRROR_Y       false

#define HAL_DISPLAY_OFFSET_X       0
#define HAL_DISPLAY_OFFSET_Y       0

#define HAL_BACKLIGHT_PWM_PIN      GPIO_NUM_48
#define HAL_BACKLIGHT_EN_PIN       GPIO_NUM_45
#define HAL_BACKLIGHT_OUTPUT_INVERT true

#endif // HAL_CONFIG_H
