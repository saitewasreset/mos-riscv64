#ifndef _PMAP_H_
#define _PMAP_H_

#include "bitops.h"
#include <mmu.h>
#include <printk.h>
#include <queue.h>
#include <types.h>

extern Pte *cur_pgdir;

extern Pte *kernel_boot_pgdir;

LIST_HEAD(Page_list, Page);
typedef LIST_ENTRY(Page) Page_LIST_entry_t;

struct Page {
    Page_LIST_entry_t pp_link; /* free list link */

    // Ref is the count of pointers (usually in page table entries)
    // to this page.  This only holds for pages allocated using
    // page_alloc.  Pages allocated at boot time using pmap.c's "alloc"
    // do not have valid reference count fields.

    u_short pp_ref;
};

// 描述物理页的结构体的列表，在`pmap.c`中定义，由`mips_vm_init`初始化
extern struct Page *pages;
// 存储空闲物理页链表的链表头，在`pmap.c`中定义，由`page_init`初始化
extern struct Page_list page_free_list;

// 返回物理页`pp`的物理页号
// 注意：DRAM从0x80000000（物理地址）开始映射
static inline u_reg_t page2ppn(struct Page *pp) {
    return (u_reg_t)(pp - pages) + PPN(0x80000000ULL);
}

// 返回物理页（Page 结构体的指针）`pp`的物理地址（低 12 位为
// 0）
static inline u_reg_t page2pa(struct Page *pp) {
    return page2ppn(pp) << PAGE_SHIFT;
}

/*
 * 概述：
 *
 * 返回物理地址`pa`对应的物理页（Page结构体的指针）
 *
 * Precondition:
 * - 传入的物理地址`pa`必须小于可用的物理内存大小
 *
 * 注意：传入的物理地址的低12位将被忽略
 */
static inline struct Page *pa2page(u_reg_t pa) {

    if (pa < LOW_ADDR_IMM) {
        panic("pa2page called with invalid pa: 0x%016x", pa);
    }

    pa -= LOW_ADDR_IMM;

    if (PPN(pa) >= npage) {
        panic("pa2page called with invalid pa: 0x%016x", pa);
    }
    return &pages[PPN(pa)];
}

// 返回物理页（Page 结构体的指针）`pp`的虚拟地址 (kseg0)
static inline u_long page2kva(struct Page *pp) { return P2KADDR(page2pa(pp)); }

/*
 * 概述：
 *
 * 查找页表，将虚拟地址`va`转化为物理地址
 *
 * Precondition:
 *
 * - `pgdir` 必须指向有效的页目录
 *
 * Postcondition：
 *
 * - 若虚拟地址`va`有对应的有效页表项，返回`va`映射到的物理地址
 * - 否则，返回0xFFFF FFFF FFFF FFFF
 *
 * 注意：传入的虚拟地址的低12位将被忽略，故可直接传入合法的页表项
 */
static inline u_reg_t va2pa(Pte *pgdir, u_long va) {
    Pte *p;

    // 在一级页表中依据一级页表偏移量查找
    Pte p1Entry = pgdir[P1X(va)];
    if (!(p1Entry & PTE_V)) {
        return ~0ULL;
    }

    if (PTE_IS_NON_LEAF(p1Entry) == 0) {
        // 一级巨页
        return PTE_ADDR(p1Entry) | (va & GENMASK(29, 0));
    }

    // 一级页表表项存储的是二级页表的**物理地址**
    // 需转化为虚拟地址访问
    // PTE_ADDR 返回页表项对应物理页的首地址（物理页号 44 位 + 12 位 0）
    // KADDR 将物理地址转化为 kseg0 中的虚拟地址
    p = (Pte *)P2KADDR(PTE_ADDR(p1Entry));

    Pte p2Entry = p[P2X(va)];

    if (!(p2Entry & PTE_V)) {
        return ~0ULL;
    }

    if (PTE_IS_NON_LEAF(p2Entry) == 0) {
        // 二级巨页
        return PTE_ADDR(p1Entry) | (va & GENMASK(20, 0));
    }

    p = (Pte *)P2KADDR(PTE_ADDR(p2Entry));

    Pte p3Entry = p[P3X(va)];

    if (!(p3Entry & PTE_V)) {
        return ~0ULL;
    }

    return PTE_ADDR(p3Entry) | (va & GENMASK(11, 0));
}

/* 概述：
 *
 *   使用入参`_memsize`（来自 bootloader）设置`memsize`、`npage`
 *
 *   Use '_memsize' from bootloader to initialize 'memsize' and
 *   calculate the corresponding 'npage' value.
 *
 * 副作用：
 *
 * - 设置全局变量 memsize：最大可用的物理地址
 * - 设置全局变量 npage：最大可用的物理页数
 * - 输出日志：Memory size: %lu KiB, number of pages: %lu\n
 */
void riscv64_detect_memory();

/* 概述：
 *
 *   分配存储物理页信息的`Page`结构体数组
 *
 * Precondition：
 *
 * - `npage`的值为物理页数（由由`mips_detect_memory`初始化）
 *
 * 副作用：
 *
 * - 设置全局变量 pages：存储物理页信息的`Page`结构体数组
 * - 输出日志："to memory %x for struct
 * Pages.\n"（对应`Page`结构体数组顶端的虚拟地址（kseg0）
 * - 输出日志："pmap.c:\t mips vm init success\n"
 */
void riscv64_vm_init(void);

/*
 * 概述：
 *
 *   初始化物理页管理：初始化空闲物理页链表、标注占用的物理页，物理页将通过引用计数管理
 *
 *   具体地，将内核映像占用的物理页、
 *   之前使用 alloc 分配/部分分配的物理页
 *  （全局变量 freemem 的值，向上对齐到页大小），标注为占用（引用计数为 1）
 *   并将剩余未占用的物理页插入到空闲物理页链表。
 *
 * Precondition：
 * - 全局变量`freemem`必须正确指向下一处空闲物理内存的虚拟地址（kseg0)
 *
 * 副作用：
 *
 * - 根据占用情况修改`pages`中的物理页结构体
 * - 设置全局变量`freemem`：原值向上对齐到页面大小
 * - 设置全局变量`page_free_list`：为合法的空闲物理页链表头
 *
 */
void page_init(void);

/* 概述：
 *
 *   分配`n`字节物理内存（对齐到`align`字节），若`clear`为真，将分配的内存填 0
 *   仅在设置页式虚拟内存管理的过程中（`mips_vm_init`）使用本分配器。
 *
 * Precondition：
 *
 * - `align`必须是 2 的整数幂
 * - 全局符号`end`正确指向内核栈顶端（即，初始时可用物理内存的起始处）：0x8040
 * 0000
 * - 全局变量 freemem 正确指向下一处可用的物理内存
 * （在调用`page_init`初始化页式内存管理后，该条件无法得到满足，此时不能再使用本分配器）
 *
 * Postcondition：
 *
 * - 返回指向分配的内存的指针（位于 kseg0 中）
 *
 * Panics：
 *
 * - 剩余内存不足
 *
 * 副作用：
 *
 * - 设置全局变量 freemem：下一处可用物理内存的虚拟地址（kseg0)
 */
void *alloc(u_int n, u_int align, int clear);

/*
 * 概述：
 *
 *   从空闲物理内存中分配一个物理页，并将该页内容清零。
 *
 *   具体地，移除空闲物理页链表的首元素，
 *   并将该元素的地址（struct Page *）写入到调用者指定的位置。
 *
 * Precondition：
 * - 参数`new`必须是指向有效`struct Page*`内存位置的**非空指针**
 * - 全局变量`page_free_list`必须是合法的空闲物理页链表的首节点
 *
 * Postcondition：
 * -
 * 若分配新页失败（内存不足，即，无空闲页），返回-E_NO_MEM，不修改调用者指定的位置。
 * -  否则，将分配的'Page'地址设置到调用者指定的位置，并返回 0。
 *
 * 注意：
 *   本函数不会增加物理页的引用计数'pp_ref'——必要时
 *   必须由调用者自行处理（显式操作或通过 page_insert）
 *
 */
int page_alloc(struct Page **pp);

/* 概述：
 *   释放页面'pp'并将其标记为空闲。
 *
 * Precondition:
 *
 * - `pp`必须指向`pages`中的一个有效页面。
 * - 'pp->pp_ref'的值为'0'。
 *
 * Panics:
 *
 * - `pp->pp_ref`的值不为 0
 */
void page_free(struct Page *pp);

/* 概述：
 *   减少`pp`对应的物理页的引用计数，
 *   若引用计数为 0，将该页面插入空闲物理页链表。
 *
 *   注意，调用此函数后无需再调用`page_free`!
 *
 * Precondition：
 *
 * - `pp`必须指向`pages`数组中的有效物理页
 * - `pp`的引用计数必须大于 0
 *
 * Panics：
 *
 * - `pp`的引用计数（无符号整数）为 0
 *
 */
void page_decref(struct Page *pp);

/* 概述：
 *   （含 TLB 操作）在虚拟地址空间`asid`中，
 *    将物理页'pp'映射到虚拟地址'va'，并按需增加物理页的引用计数。
 *
 *   - 如果`va`已有效映射了指定页，仅更新标志位，引用计数不变。
 *   - 如果`va`已有效映射到其他页，移除原映射并建立新映射，
 *     旧页的引用计数 -1，新页的引用计数 +1。
 *   - 如果`va`未有效映射，创建其二级页表项（若需要），
 *     分配物理页进行映射，新页的引用计数 +1。
 *
 * 页表项的低 12 位标志位设置为'perm | PTE_V'。
 *
 * 在所有情况下，TLB 中的相关表项（若有），都将被移除，以使得新的映射生效。
 *
 * Precondition：
 *
 * - `pgdir`必须是指向有效页目录结构的指针
 * - `pp`必须指向`pages`数组中的有效物理页
 * - `va`无需按页对齐
 * - `perm`不得包含 PTE_V，这些标志将由函数显式设置
 * - `perm`的高20位必须为0
 * - `asid`必须是有效的地址空间标识符
 *
 * Postcondition：
 *
 * - 成功时返回 0
 * - 若无法分配页表，返回-E_NO_MEM
 *
 */
int page_insert(Pte *pgdir, uint16_t asid, struct Page *pp, u_reg_t va,
                uint32_t perm);

/* 概述：
 *   查找虚拟地址`va`映射到的物理页（Page 结构体），返回指向该结构体的指针，
 *   若`ppte` != NULL，将对应的页表项地址存储到*ppte 中。
 *
 * Precondition：
 *
 * - `pgdir`必须指向有效的页目录
 * - `va`无需按页对齐
 * - `ppte`必须指向合法的用于存储`Pte *`的内存区域或者NULL
 *
 * Postcondition：
 *
 * - 若`va`不存在有效映射（不存在映射或 PTE_V = 0）
 *   - 返回 NULL
 *   - 不修改 ppte 指向的内存区域
 * - 若`va`存在有效映射
 *   - 返回指向物理页结构体的指针
 *   - 若`ppte` != NULL，将对应页表项的地址写入 ppte 指向的内存区域
 *
 */
struct Page *page_lookup(Pte *pgdir, u_long va, Pte **ppte);

/* 概述：
 *   （含 TLB 操作）在虚拟地址空间`asid`中，移除虚拟地址`va`的映射`。
 *
 *   - 如果`va`已有效映射了物理页，移除该映射，移除 TLB 中的相关表项，引用计数
 * -1。
 *   - 如果`va`未有效映射物理页，不执行任何操作。
 *
 *   注意：如果引用计数减少后等于0，则该物理页将被加入空闲链表！
 *
 * Precondition：
 *
 * - `pgdir`必须是指向有效页目录结构的指针
 * - `va`无需按页对齐
 * - `asid`必须是有效的地址空间标识符
 *
 */
void page_remove(Pte *pgdir, u_int asid, u_long va);

void map_mem(Pte *pgdir, u_reg_t va, u_reg_t pa, size_t len, uint32_t perm);
void unmap_mem(Pte *pgdir, u_reg_t va, size_t len);

void kmap(u_reg_t va, u_reg_t pa, size_t len, uint32_t perm);
void kunmap(u_reg_t va, size_t len);

extern struct Page *pages;

void physical_memory_manage_check(void);
void page_check(void);

void passive_alloc(u_reg_t va, Pte *pgdir, uint16_t asid);

void kernel_passive_alloc(u_reg_t va);

#endif /* _PMAP_H_ */
