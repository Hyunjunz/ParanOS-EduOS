#pragma once
#include <stdint.h>

typedef struct {
    uint8_t status;
    uint8_t chs_first[3];
    uint8_t type;
    uint8_t chs_last[3];
    uint32_t lba_first;
    uint32_t sectors;
} __attribute__((packed)) mbr_part_t;

typedef struct {
    mbr_part_t parts[4];
    int valid;
} mbr_t;

int mbr_parse(const void* sector0, mbr_t* out);

