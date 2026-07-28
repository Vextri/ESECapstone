#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initializes the ST7796 LCD over SPI3, the physical 5-way button pad
 * (left/right/up/down/OK), and starts the button-polling and screen-render
 * tasks. The screen shows a live status view plus a slot-select menu with
 * a confirm-before-dispense step and dedicated waiting/success/failure
 * feedback screens. */
void start_lcd_display(void);

#ifdef __cplusplus
}
#endif

#endif
