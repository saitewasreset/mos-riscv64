#ifndef _MMU_H_
#define _MMU_H_

/*
 * Part 1.  Page table/directory defines.
 */

#include <bitops.h>
#include <virt.h>

#define HIGH_ADDR_IMM 0xFFFFFFC000000000ULL
#define LOW_ADDR_IMM 0x80000000ULL
#define LOAD_ADDR_IMM 0x80200000ULL
#define BASE_ADDR_IMM 0xFFFFFFC000200000ULL
#define KERNEL_END_ADDR_BEFORE_PAGING_IMM 0x81000000ULL

#define KMALLOC_BEGIN_VA 0xFFFFFFC0A0000000ULL
#define KMALLOC_END_VA 0xFFFFFFC0E0000000ULL

#define KMALLOC_HEAP_SIZE (KMALLOC_END_VA - KMALLOC_BEGIN_VA)

#define HIGH_ADDR_OFFSET ((HIGH_ADDR_IMM) - (LOW_ADDR_IMM))

// 可用的 ASID 数量
#define NASID 256
// 页大小
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12
// 一个页表项映射的地址范围大小（4KB）
#define PTMAP PAGE_SIZE

// 所有标志位：10位，2 软件标志 + 8 硬件标志
#define FLAG_SHIFT 10

// 27位掩码，用于获取有效的VPN、PPN
#define VPN_MASK GENMASK(26, 0)

// 一个一级页表项映射的地址范围大小（1GB）
#define P1MAP (512 * 512 * 4 * 1024)
// 一个二级页表项映射的地址范围大小（2MB）
#define P2MAP (512 * 4 * 1024)
// 一个三级页表项映射的地址范围大小（4KB）
#define P3MAP (4 * 1024)

// 获取一级页表偏移量（需要再取低 9 位）需要右移的位数
#define P1SHIFT 30
// 获取二级页表偏移量（需要再取低 9 位）需要右移的位数
#define P2SHIFT 21
// 获取三级页表偏移量（需要再取低 9 位）需要右移的位数
#define P3SHIFT 12

// 给定一个虚拟地址，返回其一级页表偏移量，9 位，高位为 0
#define P1X(va) ((((u_reg_t)(va) & GENMASK(38, 0)) >> P1SHIFT) & GENMASK(8, 0))
// 给定一个虚拟地址，返回其二级页表偏移量，9 位，高位为 0
#define P2X(va) ((((u_reg_t)(va) & GENMASK(38, 0)) >> P2SHIFT) & GENMASK(8, 0))
// 给定一个虚拟地址，返回其三级页表偏移量，9 位，高位为 0
#define P3X(va) ((((u_reg_t)(va) & GENMASK(38, 0)) >> P3SHIFT) & GENMASK(8, 0))

// 给定一个物理地址，返回其物理页号（27 位），高位为 0
#define PPN(pa) (((u_reg_t)(pa)) >> PAGE_SHIFT)
// 给定一个虚拟地址，返回其虚拟页号（20 位），高位为 0
#define VPN(va) (((u_reg_t)(va) & GENMASK(38, 0)) >> PAGE_SHIFT)

// 给定一个页表项（64位=10保留 + 44 物理页号 + 2 软件标志 + 8 硬件标志），
// 返回其物理页的首地址（物理页号 44 位 + 12 位 0）
#define PTE_ADDR(pte)                                                          \
    ((((u_reg_t)(pte) >> FLAG_SHIFT) & GENMASK(43, 0)) << PAGE_SHIFT)
// 给定一个页表项（64位=10保留 + 44 物理页号 + 2 软件标志 + 8 硬件标志），
// 返回其标志位（2 软件标志 + 8 硬件标志）
#define PTE_FLAGS(pte) (((u_reg_t)(pte)) & GENMASK(9, 0))

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

// 硬件标志位：有效位，若为 0，所有匹配到该项的访存请求都会导致 TLB
// 异常
#define PTE_V 0x0001U

// 硬件标志位：读权限位
#define PTE_R 0x0002U

// 硬件标志位：写权限位
#define PTE_W 0x0004U

// 硬件标志位：执行权限位
#define PTE_X 0x0008U

#define PTE_NON_LEAF 0U
#define PTE_RO (PTE_R)
#define PTE_RW (PTE_R | PTE_W)
#define PTE_XO (PTE_X)
#define PTE_RX (PTE_R | PTE_X)
#define PTE_RWX (PTE_R | PTE_W | PTE_X)

#define PTE_IS_NON_LEAF(pte) ((((u_reg_t)(pte)) & GENMASK(3, 1)) == 0)

// 硬件标志位：用户态可访问
#define PTE_USER 0x0010U

// 硬件标志位：全局位，当 TLB 表项中 G 被设置时，对于该项，TLB 匹配时将忽略
// ASID，只依据虚拟页号匹配 相当于将该页映射到所有地址空间中
#define PTE_GLOBAL 0x0020U

// 硬件标志位：访问位，硬件会在访问时将其置 1。
// 内核可以定期清除并检查此位，用于页面置换算法。
#define PTE_ACCESS 0x0040U

// 硬件标志位：脏位，硬件会在写入时将其置 1。
#define PTE_DIRTY 0x0080U

#define PTE_SOFTFLAG_SHIFT 8

// 软件标志位：PTE_COW

#define PTE_COW (1U << 9)

// 软件标志位：PTE_LIBRARY

#define PTE_LIBRARY (1U << 8)

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

#define KSTACKTOP 0xFFFFFFC001000000ULL

#define KSTACKBOTTOM ((KSTACKTOP) - P3MAP)

// 用户空间顶部：0x003F 0000 0000 - 252GB
#define ULIM 0x003F00000000ULL

// 用户空间只读访问自身页表：[0x003E C000 0000, 0x003F 0000 0000) 1GB
#define UVPT (ULIM - P1MAP)
// 用户空间只读访问物理页结构体：[0x003E 8000 0000, 0x003E C000 0000) 1GB
#define UPAGES (UVPT - P1MAP)
// 用户空间只读访问Env块：[0x003E 4000 0000, 0x003E 8000 0000) 1GB
#define UENVS (UPAGES - P1MAP)

// 用户可用空间顶部：0x003E 4000 0000
#define UTOP UENVS

// 用户异常栈：[0x003E 3FFF F000, 0x003E 4000 0000) 4KB
#define UXSTACKTOP UTOP

// 用户栈：0x003E 3FFF E000
#define USTACKTOP (UTOP - 2 * P3MAP)

// 代码区：0x0040 0000
#define UTEXT (2 * P2MAP)
// 用户COW异常处理的临时页：[0x003F F000, 0x0040 0000) 4KB
#define UCOW (UTEXT - P3MAP)
// 0x003F E000
#define UTEMP (UCOW - P3MAP)

#ifndef __ASSEMBLER__

/*
 * Part 3.  Our helper functions.
 */
#include <error.h>
#include <string.h>
#include <types.h>

// 最大可用的物理页数，在`pmap.c`中定义，由`mips_detect_memory`初始化
extern u_reg_t npage;

// 表示一个页表项，64 位
typedef u_reg_t Pte;

// 将直接映射区域[0xFFFFFFC000000000, 0xFFFFFFC040000000)
// 的虚拟地址转化为DRAM偏移地址
// Precondition：传入直接映射区域范围内的
#define DRAMADDR(kva) ((u_reg_t)(kva) - HIGH_ADDR_IMM)

// 将直接映射区域[0xFFFFFFC000000000, 0xFFFFFFC040000000)
// 的虚拟地址转化为物理地址（从0x80000000开始）
// Precondition：传入直接映射区域范围内的
#define PADDR(kva) ((u_reg_t)(kva) - HIGH_ADDR_OFFSET)

// 将DRAM偏移地址地址转化直接映射区域[0xFFFFFFC000000000, 0xFFFFFFC040000000)
// 的虚拟地址
// Precondition：
// - 传入的地址在物理内存空间之内（小于总计物理内存容量）
// - 传入的地址 < 1GB
// Panics：
// - 若输入的物理地址超出物理内存空间
#define D2KADDR(pa) ((pa) + HIGH_ADDR_IMM)

// 将物理地址地址转化直接映射区域[0xFFFFFFC000000000, 0xFFFFFFC040000000)
// 的虚拟地址
// Precondition：
// - 传入的地址在物理内存空间之内（小于总计物理内存容量）
// - 传入的地址 < 1GB
// Panics：
// - 若输入的物理地址超出物理内存空间
#define P2KADDR(pa) ((pa) + HIGH_ADDR_OFFSET)

// 断言表达式 x 的结果为真
// Panic：若表达式 x 的结果为假
#define assert(x)                                                              \
    do {                                                                       \
        if (!(x)) {                                                            \
            panic("assertion failed: %s", #x);                                 \
        }                                                                      \
    } while (0)

// 断言表达式 x 和 y 的结果相等
// Panic：若表达式 x == y 的结果为假
#define assert_eq(x, y)                                                        \
    do {                                                                       \
        u_reg_t left = (u_reg_t)(x);                                           \
        u_reg_t right = (u_reg_t)(y);                                          \
        if (!(left == right)) {                                                \
            panic("assertion %s == %s failed: left = %016lx right = %016lx",   \
                  #x, #y, left, right);                                        \
        }                                                                      \
    } while (0)

// 判断指针是否指向用户空间的地址
// 若指向用户空间，返回该指针；否则，返回指向 ULIM的指针
// 注：typeof 是 GNU C 的拓展，用于表示某个变量的类型
// typeof((_p)) __m_p = (_p);
// 定义了一个变量__m_p，其类型为 typeof((_p))，即_p 的类型 其值为_p 的值
#define TRUP(_p)                                                               \
    ({                                                                         \
        typeof((_p)) __m_p = (_p);                                             \
        (u_reg_t) __m_p > ULIM ? (typeof(_p))ULIM : __m_p;                     \
    })

/*
 * 概述：
 *
 * 清除TLB中对于地址空间asid，关于虚拟地址va的映射
 *
 * Preconditon：
 *
 *
 * Postcondition：
 *
 * - 若存在匹配条目，该条目被无效化
 * - 若无匹配条目，TLB 内容保持不变。
 *
 */
extern void tlb_invalidate(uint16_t asid, uint16_t va);
/* 概述:
 *
 * 使ASID对应的虚拟地址空间中的 TLB 条目失效。
 *
 * 具体的，该页和相邻页的映射都将从TLB中移除。
 *
 * Preconditon:
 *
 * - `va` 无需页对齐
 *
 * Postconditon:
 *
 * 如果 TLB 中存在特定条目，则通过sfence.vma使其失效；
 *
 * 否则，不会发生任何操作。
 */
extern void tlb_flush_asid(u_int asid);

extern void tlb_flush_all();

#endif //!__ASSEMBLER__
#endif // !_MMU_H_
