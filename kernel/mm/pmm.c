#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <mm/pmm.h>
#include <lib/rand.h>
#include <lib/print.h>

static bool full_overlap_check(uint64_t base1, uint64_t top1,
                               uint64_t base2, uint64_t top2) {
    return ((base1 >= base2 && base1 <  top2)
         && (top1  >  base2 && top1  <= top2));
}

bool check_usable_memory(uint64_t base, uint64_t top) {
    uint64_t overlap_remaining = top - base;

    size_t entries = 0;
    struct memmap_entry *mm = get_memmap(&entries);

    for (size_t i = 0; i < entries; i++) {
        if (mm[i].type != MEMMAP_USABLE
#if defined (UEFI)
         && mm[i].type != MEMMAP_EFI_RECLAIMABLE
#endif
         && mm[i].type != MEMMAP_BOOTLOADER_RECLAIMABLE
         && mm[i].type != MEMMAP_KERNEL_AND_MODULES) {
            continue;
        }

        uint64_t memmap_top = mm[i].base + mm[i].length;

        if (full_overlap_check(base, top, mm[i].base, memmap_top)) {
            return true;
        }

        // Count how many bytes from a real-RAM entry overlap our range
        if ((mm[i].base >= base && mm[i].base < top)
         || (memmap_top > base && memmap_top <= top)) {
            uint64_t overlap_bottom = base;
            if (mm[i].base >= base && mm[i].base < top) {
                overlap_bottom = mm[i].base;
            }

            uint64_t overlap_top = top;
            if (memmap_top > base && memmap_top <= top) {
                overlap_top = memmap_top;
            }

            uint64_t overlap_size = overlap_top - overlap_bottom;
            overlap_remaining -= overlap_size;

            if (overlap_remaining == 0) {
                return true;
            }
        }
    }

    return false;
}

void pmm_randomise_memory(void) {
    print("pmm: Randomising memory contents...");

    size_t entries = 0;
    struct memmap_entry *mm = get_memmap(&entries);

    for (size_t i = 0; i < entries; i++) {
        if (mm[i].type != MEMMAP_USABLE)
            continue;

#if defined (BIOS)
        // We're not going to randomise memory above 4GiB from protected mode,
        // are we?
        if (mm[i].base >= 0x100000000) {
            continue;
        }
#endif

        uint8_t *ptr = (void *)(uintptr_t)mm[i].base;
        size_t len = mm[i].length;

        for (size_t j = 0;;) {
            uint32_t random = rand32();
            uint8_t *rnd_data = (void *)&random;
            if (j >= len)
                break;
            ptr[j++] = rnd_data[0];
            if (j >= len)
                break;
            ptr[j++] = rnd_data[1];
            if (j >= len)
                break;
            ptr[j++] = rnd_data[2];
            if (j >= len)
                break;
            ptr[j++] = rnd_data[3];
        }
    }

    print("\n");
}
