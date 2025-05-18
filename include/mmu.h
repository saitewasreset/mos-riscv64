#ifndef _MMU_H_
#define _MMU_H_

/*
 * Part 1.  Page table/directory defines.
 */

// 可用的 ASID 数量
#define NASID 256
// 页大小
#define PAGE_SIZE 4096
// 一个页表项映射的地址范围大小（4KB）
#define PTMAP PAGE_SIZE
// 一个页目录项映射的地址范围大小（4MB）
#define PDMAP (4 * 1024 * 1024) // bytes mapped by a page directory entry
// 获取虚拟页号/二级页表偏移量（需要再取低 10 位）需要右移的位数
#define PGSHIFT 12
// 获取一级页表偏移量需要右移的位数
#define PDSHIFT 22 // log2(PDMAP)
// 给定一个虚拟地址，返回其一级页表偏移量，10 位（0x03FF），高位为 0
#define PDX(va) ((((u_long)(va)) >> PDSHIFT) & 0x03FF)
// 给定一个虚拟地址，返回其二级页表偏移量，10 位（0x03FF），高位为 0
#define PTX(va) ((((u_long)(va)) >> PGSHIFT) & 0x03FF)
// 给定一个页表项（32 位=20 物理页号 +6 硬件标志 +6
// 软件标志），返回其物理页的首地址（物理页号 20 位
// + 12 位 0）
#define PTE_ADDR(pte) (((u_long)(pte)) & ~0xFFF)
// 给定一个页表项（32 位=20 物理页号 +6 硬件标志 +6 软件标志），返回硬件标志（6
// 位）+ 软件标志（6 位），高 20 位为 0
#define PTE_FLAGS(pte) (((u_long)(pte)) & 0xFFF)

// Page number field of an address
// 给定一个物理地址，返回其物理页号（20 位），高位为 0
#define PPN(pa) (((u_long)(pa)) >> PGSHIFT)
// 给定一个虚拟地址，返回其虚拟页号（20 位），高位为 0
#define VPN(va) (((u_long)(va)) >> PGSHIFT)

// Page Table/Directory Entry flags

#define PTE_HARDFLAG_SHIFT 6

// TLB EntryLo and Memory Page Table Entry Bit Structure Diagram.
// entrylo.value == pte.value >> 6
/*
 * +----------------------------------------------------------------+
 * |                     TLB EntryLo Structure                      |
 * +-----------+---------------------------------------+------------+
 * |3 3 2 2 2 2|2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1 0 0 0 0|0 0 0 0 0 0 |
 * |1 0 9 8 7 6|5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6|5 4 3 2 1 0 |
 * +-----------+---------------------------------------+------------+------------+
 * | Reserved  |         Physical Frame Number         |  Hardware  |  Software
 * | |Zero Filled|                20 bits                |    Flag    |    Flag
 * |
 * +-----------+---------------------------------------+------------+------------+
 *             |3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1|1 1 0 0 0 0 |0 0 0 0 0 0
 * | |1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2|1 0 9 8 7 6 |5 4 3 2 1 0 | | | C D
 * V G |            |
 *             +---------------------------------------+------------+------------+
 *             |                Memory Page Table Entry Structure |
 *             +-----------------------------------------------------------------+
 */

// 下列 PTE_*宏用于从内存中的页表项（包含软件标志）获取相应位
// 使用时，应将 Pde/Pte 类型的值与 PTE_*宏进行逻辑与操作

// Global bit. When the G bit in a TLB entry is set, that TLB entry will match
// solely on the VPN field, regardless of whether the TLB entry’s ASID field
// matches the value in EntryHi.
// 硬件标志位：全局位，当 TLB 表项中 G 被设置时，对于该项，TLB 匹配时将忽略
// ASID，只依据虚拟页号匹配 相当于将该页映射到所有地址空间中
#define PTE_G (0x0001 << PTE_HARDFLAG_SHIFT)

// Valid bit. If 0 any address matching this entry will cause a tlb miss
// exception (TLBL/TLBS).
// 硬件标志位：有效位，若为 0，所有匹配到该项的访存请求都会导致 TLB
// 异常（读取：TLBL，写入：TLBS）
#define PTE_V (0x0002 << PTE_HARDFLAG_SHIFT)

// Dirty bit, but really a write-enable bit. 1 to allow writes, 0 and any store
// using this translation will cause a tlb mod exception (TLB Mod).
// 硬件标志位：脏位（允许写入位），若为 0，任何向该页的写入都会导致 Mod 异常
#define PTE_D (0x0004 << PTE_HARDFLAG_SHIFT)

// Cache Coherency Attributes bit.
#define PTE_C_CACHEABLE (0x0018 << PTE_HARDFLAG_SHIFT)
#define PTE_C_UNCACHEABLE (0x0010 << PTE_HARDFLAG_SHIFT)

// Copy On Write. Reserved for software, used by fork.
// 软件标志位：写时复制，页表项第 0 位
#define PTE_COW 0x0001

// Shared memmory. Reserved for software, used by fork.
// 软件标志位：共享页，页表项第 1 位
#define PTE_LIBRARY 0x0002

// 软件标志位：脏位，页表项第 2 位
// #define PTE_DIRTY 0x0004 在fs/serv.h中定义

// Memory segments (32-bit kernel mode addresses)
// 用于获得各个内存区域起始地址的宏
#define KUSEG 0x00000000U
#define KSEG0 0x80000000U
#define KSEG1 0xA0000000U
#define KSEG2 0xC0000000U

/*
 * Part 2.  Our conventions.
 */

/*
 o     4G ----------->  +----------------------------+------------0x100000000
 o                      |       ...                  |  kseg2
 o      KSEG2    -----> +----------------------------+------------0xc000 0000
 o                      |          Devices           |  kseg1
 o      KSEG1    -----> +----------------------------+------------0xa000 0000
 o                      |      Invalid Memory        |   /|\
 o                      +----------------------------+----|-------Physical
 Memory Max o                      |       ...                  |  kseg0 o
 KSTACKTOP-----> +----------------------------+----|-------0x8040 0000-------end
 o                      |       Kernel Stack         |    | KSTKSIZE /|\ o
 +----------------------------+----|------                | o |       Kernel
 Text          |    |                    PDMAP o      KERNBASE ----->
 +----------------------------+----|-------0x8002 0000    | o |      Exception
 Entry       |   \|/                    \|/ o      ULIM     ----->
 +----------------------------+------------0x8000 0000-------
 o                      |         User VPT           |     PDMAP /|\ o      UVPT
 -----> +----------------------------+------------0x7fc0 0000    | o | pages |
 PDMAP                 | o      UPAGES   ----->
 +----------------------------+------------0x7f80 0000    | o |           envs
 |     PDMAP                 | o  UTOP,UENVS   ----->
 +----------------------------+------------0x7f40 0000    | o  UXSTACKTOP -/ |
 user exception stack   |     PTMAP                 | o
 +----------------------------+------------0x7f3f f000    | o | |     PTMAP | o
 USTACKTOP ----> +----------------------------+------------0x7f3f e000    | o |
 normal user stack      |     PTMAP                 | o
 +----------------------------+------------0x7f3f d000    | a | | | a
 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~                           | a . . | a . . kuseg
 a                      .                            . | a
 |~~~~~~~~~~~~~~~~~~~~~~~~~~~~|                           | a | | | o UTEXT
 -----> +----------------------------+------------0x0040 0000    | o | reserved
 for COW      |     PTMAP                 | o       UCOW    ----->
 +----------------------------+------------0x003f f000    | o |   reversed for
 temporary   |     PTMAP                 | o       UTEMP   ----->
 +----------------------------+------------0x003f e000    | o |       invalid
 memory       |                          \|/ a     0 ------------>
 +----------------------------+ ----------------------------
 o
*/

// 内核.text 节被加载到的位置:0x8002 0000
#define KERNBASE 0x80020000

// 内核栈开始的位置：0x8040 0000
#define KSTACKTOP (ULIM + PDMAP)
// 用户空间顶部：0x8000 0000
#define ULIM 0x80000000

#define UVPT (ULIM - PDMAP)
#define UPAGES (UVPT - PDMAP)
#define UENVS (UPAGES - PDMAP)

#define UTOP UENVS
#define UXSTACKTOP UTOP

#define USTACKTOP (UTOP - 2 * PTMAP)
#define UTEXT PDMAP
#define UCOW (UTEXT - PTMAP)
#define UTEMP (UCOW - PTMAP)

#ifndef __ASSEMBLER__

/*
 * Part 3.  Our helper functions.
 */
#include <error.h>
#include <string.h>
#include <types.h>

// 最大可用的物理页数，在`pmap.c`中定义，由`mips_detect_memory`初始化
extern u_long npage;

// 表示一个页目录表项，32 位，实际为 unsigned long
typedef u_long Pde;
// 表示一个页表项，32 位，实际为 unsigned long
typedef u_long Pte;

// 将 kseg0 中的虚拟地址转化为物理地址
// Precondition：传入 kseg0 范围内的虚拟地址 (0x8000 0000 -- 0xA000 0000)
// Panic：若输入的虚拟地址位于用户空间（< ULIM = 0x8000 0000）
#define PADDR(kva)                                                             \
    ({                                                                         \
        u_long _a = (u_long)(kva);                                             \
        if (_a < ULIM)                                                         \
            panic("PADDR called with invalid kva %08lx", _a);                  \
        _a - ULIM;                                                             \
    })

// 将物理地址转化为 kseg0 中的虚拟地址
// Precondition：
// - 传入的地址在物理内存空间之内（小于总计物理内存容量）
// - 传入的地址 < 512MB
// Panics：
// - 若输入的物理地址超出物理内存空间
#define KADDR(pa)                                                              \
    ({                                                                         \
        u_long _ppn = PPN(pa);                                                 \
        if (_ppn >= npage) {                                                   \
            panic("KADDR called with invalid pa %08lx", (u_long)pa);           \
        }                                                                      \
        (pa) + ULIM;                                                           \
    })

// 断言表达式 x 的结果为真
// Panic：若表达式 x 的结果为假
#define assert(x)                                                              \
    do {                                                                       \
        if (!(x)) {                                                            \
            panic("assertion failed: %s", #x);                                 \
        }                                                                      \
    } while (0)

// 判断指针是否指向用户空间的地址
// 若指向用户空间，返回该指针；否则，返回指向 ULIM（0x8000 0000）的指针
// 注：typeof 是 GNU C 的拓展，用于表示某个变量的类型
// typeof((_p)) __m_p = (_p);
// 定义了一个变量__m_p，其类型为 typeof((_p))，即_p 的类型 其值为_p 的值
#define TRUP(_p)                                                               \
    ({                                                                         \
        typeof((_p)) __m_p = (_p);                                             \
        (u_int) __m_p > ULIM ? (typeof(_p))ULIM : __m_p;                       \
    })

/*
 * 概述：
 *
 * 根据传入的 entryhi 值查找并清除对应的 TLB 条目
 * 若存在匹配项，则通过写入零值使其无效；若不存在，则直接返回。
 *
 * Preconditon：
 *
 * 传入的 entryhi 需包含合法的 VPN2 和 ASID，以正确匹配 TLB 条目。
 *
 * Postcondition：
 *
 * - 若存在匹配条目，该条目被无效化（EntryLo0、EntryLo1 清零）
 * - 若无匹配条目，TLB 内容保持不变。
 *
 * 注意：函数将保存并恢复EntryHi，无需调用者处理
 */
extern void tlb_out(u_int entryhi);
/* 概述:
 *
 * 使ASID对应的虚拟地址空间中映射虚拟地址 `va` 的 TLB
 * 条目失效。
 *
 * 具体的，该页和相邻页的映射都将从TLB中移除。
 *
 * Preconditon:
 *
 * - `va` 无需页对齐
 *
 * Postconditon:
 *
 * 如果 TLB 中存在特定条目，则通过将对应的 TLB条目写为零来使其失效；
 *
 * 否则，不会发生任何操作。
 *
 * 由于 4Kc TLB条目的结构，与该虚拟地址对应的页 **以及** 相邻页的映射将被移除。
 */
void tlb_invalidate(u_int asid, u_long va);
#endif //!__ASSEMBLER__
#endif // !_MMU_H_
