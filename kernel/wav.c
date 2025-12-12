#include "wav.h"

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }

int wav_parse(const uint8_t *buf, uint32_t len, wav_info_t *out)
{
    if (!buf || len < 44 || !out)
        return -1;
    if (rd32(buf + 0) != 0x46464952 || rd32(buf + 8) != 0x45564157) // "RIFF" "WAVE"
        return -1;
    uint32_t pos = 12;
    uint16_t audio_fmt = 0;
    uint16_t channels = 0;
    uint32_t rate = 0;
    uint16_t bits = 0;
    const uint8_t *data = 0;
    uint32_t data_bytes = 0;
    while (pos + 8 <= len)
    {
        uint32_t id = rd32(buf + pos);
        uint32_t sz = rd32(buf + pos + 4);
        pos += 8;
        if (pos + sz > len)
            break;
        if (id == 0x20746d66) // "fmt "
        {
            if (sz < 16)
                return -1;
            audio_fmt = rd16(buf + pos + 0);
            channels = rd16(buf + pos + 2);
            rate = rd32(buf + pos + 4);
            bits = rd16(buf + pos + 14);
        }
        else if (id == 0x61746164) // "data"
        {
            data = buf + pos;
            data_bytes = sz;
        }
        pos += sz;
    }
    if (audio_fmt != 1 || !data || data_bytes == 0 || (bits != 8 && bits != 16))
        return -1;
    out->rate = rate;
    out->channels = channels;
    out->bits = bits;
    out->data = data;
    out->data_bytes = data_bytes;
    return 0;
}
