#pragma once
#include <stdint.h>

// Simple ATA PIO (legacy IDE primary bus, master device)

#define ATA_PRIMARY_IO     0x1F0
#define ATA_PRIMARY_CTRL   0x3F6

// IO ports offsets
#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_FEATURES   0x01
#define ATA_REG_SECCOUNT0  0x02
#define ATA_REG_LBA0       0x03
#define ATA_REG_LBA1       0x04
#define ATA_REG_LBA2       0x05
#define ATA_REG_HDDEVSEL   0x06
#define ATA_REG_COMMAND    0x07
#define ATA_REG_STATUS     0x07

#define ATA_REG_SECCOUNT1  0x08
#define ATA_REG_LBA3       0x09
#define ATA_REG_LBA4       0x0A
#define ATA_REG_LBA5       0x0B

// Control ports
#define ATA_REG_ALTSTATUS  0x00
#define ATA_REG_DEVCTRL    0x00

// Status bits
#define ATA_SR_BSY   0x80
#define ATA_SR_DRDY  0x40
#define ATA_SR_DF    0x20
#define ATA_SR_DSC   0x10
#define ATA_SR_DRQ   0x08
#define ATA_SR_CORR  0x04
#define ATA_SR_IDX   0x02
#define ATA_SR_ERR   0x01

// Commands
#define ATA_CMD_IDENTIFY   0xEC
#define ATA_CMD_READ_SECT  0x20  // LBA28 PIO
#define ATA_CMD_WRITE_SECT 0x30  // LBA28 PIO write

typedef struct {
    uint16_t raw[256];
    int present;
    uint32_t lba28_sectors;
} ata_identify_t;

void ata_init(void);
int  ata_identify(ata_identify_t* out);
int  ata_read28(uint32_t lba, uint8_t count, void* buffer); // count in sectors (1..128)
int  ata_write28(uint32_t lba, uint8_t count, const void* buffer);
