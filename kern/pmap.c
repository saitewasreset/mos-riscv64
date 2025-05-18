#include "error.h"
#include "queue.h"
#include <bitops.h>
#include <env.h>
#include <malta.h>
#include <mmu.h>
#include <pmap.h>
#include <printk.h>

/* These variables are set by mips_detect_memory(ram_low_size); */
static u_long memsize; /* 最大可用的物理地址，由`mips_detect_memory`初始化 */
u_long npage;          /* 最大可用的物理页数，由`mips_detect_memory`初始化 */

Pde *cur_pgdir;

struct Page *pages; /* 描述物理页的结构体的列表，由`mips_vm_init`初始化 */

// 用于`alloc`分配器，指向下一个可用物理内存的虚拟地址（kseg0），由`alloc`初始化
static u_long freemem;
// 存储空闲物理页链表的链表头，由`page_init`初始化
struct Page_list page_free_list;

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
void mips_detect_memory(u_int _memsize) {
    /* Step 1: Initialize memsize. */
    memsize = _memsize;

    /* Step 2: Calculate the corresponding 'npage' value. */
    /* Exercise 2.1: Your code here. */

    npage = memsize >> PGSHIFT;

    printk("Memory size: %lu KiB, number of pages: %lu\n", memsize / 1024,
           npage);
}

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
void *alloc(u_int n, u_int align, int clear) {
    extern char end[];
    u_long alloced_mem;

    /* Initialize `freemem` if this is the first time. The first virtual address
     * that the linker did *not* assign to any kernel code or global variables.
     */
    if (freemem == 0) {
        freemem = (u_long)end; // end
    }

    /* Step 1: Round up `freemem` up to be aligned properly */
    // 查找大于等于`a`的、最近的是`n`的倍数的整数，要求`n`必须是 2 的正整数幂
    // Precondition：align 是 2 的整数幂
    freemem = ROUND(freemem, align);

    /* Step 2: Save current value of `freemem` as allocated chunk. */
    alloced_mem = freemem;

    /* Step 3: Increase `freemem` to record allocation. */
    freemem = freemem + n;

    // Panic if we're out of memory.
    panic_on(PADDR(freemem) >= memsize);

    /* Step 4: Clear allocated chunk if parameter `clear` is set. */
    if (clear) {
        memset((void *)alloced_mem, 0, n);
    }

    /* Step 5: return allocated chunk. */
    return (void *)alloced_mem;
}
/* End of Key Code "alloc" */

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
void mips_vm_init() {
    /* Allocate proper size of physical memory for global array `pages`,
     * for physical memory management. Then, map virtual address `UPAGES` to
     * physical address `pages` allocated before. For consideration of
     * alignment, you should round up the memory size before map. */
    pages = (struct Page *)alloc(npage * sizeof(struct Page), PAGE_SIZE, 1);

    printk("to memory %x for struct Pages.\n", freemem);
    printk("pmap.c:\t mips vm init success\n");
}

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
void page_init(void) {
    /* Step 1: Initialize page_free_list. */
    /* Hint: Use macro `LIST_INIT` defined in include/queue.h. */
    /* Exercise 2.3: Your code here. (1/4) */

    LIST_INIT(&page_free_list);

    /* Step 2: Align `freemem` up to multiple of PAGE_SIZE. */
    /* Exercise 2.3: Your code here. (2/4) */

    freemem = ROUND(freemem, PAGE_SIZE);

    /* Step 3: Mark all memory below `freemem` as used (set `pp_ref` to 1) */
    /* Exercise 2.3: Your code here. (3/4) */

    // 注意：`freemem`指向的是下一处空闲物理内存的**虚拟地址**（kseg0)
    size_t used_page_count = PADDR(freemem) / PAGE_SIZE;

    for (size_t i = 0; i < used_page_count; i++) {
        pages[i].pp_ref = 1;
    }

    /* Step 4: Mark the other memory as free. */
    /* Exercise 2.3: Your code here. (4/4) */

    for (size_t i = used_page_count; i < npage; i++) {
        pages[i].pp_ref = 0;

        LIST_INSERT_HEAD(&page_free_list, &pages[i], pp_link);
    }
}

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
int page_alloc(struct Page **new) {
    /* Step 1: Get a page from free memory. If fails, return the error code.*/
    struct Page *pp;
    /* Exercise 2.4: Your code here. (1/2) */

    // Precondition: `head` must point to a valid linked list
    pp = LIST_FIRST(&page_free_list);

    if (pp == NULL) {
        return -E_NO_MEM;
    }

    // Precondition:
    // - `pp` is a non-NULL pointer to a valid list element
    // - `pp` is already part of the linked list
    // - `pp_link` is the name of the list entry structure within 'elm' that
    // contains 'le_prev' and 'le_next' members.
    // - 'pp->field.le_prev' must point to a valid pointer,
    // this is guaranteed by `LIST_INSERT_HEAD`, which holds even for the first
    // node of the linked list in which case its le_prev points to the header's
    // lh_first field
    LIST_REMOVE(pp, pp_link);

    /* Step 2: Initialize this page with zero.
     * Hint: use `memset`. */
    /* Exercise 2.4: Your code here. (2/2) */
    // 注意，所有访存使用的都是虚拟地址
    u_long page_begin_vaddr = page2kva(pp);

    memset((void *)page_begin_vaddr, 0, PAGE_SIZE);

    *new = pp;
    return 0;
}

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
void page_free(struct Page *pp) {
    assert(pp->pp_ref == 0);
    /* Just insert it into 'page_free_list'. */
    /* Exercise 2.5: Your code here. */

    LIST_INSERT_HEAD(&page_free_list, pp, pp_link);
}

/* 概述：
 *   给定指向页目录的指针`pgdir`，`pgdir_walk`返回指向虚拟地址`va`对应页表条目的指针。
 *
 * Precondition:
 *
 * - `va`无需页对齐
 * - `pgdir`是指向页目录的指针
 * - `ppte`是有效指针（不应为 NULL）
 *
 * Postcondition:
 * - 当`create`为`0`时：
 *   若找到对应页的页表项，将页表条目的虚拟地址存储到*ppte 并返回 0；
 *   否则设置*ppte = NULL 并返回 0
 * - 当`create`为`1`时：
 *   若找到对应页的页表项，将页表条目的虚拟地址存储到*ppte 并返回 0；
 *   否则（即，对应页的二级页表项还未分配）:
 *     - 若内存不足，返回-E_NO_MEM
 *     - 否则分配新的物理页**用于存储页表项**，
 *       将页表条目的虚拟地址存储到*ppte 并返回 0
 *
 * 注意：本函数只为**页表项**分配物理页（若需要），不实际建立虚拟地址到物理地址的映射
 */
static int pgdir_walk(Pde *pgdir, u_long va, int create, Pte **ppte) {
    /* Step 1: Get the corresponding page directory entry. */
    /* Exercise 2.6: Your code here. (1/3) */

    // PDX(va)：给定一个虚拟地址，返回其一级页表偏移量，10 位（0x03FF），高位为
    // 0
    u_long page_directory_entry_idx = PDX(va);
    Pde *page_directory_entry = &pgdir[page_directory_entry_idx];

    /* Step 2: If the corresponding page table is not existent (valid) then:
     *   * If parameter `create` is set, create one. Set the permission bits
     * 'PTE_C_CACHEABLE | PTE_V' for this new page in the page directory. If
     * failed to allocate a new page (out of memory), return the error.
     *   * Otherwise, assign NULL to '*ppte' and return 0.
     */
    /* Exercise 2.6: Your code here. (2/3) */

    u_long page_table_base_addr = 0;

    if ((*page_directory_entry & PTE_V) == 0) {
        if (create == 0) {
            *ppte = 0;
            return 0;
        } else {
            struct Page *new_page = NULL;

            // This does NOT increase the reference count 'pp_ref' of the page -
            // the caller must do these if necessary (either explicitly or via
            // page_insert)
            int ret = page_alloc(&new_page);
            if (ret == 0) {
                new_page->pp_ref++;

                *page_directory_entry =
                    ((page2pa(new_page)) | PTE_C_CACHEABLE | PTE_V);
                page_table_base_addr = page2kva(new_page);
            } else {
                *ppte = NULL;

                return ret;
            }
        }
    } else {
        u_long page_table_base_physical_addr = PTE_ADDR(*page_directory_entry);
        page_table_base_addr = KADDR(page_table_base_physical_addr);
    }

    /* Step 3: Assign the kernel virtual address of the page table entry to
     * '*ppte'. */
    /* Exercise 2.6: Your code here. (3/3) */

    *ppte = &((Pte *)page_table_base_addr)[PTX(va)];

    return 0;
}

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
 * 页表项的低 12 位标志位设置为'perm | PTE_C_CACHEABLE | PTE_V'。
 *
 * 在所有情况下，TLB 中的相关表项（若有），都将被移除，以使得新的映射生效。
 *
 * Precondition：
 *
 * - `pgdir`必须是指向有效页目录结构的指针
 * - `pp`必须指向`pages`数组中的有效物理页
 * - `va`无需按页对齐
 * - `perm`不得包含 PTE_V/PTE_C_CACHEABLE，这些标志将由函数显式设置
 * - `perm`的高20位必须为0
 * - `asid`必须是有效的地址空间标识符
 *
 * Postcondition：
 *
 * - 成功时返回 0
 * - 若无法分配页表，返回-E_NO_MEM
 *
 */
int page_insert(Pde *pgdir, u_int asid, struct Page *pp, u_long va,
                u_int perm) {
    Pte *pte;

    /* Step 1: Get corresponding page table entry. */
    pgdir_walk(pgdir, va, 0, &pte);
    // 20250422 2055：超级地球包分配老婆，想要老婆的去填C-01表格 -OHHHH
    // 20250422 2055：超级地球是头猪！ -saitewasreset
    // 若虚拟地址`va`已经被映射，更新标志位或者删除之前的映射
    if (pte && (*pte & PTE_V)) {
        // 如果之前的映射和要创建的映射不同（具体地，不对应同一个 Page
        // 结构体），移除之前的映射
        if (pa2page(*pte) != pp) {
            page_remove(pgdir, asid, va);
        } else {
            // 若是同一个映射，只更新标志位
            // 为了使得新的标志位生效，需要从 TLB 中移除相关条目！
            tlb_invalidate(asid, va);
            *pte = page2pa(pp) | perm | PTE_C_CACHEABLE | PTE_V;
            return 0;
        }
    }

    /* Step 2: Flush TLB with 'tlb_invalidate'. */
    /* Exercise 2.7: Your code here. (1/3) */

    // 为了使得新的标志位生效，需要从 TLB 中移除相关条目！
    tlb_invalidate(asid, va);

    /* Step 3: Re-get or create the page table entry. */
    /* If failed to create, return the error. */
    /* Exercise 2.7: Your code here. (2/3) */

    int ret = pgdir_walk(pgdir, va, 1, &pte);

    if (ret != 0) {
        return ret;
    }

    /* Step 4: Insert the page to the page table entry with 'perm |
     * PTE_C_CACHEABLE | PTE_V' and increase its 'pp_ref'. */
    /* Exercise 2.7: Your code here. (3/3) */

    *pte = page2pa(pp) | perm | PTE_C_CACHEABLE | PTE_V;

    pp->pp_ref++;
    return 0;
}

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
struct Page *page_lookup(Pde *pgdir, u_long va, Pte **ppte) {
    struct Page *pp;
    Pte *pte;

    /* Step 1: Get the page table entry. */
    pgdir_walk(pgdir, va, 0, &pte);

    /* Hint: Check if the page table entry doesn't exist or is not valid. */
    if (pte == NULL || (*pte & PTE_V) == 0) {
        return NULL;
    }

    /* Step 2: Get the corresponding Page struct. */
    /* Hint: Use function `pa2page`, defined in include/pmap.h . */
    pp = pa2page(*pte);
    if (ppte) {
        *ppte = pte;
    }

    return pp;
}
/* End of Key Code "page_lookup" */

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
void page_decref(struct Page *pp) {
    assert(pp->pp_ref > 0);

    /* If 'pp_ref' reaches to 0, free this page. */
    if (--pp->pp_ref == 0) {
        page_free(pp);
    }
}

/* Lab 2 Key Code "page_remove" */
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
void page_remove(Pde *pgdir, u_int asid, u_long va) {
    Pte *pte;

    /* Step 1: Get the page table entry, and check if the page table entry is
     * valid. */
    struct Page *pp = page_lookup(pgdir, va, &pte);
    if (pp == NULL) {
        return;
    }

    /* Step 2: Decrease reference count on 'pp'. */
    page_decref(pp);

    /* Step 3: Flush TLB. */
    *pte = 0;
    tlb_invalidate(asid, va);
    return;
}
/* End of Key Code "page_remove" */

void physical_memory_manage_check(void) {
    struct Page *pp, *pp0, *pp1, *pp2;
    struct Page_list fl;
    int *temp;

    // should be able to allocate three pages
    pp0 = pp1 = pp2 = 0;
    assert(page_alloc(&pp0) == 0);
    assert(page_alloc(&pp1) == 0);
    assert(page_alloc(&pp2) == 0);

    assert(pp0);
    assert(pp1 && pp1 != pp0);
    assert(pp2 && pp2 != pp1 && pp2 != pp0);

    // temporarily steal the rest of the free pages
    fl = page_free_list;
    // now this page_free list must be empty!!!!
    LIST_INIT(&page_free_list);
    // should be no free memory
    assert(page_alloc(&pp) == -E_NO_MEM);

    temp = (int *)page2kva(pp0);
    // write 1000 to pp0
    *temp = 1000;
    // free pp0
    page_free(pp0);
    printk("The number in address temp is %d\n", *temp);

    // alloc again
    assert(page_alloc(&pp0) == 0);
    assert(pp0);

    // pp0 should not change
    assert(temp == (int *)page2kva(pp0));
    // pp0 should be zero
    assert(*temp == 0);

    page_free_list = fl;
    page_free(pp0);
    page_free(pp1);
    page_free(pp2);
    struct Page_list test_free;
    struct Page *test_pages;
    test_pages = (struct Page *)alloc(10 * sizeof(struct Page), PAGE_SIZE, 1);
    LIST_INIT(&test_free);
    // LIST_FIRST(&test_free) = &test_pages[0];
    int i, j = 0;
    struct Page *p, *q;
    for (i = 9; i >= 0; i--) {
        test_pages[i].pp_ref = i;
        // test_pages[i].pp_link=NULL;
        // printk("0x%x  0x%x\n",&test_pages[i], test_pages[i].pp_link.le_next);
        LIST_INSERT_HEAD(&test_free, &test_pages[i], pp_link);
        // printk("0x%x  0x%x\n",&test_pages[i], test_pages[i].pp_link.le_next);
    }
    p = LIST_FIRST(&test_free);
    int answer1[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    assert(p != NULL);
    while (p != NULL) {
        // printk("%d %d\n",p->pp_ref,answer1[j]);
        assert(p->pp_ref == answer1[j++]);
        // printk("ptr: 0x%x v:
        // %d\n",(p->pp_link).le_next,((p->pp_link).le_next)->pp_ref);
        p = LIST_NEXT(p, pp_link);
    }
    // insert_after test
    int answer2[] = {0, 1, 2, 3, 4, 20, 5, 6, 7, 8, 9};
    q = (struct Page *)alloc(sizeof(struct Page), PAGE_SIZE, 1);
    q->pp_ref = 20;

    // printk("---%d\n",test_pages[4].pp_ref);
    LIST_INSERT_AFTER(&test_pages[4], q, pp_link);
    // printk("---%d\n",LIST_NEXT(&test_pages[4],pp_link)->pp_ref);
    p = LIST_FIRST(&test_free);
    j = 0;
    // printk("into test\n");
    while (p != NULL) {
        //      printk("%d %d\n",p->pp_ref,answer2[j]);
        assert(p->pp_ref == answer2[j++]);
        p = LIST_NEXT(p, pp_link);
    }

    printk("physical_memory_manage_check() succeeded\n");
}

void page_check(void) {
    struct Page *pp, *pp0, *pp1, *pp2;
    struct Page_list fl;

    // should be able to allocate a page for directory
    assert(page_alloc(&pp) == 0);
    Pde *boot_pgdir = (Pde *)page2kva(pp);

    // should be able to allocate three pages
    pp0 = pp1 = pp2 = 0;
    assert(page_alloc(&pp0) == 0);
    assert(page_alloc(&pp1) == 0);
    assert(page_alloc(&pp2) == 0);

    assert(pp0);
    assert(pp1 && pp1 != pp0);
    assert(pp2 && pp2 != pp1 && pp2 != pp0);

    // temporarily steal the rest of the free pages
    fl = page_free_list;
    // now this page_free list must be empty!!!!
    LIST_INIT(&page_free_list);

    // should be no free memory
    assert(page_alloc(&pp) == -E_NO_MEM);

    // there is no free memory, so we can't allocate a page table
    assert(page_insert(boot_pgdir, 0, pp1, 0x0, 0) < 0);

    // free pp0 and try again: pp0 should be used for page table
    page_free(pp0);
    assert(page_insert(boot_pgdir, 0, pp1, 0x0, 0) == 0);
    assert(PTE_FLAGS(boot_pgdir[0]) == (PTE_C_CACHEABLE | PTE_V));
    assert(PTE_ADDR(boot_pgdir[0]) == page2pa(pp0));
    assert(PTE_FLAGS(*(Pte *)page2kva(pp0)) == (PTE_C_CACHEABLE | PTE_V));

    printk("va2pa(boot_pgdir, 0x0) is %x\n", va2pa(boot_pgdir, 0x0));
    printk("page2pa(pp1) is %x\n", page2pa(pp1));
    //  printk("pp1->pp_ref is %d\n",pp1->pp_ref);
    assert(va2pa(boot_pgdir, 0x0) == page2pa(pp1));
    assert(pp1->pp_ref == 1);

    // should be able to map pp2 at PAGE_SIZE because pp0 is already allocated
    // for page table
    assert(page_insert(boot_pgdir, 0, pp2, PAGE_SIZE, 0) == 0);
    assert(va2pa(boot_pgdir, PAGE_SIZE) == page2pa(pp2));
    assert(pp2->pp_ref == 1);

    // should be no free memory
    assert(page_alloc(&pp) == -E_NO_MEM);

    printk("start page_insert\n");
    // should be able to map pp2 at PAGE_SIZE because it's already there
    assert(page_insert(boot_pgdir, 0, pp2, PAGE_SIZE, 0) == 0);
    assert(va2pa(boot_pgdir, PAGE_SIZE) == page2pa(pp2));
    assert(pp2->pp_ref == 1);

    // pp2 should NOT be on the free list
    // could happen in ref counts are handled sloppily in page_insert
    assert(page_alloc(&pp) == -E_NO_MEM);

    // should not be able to map at PDMAP because need free page for page table
    assert(page_insert(boot_pgdir, 0, pp0, PDMAP, 0) < 0);

    // insert pp1 at PAGE_SIZE (replacing pp2)
    assert(page_insert(boot_pgdir, 0, pp1, PAGE_SIZE, 0) == 0);

    // should have pp1 at both 0 and PAGE_SIZE, pp2 nowhere, ...
    assert(va2pa(boot_pgdir, 0x0) == page2pa(pp1));
    assert(va2pa(boot_pgdir, PAGE_SIZE) == page2pa(pp1));
    // ... and ref counts should reflect this
    assert(pp1->pp_ref == 2);
    printk("pp2->pp_ref %d\n", pp2->pp_ref);
    assert(pp2->pp_ref == 0);
    printk("end page_insert\n");

    // pp2 should be returned by page_alloc
    assert(page_alloc(&pp) == 0 && pp == pp2);

    // unmapping pp1 at 0 should keep pp1 at PAGE_SIZE
    page_remove(boot_pgdir, 0, 0x0);
    assert(va2pa(boot_pgdir, 0x0) == ~0);
    assert(va2pa(boot_pgdir, PAGE_SIZE) == page2pa(pp1));
    assert(pp1->pp_ref == 1);
    assert(pp2->pp_ref == 0);

    // unmapping pp1 at PAGE_SIZE should free it
    page_remove(boot_pgdir, 0, PAGE_SIZE);
    assert(va2pa(boot_pgdir, 0x0) == ~0);
    assert(va2pa(boot_pgdir, PAGE_SIZE) == ~0);
    assert(pp1->pp_ref == 0);
    assert(pp2->pp_ref == 0);

    // so it should be returned by page_alloc
    assert(page_alloc(&pp) == 0 && pp == pp1);

    // should be no free memory
    assert(page_alloc(&pp) == -E_NO_MEM);

    // forcibly take pp0 back
    assert(PTE_ADDR(boot_pgdir[0]) == page2pa(pp0));
    boot_pgdir[0] = 0;
    assert(pp0->pp_ref == 1);
    pp0->pp_ref = 0;

    // give free list back
    page_free_list = fl;

    // free the pages we took
    page_free(pp0);
    page_free(pp1);
    page_free(pp2);
    page_free(pa2page(PADDR(boot_pgdir)));

    printk("page_check() succeeded!\n");
}
