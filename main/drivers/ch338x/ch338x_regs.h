#ifndef DRIVERS_CH338X_REGS_H
#define DRIVERS_CH338X_REGS_H

/* Register map, from demo/usb_hub/main/boards/hub_rgb/base_hub_regs.h. */

#define CH338X_REG_VID 0x00 /* 16-bit */
#define CH338X_REG_PID 0x02 /* 16-bit */

#define CH338X_REG_FUN_CFG1 0x06
#define CH338X_POWER_MODE            (1u << 7) /* 0 bus-powered, 1 self-powered */
#define CH338X_HIGH_SPEED_DISABLE    (1u << 5) /* 1 = full speed only */
#define CH338X_STT_MTT_SELECT        (1u << 4) /* 0 STT, 1 MTT (default) */
#define CH338X_OCP_CONTROL_MASK      ((1u << 2) | (1u << 1))
#define CH338X_PORT_POWER_CONTROL    (1u << 0) /* 0 ganged, 1 per-port */

#define CH338X_REG_FUN_CFG3 0x08
#define CH338X_PORT_REMAP_ENABLE     (1u << 3)
#define CH338X_STRING_DESC_ENABLE    (1u << 0)

#define CH338X_REG_DEV_REMOVABLE 0x09 /* bit n = downstream port n not removable */

#define CH338X_REG_FUN_CFG4 0xD2
#define CH338X_FORCE_FULL_SPEED      (1u << 1)
#define CH338X_LED_ENABLE            (1u << 0)

#define CH338X_REG_FUN_CFG5 0xD3
#define CH338X_LED_POLARITY          (1u << 7) /* 1 active high */
#define CH338X_OCP_POLARITY          (1u << 6) /* 1 active high */
#define CH338X_PORT_POWER_POLARITY   (1u << 5) /* 1 active high */

#define CH338X_REG_REMAP12 0xFB
#define CH338X_REG_REMAP34 0xFC
#define CH338X_REG_REMAP56 0xFD
#define CH338X_REG_REMAP7  0xFE

#endif // DRIVERS_CH338X_REGS_H
