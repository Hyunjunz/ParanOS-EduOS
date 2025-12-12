#pragma once
#include <stdint.h>

// Minimal PCI config helpers (config space via 0xCF8/0xCFC)
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off);
void     pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val);
void     pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint16_t val);
int      pci_find_device(uint16_t vendor, uint16_t device, uint8_t *out_bus, uint8_t *out_slot, uint8_t *out_func);
