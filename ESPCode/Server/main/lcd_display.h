#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initializes the ST7796 LCD over SPI3, the physical up/down/select buttons,
 * and starts the button-polling and screen-render tasks. The screen shows a
 * live status view plus a slot-select / dispense-or-edit menu tree. */
void start_lcd_display(void);

#ifdef __cplusplus
}
#endif

#endif
