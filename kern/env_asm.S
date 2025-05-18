#include <asm/asm.h>
#include <mmu.h>
#include <trap.h>
#include <kclock.h>

/*
 * env_pop_tf:
 *
 * 功能：从指定的 Trapframe 恢复上下文，重置时钟，设置ASID，并返回到用户态
 *
 * Precondition:
 * - tf 必须指向一个有效的 Trapframe 结构，该结构包含完整的寄存器状态
 * - asid 必须是一个有效的地址空间标识符，与当前进程对应
 *
 * Postcondition:
 * - 恢复 Trapframe 中的所有寄存器状态，重置时钟计数器
 * - 通过 eret 指令返回到用户态，完成上下文切换
 *
 * 副作用：
 * - 修改 CP0_ENTRYHI 寄存器以设置 ASID
 * - 通过 RESET_KCLOCK 重置时钟相关寄存器
 */
.text
LEAF(env_pop_tf)
.set reorder
.set at
	mtc0    a1, CP0_ENTRYHI
	move    sp, a0
	RESET_KCLOCK
	j       ret_from_exception
END(env_pop_tf)
