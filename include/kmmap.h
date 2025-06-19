#ifndef __KMMAP_H
#define __KMMAP_H

#include <mmu.h>
#include <stdint.h>

#define KMMAP_COUNT (KMMAP_SIZE / PAGE_SIZE)

void *kmmap_alloc(u_reg_t pa, size_t size, uint32_t perm);
void kmmap_free(void *mapped_va);

#endif