#pragma once
#include <stdint.h>

// Minimal WAV loader (PCM only)
typedef struct
{
    uint32_t rate;
    uint16_t channels;
    uint16_t bits;
    const uint8_t *data;
    uint32_t data_bytes;
} wav_info_t;

int wav_parse(const uint8_t *buf, uint32_t len, wav_info_t *out);
