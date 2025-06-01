#include "mmu.h"
#include <pmap.h>
#include <virtio.h>

Pte *boot_pgdir = (Pte *)(0xFFFFFFC001000000ULL);

void virtio_init(void) {
    map_mem(boot_pgdir, MMIO_BEGIN_VA, VIRTIO_BEGIN_ADDRESS,
            MMIO_END_VA - MMIO_BEGIN_VA, PTE_V | PTE_RW | PTE_GLOBAL);
}