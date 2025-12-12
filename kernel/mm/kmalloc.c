#include <stddef.h>
#include <stdint.h>
#include <mm/pmm.h>

void* malloc(size_t size)
{
    // 페이지 단위만큼 할당 (단순 구현)
    size_t pages = (size + 4095) / 4096;
    void* block = NULL;
    for (size_t i = 0; i < pages; ++i) {
        void* p = ext_mem_alloc(4096);
        if (!p) return NULL;
        if (i == 0) block = p;
    }
    return block;
}

void free(void* ptr)
{
    if (ptr) {
        // pmm_free expects (ptr, size); this simple free releases one page.
        pmm_free(ptr, 4096);
    }
}
