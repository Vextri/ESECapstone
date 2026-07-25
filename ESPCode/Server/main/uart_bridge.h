#ifndef UART_BRIDGE_H
#define UART_BRIDGE_H

#include <stdbool.h>

#include "bridge_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bridge is considered disconnected once this many microseconds pass with no
 * STATUS/ACK/BOOT_SYNC line received from the Pico. */
#define UART_BRIDGE_TIMEOUT_US (5 * 1000000)

/* How long to wait for an ACK|action=DISPENSE before treating it as a
 * timeout (used by both the UART bridge and the LCD's manual dispense path). */
#define DISPENSE_ACK_TIMEOUT_US (40 * 1000000LL)

void start_uart_bridge(void);

/* Sends CMD|action=DISPENSE[|slot=n] directly to the Pico. */
void bridge_send_dispense_for_slot(int slot_number);

/* Sends CMD|action=LOAD_PROFILE|... for the given slot to the Pico. */
void bridge_send_load_profile_for_slot(const pill_slot_state_t *slot_state);

/* Sends CMD|action=SET_TIME|epoch=<now> if the ESP's own clock is valid. */
bool bridge_send_set_time(void);

/* Queues slot_number for dispensing (deduplicated). Caller must hold
 * bridge_state_mutex. Returns false if the queue is full or the slot is
 * invalid. */
bool bridge_enqueue_dispense_slot_locked(pico_bridge_state_t *state, int slot_number);

/* Pops and starts the next queued dispense, if any and none is already in
 * flight. Caller must hold bridge_state_mutex. */
bool bridge_start_next_dispense_locked(pico_bridge_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
