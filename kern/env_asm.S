#include <asm/asm.h>
#include <mmu.h>
#include <trap.h>
#include <kclock.h>
#include <stackframe.h>

/*
 * env_pop_tf:
 *
 * 功能：从指定的 Trapframe 恢复上下文，重置时钟，设置ASID，并返回到用户态
 *
 * Precondition:
 * - a0（第一个参数）：存储陷阱帧的内存地址
 * - a1（第二个参数）：ASID
 * - a2（第三个参数）：一级页表的物理页号
 * - tf 必须指向一个有效的 Trapframe 结构，该结构包含完整的寄存器状态
 * - asid 必须是一个有效的地址空间标识符，与当前进程对应
 *
 * Postcondition:
 * - 恢复 Trapframe 中的所有寄存器状态，重置时钟计数器
 * - 通过 sret 指令返回到用户态，完成上下文切换
 *
 * 副作用：
 * - 修改 CP0_ENTRYHI 寄存器以设置 ASID
 * - 通过 RESET_KCLOCK 重置时钟相关寄存器
 */
.text
BEGIN(env_pop_tf, 16)
    li      t1, 0xF000000000000000

    csrr    t0, satp

    and     t0, t0, t1

    // 设置asid
    slli    a1, a1, 44
    or      t0, t0, a1

    // 设置一级页表PPN
    or      t0, t0, a2

    csrw    satp, t0

    mv      s1, a0

    // 含有函数调用，需要保存a0!
	RESET_KCLOCK
	
    mv    sp, s1
    RESTORE_ALL

    sret
END(env_pop_tf, 16)
