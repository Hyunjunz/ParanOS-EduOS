#include <limine.h>

// Delimit the request list so Limine never mis-detects stray data as requests.
__attribute__((used, section(".limine_requests_start_marker")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

// Opt into the latest supported base revision shipped with this tree.
__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(4);

// Keep the kernel on classic 4-level paging; QEMU's default CPUs do not expose
// LA57 and requesting it triggers the paging-mode panic seen at boot.
//__attribute__((used, section(".limine_requests")))
//static volatile struct limine_paging_mode_request limine_pm_request = {
//   .id = LIMINE_PAGING_MODE_REQUEST_ID,
//    .revision = 0,
//    .mode = LIMINE_PAGING_MODE_X86_64_4LVL,
//    .max_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
//    .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
//};

__attribute__((used, section(".limine_requests_end_marker")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;
