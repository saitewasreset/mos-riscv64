#include <asm/asm.h>

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
LEAF(tlb_out)
.set noreorder
	// 保存原有CP0_ENTRYHI的值：VPN2、**ASID**
	mfc0    t0, CP0_ENTRYHI
	// 用参数entryhi覆盖CP0_ENTRYHI的值
	mtc0    a0, CP0_ENTRYHI
	nop
	/* Step 1: Use 'tlbp' to probe TLB entry */
	/* Exercise 2.8: Your code here. (1/2) */
	// 使用CP0_ENTRYHI中的VPN2、ASID匹配TLB条目
	// 若找到，将其下标保存到CP0_INDEX
	// 若未找到，将CP0_INDEX的最高位置1（使其<0）
	tlbp
	nop
	/* Step 2: Fetch the probe result from CP0.Index */
	// 将CP0_INDEX的值保存到t1中
	mfc0    t1, CP0_INDEX
.set reorder
	// 判断有无匹配的条目
	bltz    t1, NO_SUCH_ENTRY
.set noreorder
	// 若有，清除条目对应的映射（事实上，VPN2对应的两个映射都会被清除）
	mtc0    zero, CP0_ENTRYHI
	mtc0    zero, CP0_ENTRYLO0
	mtc0    zero, CP0_ENTRYLO1
	nop
	/* Step 3: Use 'tlbwi' to write CP0.EntryHi/Lo into TLB at CP0.Index  */
	/* Exercise 2.8: Your code here. (2/2) */
	tlbwi

.set reorder

NO_SUCH_ENTRY:
	// 无论是否有匹配的条目，以下指令都将执行
	// 还原原有的CP0_ENTRYHI的值（主要目的是还原ASID）
	mtc0    t0, CP0_ENTRYHI
	j       ra
END(tlb_out)

/*
* 概述：
*
* 处理 TLB 重填异常，通过页表查询获取缺失的页表项并加载到 TLB
* 使用 tlbwr 指令将 EntryHi 和 EntryLo0/1 写入随机的 TLB 条目
*
* Postcondition：
*
* - EntryLo0 和 EntryLo1 被设置为对应的页表项
* - 通过 tlbwr 指令将 EntryHi/Lo0/Lo1 写入随机 TLB 条目
*
* 注意：函数会保存/恢复返回地址及栈指针，调用者无需处理寄存器保存
*/
NESTED(do_tlb_refill, 24, zero)
	mfc0    a1, CP0_BADVADDR
	mfc0    a2, CP0_ENTRYHI
	andi    a2, a2, 0xff /* ASID is stored in the lower 8 bits of CP0_ENTRYHI */
.globl do_tlb_refill_call;
do_tlb_refill_call:
	addi    sp, sp, -24 /* Allocate stack for arguments(3), return value(2), and return address(1) */
	sw      ra, 20(sp) /* [sp + 20] - [sp + 23] store the return address */
	addi    a0, sp, 12 /* [sp + 12] - [sp + 19] store the return value */
	jal     _do_tlb_refill /* (Pte *, u_int, u_int) [sp + 0] - [sp + 11] reserved for 3 args */
	lw      a0, 12(sp) /* Return value 0 - Even page table entry */
	lw      a1, 16(sp) /* Return value 1 - Odd page table entry */
	lw      ra, 20(sp) /* Return address */
	addi    sp, sp, 24 /* Deallocate stack */
	mtc0    a0, CP0_ENTRYLO0 /* Even page table entry */
	mtc0    a1, CP0_ENTRYLO1 /* Odd page table entry */
	nop
	/* Hint: use 'tlbwr' to write CP0.EntryHi/Lo into a random tlb entry. */
	/* Exercise 2.10: Your code here. */
	tlbwr
	jr      ra
END(do_tlb_refill)
