/* ============================================================================
 * UART_BRIDGE.H - Pico Communication Protocol
 * ----------------------------------------------------------------------------
 * Public interface to the line-based UART protocol spoken between the ESP
 * and the Pico 2 controller (CMD|action=... going out, ACK|/STATUS| coming
 * back). Owns the dispense request queue so multiple slots can be asked to
 * dispense without losing a request while one is already in flight.
 * ============================================================================ */

#ifndef UART_BRIDGE_H
#define UART_BRIDGE_H

#include <stdbool.h>

#include "bridge_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bridge is considered disconnected once this many microseconds pass with no
 * STATUS/ACK/BOOT_SYNC line received from the Pico. Paired with the
 * heartbeat below (which keeps real traffic flowing every ~60s whenever the
 * Pico is actually alive), so this only needs to survive a couple of missed
 * heartbeat cycles, not long real-world gaps between dispenses. */
#define UART_BRIDGE_TIMEOUT_US (3LL * 60 * 1000000)

/* How often the ESP re-sends SET_TIME as a heartbeat, purely to keep the
 * "connected" status accurate. The Pico has no periodic heartbeat of its
 * own (it only ever speaks after boot or a dispense), so without this the
 * dashboard would show "disconnected" during any normal quiet gap between
 * events, even though the Pico is fine. Re-sending SET_TIME is harmless
 * (idempotent) and the Pico always ACKs it, which is what actually keeps
 * the connection status fresh. */
#define UART_BRIDGE_HEARTBEAT_INTERVAL_US (60LL * 1000000)

/* How often a clear, human-readable "is the Pico actually connected" line
 * gets printed to the ESP's own serial log, separate from and easier to
 * spot than the scrolling per-message protocol logs. */
#define UART_BRIDGE_STATUS_PRINT_INTERVAL_US (10LL * 1000000)

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

/* Queues slot_number for dispensing (deduplicated). is_scheduled marks
 * whether this request came from the schedule checker (true) versus a
 * manual trigger (false), used later to decide whether to send the
 * "dose dispensed" notification. Caller must hold bridge_state_mutex.
 * Returns false if the queue is full or the slot is invalid. */
bool bridge_enqueue_dispense_slot_locked(pico_bridge_state_t *state, int slot_number, bool is_scheduled);

/* Pops and starts the next queued dispense, if any and none is already in
 * flight. Caller must hold bridge_state_mutex. */
bool bridge_start_next_dispense_locked(pico_bridge_state_t *state);

#ifdef __cplusplus
}
#endif

#endif
