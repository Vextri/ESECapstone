/**
 * ESP UART Command Receiver
 *
 * Polls uart1 for incoming commands from the ESP and dispatches them.
 * Call esp_uart_poll() frequently in the main loop.
 *
 * Supported commands (pipe-delimited, newline-terminated):
 *
 *   CMD|action=LOAD_PROFILE|slot=<0-4>|med=<name>|total=<n>|dose=<n>|time=<ms>
 *       Loads a medication profile into the given slot.
 *       ACK: ACK|action=LOAD_PROFILE|slot=<n>|result=ok
 *
 * ACKs and errors are written back to the ESP over the same UART.
 */

#ifndef ESP_UART_H
#define ESP_UART_H

/**
 * Poll the ESP UART for incoming bytes and process any complete lines.
 * Must be called frequently (every main loop iteration).
 */
void esp_uart_poll(void);

/**
 * Inject a pre-built command line directly (e.g. from the USB serial terminal).
 * Processes identically to a line received from the ESP.
 * @param line Null-terminated command string (no newline needed).
 */
void esp_uart_inject_line(char *line);

#endif // ESP_UART_H
