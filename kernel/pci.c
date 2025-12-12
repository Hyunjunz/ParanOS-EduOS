#include "pci.h"
#include "sys/cpu.h"

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off)
{
    uint32_t addr = (uint32_t)((1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (off & 0xFC));
    outl(0xCF8, addr);
    return inl(0xCFC);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off)
{
    uint32_t v = pci_config_read32(bus, slot, func, off & 0xFC);
    return (uint16_t)((v >> ((off & 2) * 8)) & 0xFFFF);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint16_t val)
{
    uint32_t v = pci_config_read32(bus, slot, func, off & 0xFC);
    int shift = (off & 2) * 8;
    v &= ~(0xFFFFu << shift);
    v |= ((uint32_t)val << shift);
    uint32_t addr = (uint32_t)((1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (off & 0xFC));
    outl(0xCF8, addr);
    outl(0xCFC, v);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val)
{
    uint32_t addr = (uint32_t)((1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (off & 0xFC));
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

int pci_find_device(uint16_t vendor, uint16_t device, uint8_t *out_bus, uint8_t *out_slot, uint8_t *out_func)
{
    for (uint8_t bus = 0; bus < 32; ++bus)
    {
        for (uint8_t slot = 0; slot < 32; ++slot)
        {
            uint32_t id = pci_config_read32(bus, slot, 0, 0);
            if (id == 0xFFFFFFFF)
                continue;
            uint16_t ven = (uint16_t)(id & 0xFFFF);
            uint16_t dev = (uint16_t)((id >> 16) & 0xFFFF);
            if (ven == vendor && dev == device)
            {
                if (out_bus) *out_bus = bus;
                if (out_slot) *out_slot = slot;
                if (out_func) *out_func = 0;
                return 1;
            }
        }
    }
    return 0;
}
