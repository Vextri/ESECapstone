/**
 * Pico Flash Storage
 *
 * Persists medication profile slots 0-4 to the last sector of flash.
 * Data survives power-off and is restored automatically at boot.
 *
 * Layout: last 4096-byte flash sector, first 256 bytes used.
 *   [magic u32][dispense_profile_t x5][checksum u32]
 */

#ifndef PICO_STORAGE_H
#define PICO_STORAGE_H

#include "pill_dispenser.h"
#include <stdbool.h>

// Number of profile slots persisted to flash (all slots 0-4)
#define FLASH_PROFILE_SLOT_COUNT MAX_PROFILES

/**
 * Save the first FLASH_PROFILE_SLOT_COUNT profiles to flash.
 * @param profiles Full profiles array; only indices 0-2 are written.
 */
void flash_profiles_save(const dispense_profile_t *profiles);

/**
 * Load FLASH_PROFILE_SLOT_COUNT profiles from flash into the provided array.
 * @param profiles Destination array; indices 0-2 are overwritten on success.
 * @return true if valid data was found and loaded, false if flash is blank or corrupt.
 */
bool flash_profiles_load(dispense_profile_t *profiles);

#endif // PICO_STORAGE_H
