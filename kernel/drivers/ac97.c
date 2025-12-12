#include "ac97.h"
#include <stddef.h>
#include "pci.h"
#include <serial.h>
#include "mm/vmm.h"
#include "mm/pmm.h"
#include "string.h"
#include "io.h"
#include "sys/cpu.h"

extern void *kmalloc(size_t sz);
extern void *ext_mem_alloc(size_t sz);

#define AC97_VENDOR_INTEL 0x8086
#define AC97_DEVICE_ICH   0x2415

// Registers (IO mapped)
#define AC97_NAM_BAR 0x10
#define AC97_NABM_BAR 0x14
#define AC97_GLOB_CNT 0x2C
// Native audio mixer registers (offsets from NAM BAR)
#define AC97_MASTER_VOL     0x02
#define AC97_PCM_OUT_VOL    0x18
#define AC97_POWER_CTRL     0x26
#define AC97_PCM_FRONT_RATE 0x2C

// Bus master registers (offsets from NABM BAR)
#define BDBAR_PCM_OUT   0x10 // Buffer descriptor base addr
#define CIV_PCM_OUT     0x14 // Current index value
#define LVI_PCM_OUT     0x15 // Last valid index
#define SR_PCM_OUT      0x16 // Status register (2 bytes)
#define PICB_PCM_OUT    0x18 // Position in current buffer
#define CR_PCM_OUT      0x1B // Control register

#define SR_FLAG_DMA_DONE 0x01
#define SR_FLAG_IOC      0x04
#define CR_IOCE          0x04
#define CR_RR            0x02
#define CR_RUN           0x01

#define BD_ENTRY_COUNT 32

typedef struct __attribute__((packed))
{
    uint32_t addr;
    uint16_t samples;   // bits 0-14: sample count-1, bit15: IOC
    uint16_t control;   // bit14: BUP (last buffer)
} ac97_bd_t;


static uint16_t g_nam = 0;
static uint16_t g_nabm = 0;
static ac97_bd_t *g_bd = 0;
static uint16_t *g_pcm_buf = 0;
static uint32_t g_pcm_frames = 0;
static uint32_t g_pcm_pos = 0;
static uint8_t g_ready = 0;
static uint32_t g_pcm_bytes_alloc = 0;
static uint32_t g_pcm_phys = 0;
static uint32_t g_bd_phys = 0;
void *pmm_alloc(void);
void *pmm_alloc_contig(uint32_t pages);

static void outw_offset(uint16_t base, uint16_t off, uint16_t v) { outw(base + off, v); }
static uint16_t inw_offset(uint16_t base, uint16_t off) { return inw(base + off); }
static uint8_t inb_offset(uint16_t base, uint16_t off) { return inb(base + off); }
static void outb_offset(uint16_t base, uint16_t off, uint8_t v) { outb(base + off, v); }

static void ac97_hw_reset(void)
{
    // 1. Global Control: cold reset 해제
    outl(g_nabm + AC97_GLOB_CNT, 0x00000002);

    // 짧은 딜레이 (QEMU에서도 중요)
    for (volatile int i = 0; i < 100000; ++i)
        (void)inw_offset(g_nam, AC97_MASTER_VOL);

    // 2. NAM reset (mixer reset)
    outw_offset(g_nam, 0x00, 0x0000);

    for (volatile int i = 0; i < 100000; ++i)
        (void)inw_offset(g_nam, AC97_MASTER_VOL);
}

static int alloc_phys_block(uint32_t bytes, uint32_t *phys_out, void **va_out)
{
    uint32_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages == 0) pages = 1;

    uint8_t *first = (uint8_t *)pmm_alloc_contig(pages);
    if (!first)
        return -1;

    uintptr_t hhdm = vmm_hhdm_offset();
    *phys_out = (uint32_t)((uintptr_t)first - hhdm);
    *va_out = first;
    return 0;
}


static void ac97_powerup(void)
{
    outw_offset(g_nam, AC97_POWER_CTRL, 0x0000); // clear powerdown
    // Wait briefly for power status to settle.
    for (int i = 0; i < 1000; ++i)
    {
        uint16_t st = inw_offset(g_nam, AC97_POWER_CTRL);
        if ((st & 0xF000) == 0)
            break;
    }
}

int ac97_is_ready(void)
{
    return g_ready;
}

int ac97_init(void)
{
    uint8_t bus, slot, func;
    if (!pci_find_device(AC97_VENDOR_INTEL, AC97_DEVICE_ICH, &bus, &slot, &func))
    {
        serial_printf("[AC97] device not found\n");
        return -1;
    }

    uint32_t nam_bar  = pci_config_read32(bus, slot, func, AC97_NAM_BAR);
    uint32_t nabm_bar = pci_config_read32(bus, slot, func, AC97_NABM_BAR);

    // IO space 강제 설정
    if (!(nam_bar & 1) || !(nabm_bar & 1))
    {
        pci_config_write32(bus, slot, func, AC97_NAM_BAR,  0x1C01);
        pci_config_write32(bus, slot, func, AC97_NABM_BAR, 0x1C41);
        nam_bar  = pci_config_read32(bus, slot, func, AC97_NAM_BAR);
        nabm_bar = pci_config_read32(bus, slot, func, AC97_NABM_BAR);
    }

    g_nam  = (uint16_t)(nam_bar & ~3u);
    g_nabm = (uint16_t)(nabm_bar & ~3u);

    // PCI CMD: IO + Bus Master
    uint16_t cmd = pci_config_read16(bus, slot, func, 0x04);
    cmd |= 0x0005;
    pci_config_write16(bus, slot, func, 0x04, cmd);

    // 하드웨어 리셋
    ac97_hw_reset();

    // Power up
    outw_offset(g_nam, AC97_POWER_CTRL, 0x0000);

    // 볼륨 unmute + max
    outw_offset(g_nam, AC97_MASTER_VOL,  0x0000);
    outw_offset(g_nam, AC97_PCM_OUT_VOL, 0x0000);

    // BD 테이블 할당
    if (alloc_phys_block(sizeof(ac97_bd_t) * BD_ENTRY_COUNT,
                          &g_bd_phys, (void **)&g_bd) != 0)
        return -1;

    memset(g_bd, 0, sizeof(ac97_bd_t) * BD_ENTRY_COUNT);
    outl(g_nabm + BDBAR_PCM_OUT, g_bd_phys);

    // DMA 엔진 초기화
    outb_offset(g_nabm, CR_PCM_OUT, CR_RR);
    while (inb_offset(g_nabm, CR_PCM_OUT) & CR_RR);

    outw_offset(g_nabm, SR_PCM_OUT, 0x001C);

    g_ready = 1;
    serial_printf("[AC97] init OK (NAM=0x%x NABM=0x%x)\n", g_nam, g_nabm);
    return 0;
}


int ac97_play_pcm(const uint16_t *pcm, uint32_t frames,
                  uint32_t rate_hz, uint8_t channels)
{
    if (!g_ready || !pcm || frames == 0 || (channels != 1 && channels != 2))
        return -1;

    // Variable Rate Audio enable
    outw_offset(g_nam, 0x2A, 0x0001);

    if (rate_hz < 8000)  rate_hz = 8000;
    if (rate_hz > 48000) rate_hz = 48000;
    outw_offset(g_nam, AC97_PCM_FRONT_RATE, (uint16_t)rate_hz);

    uint32_t bytes = frames * channels * 2;
    uint32_t need  = (bytes + 0xFFF) & ~0xFFFu;

    if (!g_pcm_buf || need > g_pcm_bytes_alloc)
    {
        if (alloc_phys_block(need, &g_pcm_phys, (void **)&g_pcm_buf) != 0)
            return -1;
        g_pcm_bytes_alloc = need;
    }

    memcpy(g_pcm_buf, pcm, bytes);

    // BD 작성
    uint32_t frames_left = frames;
    uint32_t offset = 0;
    uint32_t desc = 0;

    while (frames_left && desc < BD_ENTRY_COUNT)
    {
        uint32_t fc = frames_left;
        if (fc * channels > 0xFFFE)
            fc = 0xFFFE / channels;

        uint32_t sample_count = fc * channels;

        g_bd[desc].addr = g_pcm_phys + offset;
        g_bd[desc].samples = (uint16_t)((sample_count - 1) & 0x7FFF);
        g_bd[desc].control = 0;

        offset += sample_count * 2;
        frames_left -= fc;
        desc++;
    }

    if (desc == 0)
        return -1;

    // 마지막 BD
    g_bd[desc - 1].samples |= 0x8000; // IOC
    g_bd[desc - 1].control |= 0x4000; // BUP

    // DMA 리셋
    outb_offset(g_nabm, CR_PCM_OUT, CR_RR);
    while (inb_offset(g_nabm, CR_PCM_OUT) & CR_RR);

    outw_offset(g_nabm, SR_PCM_OUT, 0x001C);

    // 재생 시작
    outb_offset(g_nabm, CIV_PCM_OUT, 0);
    outb_offset(g_nabm, LVI_PCM_OUT, (uint8_t)(desc - 1));
    outb_offset(g_nabm, CR_PCM_OUT, CR_RUN);

    serial_printf("[AC97] play %u frames @ %u Hz, ch=%u\n",
                  frames, rate_hz, channels);
    uint8_t sr = inb_offset(g_nabm, SR_PCM_OUT);
    uint8_t cr = inb_offset(g_nabm, CR_PCM_OUT);
    uint8_t civ = inb_offset(g_nabm, CIV_PCM_OUT);

    serial_printf("[AC97 DBG] SR=0x%x CR=0x%x CIV=%u\n", sr, cr, civ);

    return 0;
}
