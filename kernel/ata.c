#include "ata.h"
#include "io.h"
#include "serial.h"

static inline uint8_t inb_p(uint16_t port) { uint8_t v = inb(port); io_wait(); return v; }

static int ata_wait_bsy(void)
{
    for (int i = 0; i < 1000000; ++i)
    {
        if (!(inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_BSY))
            return 0;
    }
    return -1;
}

static int ata_wait_drq(void)
{
    for (int i = 0; i < 1000000; ++i)
    {
        uint8_t st = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (st & ATA_SR_ERR) return -2;
        if (st & ATA_SR_DF)  return -3;
        if (st & ATA_SR_DRQ) return 0;
    }
    return -1;
}

void ata_init(void)
{
    // Select master, LBA
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0);
    io_wait();
}

int ata_identify(ata_identify_t* out)
{
    if (!out) return -1;
    out->present = 0;
    for (int i = 0; i < 256; ++i) out->raw[i] = 0;
    out->lba28_sectors = 0;

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0); // master, LBA
    io_wait();

    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, 0);

    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    // Some devices need time to update status; rely on BSY wait instead of immediate zero check
    if (ata_wait_bsy()) return -1;

    // Check if device is ATA (LBA1/LBA2 non-zero implies ATAPI)
    uint8_t cl = inb(ATA_PRIMARY_IO + ATA_REG_LBA1);
    uint8_t ch = inb(ATA_PRIMARY_IO + ATA_REG_LBA2);
    if (cl || ch) return -2; // not ATA

    if (ata_wait_drq()) return -3;

    // Read 256 words
    for (int i = 0; i < 256; ++i)
    {
        uint16_t w;
        __asm__ volatile ("inw %1, %0" : "=a"(w) : "Nd"(ATA_PRIMARY_IO + ATA_REG_DATA));
        out->raw[i] = w;
    }

    out->present = 1;
    // LBA28 sectors at words 60-61
    out->lba28_sectors = ((uint32_t)out->raw[61] << 16) | out->raw[60];
    serial_printf("[ata] present, LBA28 sectors=%u\n", out->lba28_sectors);
    return 0;
}

int ata_read28(uint32_t lba, uint8_t count, void* buffer)
{
    if (count == 0) return 0;
    if (count > 128) count = 128;
    uint16_t* bufw = (uint16_t*)buffer;

    if (ata_wait_bsy()) return -1;

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_SECT);

    for (uint32_t s = 0; s < count; ++s)
    {
        if (ata_wait_bsy()) return -2;
        if (ata_wait_drq()) return -3;
        for (int i = 0; i < 256; ++i)
        {
            uint16_t w;
            __asm__ volatile ("inw %1, %0" : "=a"(w) : "Nd"(ATA_PRIMARY_IO + ATA_REG_DATA));
            *bufw++ = w;
        }
    }
    return 0;
}

int ata_write28(uint32_t lba, uint8_t count, const void* buffer)
{
    if (count == 0) return 0;
    if (count > 128) count = 128;
    const uint16_t* bufw = (const uint16_t*)buffer;

    if (ata_wait_bsy()) return -1;

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, count);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_SECT);

    for (uint32_t s = 0; s < count; ++s)
    {
        if (ata_wait_bsy()) return -2;
        if (ata_wait_drq()) return -3;
        for (int i = 0; i < 256; ++i)
        {
            uint16_t w = *bufw++;
            __asm__ volatile ("outw %0, %1" :: "a"(w), "Nd"(ATA_PRIMARY_IO + ATA_REG_DATA));
        }
    }
    return 0;
}
