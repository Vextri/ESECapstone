/**
 * Pico Flash Storage Implementation
 */

#include "pico_storage.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>
#include <stdio.h>

// Magic value written at the start of the stored block to detect valid data
#define STORAGE_MAGIC 0xD1573A5E

// Target: last sector of flash
// PICO_FLASH_SIZE_BYTES is board-defined (4 MB for pico2)
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

// flash_range_program requires a multiple of FLASH_PAGE_SIZE (256 bytes).
// 2 pages = 512 bytes — needed now that all 5 slots with schedule fields are stored.
#define STORE_PADDED_SIZE (FLASH_PAGE_SIZE * 2)

typedef struct {
    uint32_t         magic;
    dispense_profile_t slots[FLASH_PROFILE_SLOT_COUNT];
    uint32_t         checksum;
} flash_profile_store_t;

_Static_assert(sizeof(flash_profile_store_t) <= STORE_PADDED_SIZE,
               "flash_profile_store_t exceeds STORE_PADDED_SIZE - increase STORE_PADDED_SIZE");

// Sum of every byte in the slots array
static uint32_t compute_checksum(const dispense_profile_t *slots) {
    const uint8_t *p = (const uint8_t *)slots;
    uint32_t sum = 0;
    for (size_t i = 0; i < sizeof(dispense_profile_t) * FLASH_PROFILE_SLOT_COUNT; i++) {
        sum += p[i];
    }
    return sum;
}

void flash_profiles_save(const dispense_profile_t *profiles) {
    // Build a 256-byte buffer (flash page), initialised to 0xFF (erased state)
    static uint8_t page_buf[STORE_PADDED_SIZE];
    memset(page_buf, 0xFF, sizeof(page_buf));

    flash_profile_store_t *store = (flash_profile_store_t *)page_buf;
    store->magic    = STORAGE_MAGIC;
    memcpy(store->slots, profiles, sizeof(dispense_profile_t) * FLASH_PROFILE_SLOT_COUNT);
    store->checksum = compute_checksum(store->slots);

    // Erase + program must run with interrupts disabled
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, page_buf, STORE_PADDED_SIZE);
    restore_interrupts(ints);

    printf("[FLASH] Profiles saved (slots 0-2)\n");
}

bool flash_profiles_load(dispense_profile_t *profiles) {
    const uint8_t *flash_ptr =
        (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
    const flash_profile_store_t *store =
        (const flash_profile_store_t *)flash_ptr;

    if (store->magic != STORAGE_MAGIC) {
        printf("[FLASH] No saved profiles found (flash blank or first boot)\n");
        return false;
    }

    uint32_t expected = compute_checksum(store->slots);
    if (store->checksum != expected) {
        printf("[FLASH] Checksum mismatch — data corrupt, ignoring\n");
        return false;
    }

    memcpy(profiles, store->slots,
           sizeof(dispense_profile_t) * FLASH_PROFILE_SLOT_COUNT);
    printf("[FLASH] Profiles restored from flash\n");
    return true;
}
