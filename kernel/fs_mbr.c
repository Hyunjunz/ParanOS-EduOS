#include "fs_mbr.h"
#include <string.h>

int mbr_parse(const void* sector0, mbr_t* out)
{
    if (!sector0 || !out) return -1;
    const uint8_t* b = (const uint8_t*)sector0;
    if (b[510] != 0x55 || b[511] != 0xAA)
    {
        out->valid = 0;
        memset(out, 0, sizeof(*out));
        return -2;
    }
    const mbr_part_t* p = (const mbr_part_t*)(b + 446);
    for (int i = 0; i < 4; ++i)
        out->parts[i] = p[i];
    out->valid = 1;
    return 0;
}

