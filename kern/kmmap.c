#include <kmmap.h>
#include <mmu.h>
#include <pmap.h>
#include <printk.h>
#include <string.h>

static uint64_t kmmap_bmap[KMMAP_COUNT] = {0};

void *kmmap_alloc(u_reg_t pa, size_t size, uint32_t perm) {
    if (size % PAGE_SIZE != 0) {
        panic("kmmap_alloc: invalid kmmap size %lx", size);
    }

    size_t page_count = size / PAGE_SIZE;

    u_reg_t vpn_offset = 0;
    int found = 0;

    for (size_t i = 0; i < KMMAP_COUNT; i++) {
        if (kmmap_bmap[i] == 0) {
            int available = 1;
            for (size_t j = i; j < i + page_count; j++) {
                if (kmmap_bmap[j] != 0) {
                    available = 0;
                    break;
                }
            }

            if (available == 1) {
                found = 1;
                vpn_offset = i;
                break;
            }
        }
    }

    if (found == 0) {
        debugk("kmmap_alloc", "no free kmap address space for size: %lx\n",
               size);
        return NULL;
    }

    for (size_t i = vpn_offset; i < vpn_offset + page_count; i++) {
        kmmap_bmap[i] = (vpn_offset + page_count - i);
    }

    u_reg_t begin_va = (vpn_offset << PAGE_SHIFT) + KMMAP_BEGIN_VA;

    kmap(begin_va, pa, size, perm);

    return (void *)(begin_va);
}

// 若地址合法但未映射，本函数输出警告
void kmmap_free(void *mapped_va) {
    u_reg_t va = (u_reg_t)mapped_va;

    if ((va < KMMAP_BEGIN_VA) || (va >= KMMAP_END_VA)) {
        panic("kmmap_free: invalid va: 0x%016lx", va);
    }

    if (va % PAGE_SIZE != 0) {
        panic("kmmap_free: va not aligned to PAGE_SIZE: 0x%016lx", va);
    }

    u_reg_t vpn = VPN(va);
    u_reg_t vpn_offset = vpn - VPN(KMMAP_BEGIN_VA);

    size_t page_count = kmmap_bmap[vpn_offset];

    if (page_count == 0) {
        debugk("kmmap_free", "freed memory: 0x%016lx\n", va);
        return;
    }

    kunmap(va, page_count * PAGE_SIZE);

    memset(&kmmap_bmap[vpn_offset], 0, page_count * sizeof(uint64_t));
}