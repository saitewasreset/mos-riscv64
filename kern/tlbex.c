#include "mmu.h"
#include "printk.h"
#include "types.h"
#include <bitops.h>
#include <env.h>
#include <pmap.h>

/*
 * 概述：
 *
 * （含 TLB 操作）在校验指定虚拟地址`va`位于允许的用户空间区域后，
 * 分配一个新的物理页面
 * 将该物理页面映射到该地址并增加该页面的引用计数。
 *
 * 若虚拟地址`va`已经存在有效映射，移除原映射，原映射的物理页的引用计数 -1。
 *
 * 页面权限根据虚拟地址所属区域进行配置：
 * 内核映射的用户区域（UVPT 至 ULIM）不设置 PTE_D 标志（只读访问）。
 *
 * Precondition:
 *
 * - `va` **无需**页面对齐，但必须满足以下条件：
 *   - `va >= UTEMP`（0x003f e000）
 *   - `va` 不在 [USTACKTOP, USTACKTOP + PAGE_SIZE) 区间内 [0x7f3f e000，0x7f3f
 * f000）
 *   - `va` 不在 [UENVS, UPAGES)、[UPAGES, UVPT) 区间内，且不 >= ULIM
 * - `pgdir` 必须指向有效的页目录
 * - `asid` 必须是有效的 ASID（用于 TLB 管理）
 *
 * Postcondition:
 *
 * - 分配物理页面并插入`pgdir`页目录的`va`映射项
 * - `va`对应的页表项权限设置为 PTE_R | PTE_V |
 *   （若 va < UTOP 则附加 PTE_W）
 * - 移除`va`地址原有的所有映射，原映射的物理页的引用计数将 -1
 * - 已分配页面的`pp_ref`引用计数增加
 */
void passive_alloc(u_reg_t va, Pte *pgdir, uint16_t asid) {
    struct Page *p = NULL;

    if (va < UTEMP) {
        panic("address too low");
    }

    if (va >= USTACKTOP && va < USTACKTOP + PAGE_SIZE) {
        panic("invalid memory");
    }

    if (va >= UENVS && va < UPAGES) {
        panic("envs zone");
    }

    if (va >= UPAGES && va < UVPT) {
        panic("pages zone");
    }

    if (va >= UVPT && va < ULIM) {
        panic("User VPT zone");
    }

    if (va >= ULIM) {
        panic("kernel address");
    }

    panic_on(page_alloc(&p));

    // Postconditon for `page_alloc`: now, p points to the allocated Page

    panic_on(page_insert(pgdir, asid, p, va,
                         ((va >= UTOP) ? PTE_RO : PTE_RW) | PTE_USER));
}

/*
 * 概述：
 *
 * （含 TLB 操作）在校验指定虚拟地址`va`位于允许的内核空间区域后，
 * 分配一个新的物理页面
 * 将该物理页面映射到该地址并增加该页面的引用计数。
 *
 * 若虚拟地址`va`已经存在有效映射，移除原映射，原映射的物理页的引用计数 -1。
 *
 * 页面权限根据虚拟地址所属区域进行配置：
 * 内核映射的用户区域（UVPT 至 ULIM）不设置 PTE_D 标志（只读访问）。
 *
 * Precondition:
 *
 * - `va` **无需**页面对齐，但必须满足以下条件：
 *    - va >= KMALLOC_BEGIN_VA && va < KMALLOC_END_VA
 *
 * Postcondition:
 *
 * - 分配物理页面并插入`pgdir`页目录的`va`映射项
 * - `va`对应的页表项权限设置为 PTE_RW | PTE_V |
 * - 移除`va`地址原有的所有映射，原映射的物理页的引用计数将 -1
 * - 已分配页面的`pp_ref`引用计数增加
 */
void kernel_passive_alloc(u_reg_t va) {
    struct Page *p = NULL;

    if (va < KMALLOC_BEGIN_VA || va >= KMALLOC_END_VA) {
        panic("kernel_passive_alloc: invalid address: 0x%016lx\n", va);
    }

    panic_on(page_alloc(&p));

    // Postconditon for `page_alloc`: now, p points to the allocated Page

    panic_on(page_insert(kernel_boot_pgdir, 0, p, va, PTE_RW | PTE_GLOBAL));
}