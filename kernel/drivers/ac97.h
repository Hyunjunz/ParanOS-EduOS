#pragma once
#include <stdint.h>

// Minimal AC'97 PCI audio driver (QEMU -device AC97)
// Only supports PCM OUT for simple WAV playback.

int ac97_init(void);
int ac97_play_pcm(const uint16_t *pcm, uint32_t frames, uint32_t rate_hz, uint8_t channels);
int ac97_is_ready(void);
