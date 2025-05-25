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