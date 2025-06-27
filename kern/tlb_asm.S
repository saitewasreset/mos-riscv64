#include <asm/asm.h>

// 清除TLB中对于地址空间asid，关于虚拟地址va的映射
// 输入：
//     - a0：asid
//     - a1: va
// 输出：
//     无
BEGIN(tlb_invalidate, 16)
    sfence.vma a1, a0
END(tlb_invalidate, 16)

// 清除TLB中对于地址空间asid的所有映射
// 输入：
//     - a0：asid
// 输出：
//     无
BEGIN(tlb_flush_asid, 16)
    sfence.vma zero, a0
END(tlb_flush_asid, 16)

BEGIN(tlb_flush_all, 16)
    sfence.vma
END(tlb_flush_all, 16)

// 设置stap寄存器
// 输入：
//     - a0：asid
//     - a1: 一级页表PPN
// 输出：
//     无
BEGIN(set_satp, 16)
    li      t1, 0xF000000000000000

    csrr    t0, satp

    and     t0, t0, t1

    // 设置asid
    slli    a0, a0, 44
    or      t0, t0, a0

    // 设置一级页表PPN
    or      t0, t0, a1

    csrw    satp, t0
END(set_satp, 16)