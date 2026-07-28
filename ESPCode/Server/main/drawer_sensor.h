#ifndef DRAWER_SENSOR_H
#define DRAWER_SENSOR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Hall-effect sensor wired directly to the ESP (not through the Pico) that
 * detects when the pill drawer/tray is opened. Used to confirm a dispensed
 * dose was actually picked up: while bridge_state.awaiting_drawer_open is
 * true, a trigger calls bridge_mark_dispense_taken_locked() for
 * bridge_state.drawer_open_slot, and also cancels the "pickup not
 * confirmed" reminder in notify.c (since that timer checks live state at
 * fire time). Call once at boot, after start_uart_bridge() so
 * bridge_state_mutex already exists. */
void start_drawer_sensor(void);

#ifdef __cplusplus
}
#endif

#endif
