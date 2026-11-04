#ifndef APP_H
#define APP_H

/* Brings up the screen, FT6336U touch, and the INA226 power monitor, then
 * starts a task that periodically logs power readings. Called once from
 * app_main(). */
void app_main_start(void);

#endif // APP_H
