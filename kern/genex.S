#include <asm/asm.h>
#include <stackframe.h>

.macro BUILD_HANDLER exception handler
NESTED(handle_\exception, TF_SIZE + 8, zero)
	// 将当前sp赋值给a0，使得异常处理函数中可使用参数`struct Trapframe *tf`获取保存的上下文
	move    a0, sp
	addiu   sp, sp, -8
	jal     \handler
	addiu   sp, sp, 8
	j       ret_from_exception
END(handle_\exception)
.endm

/*
 * ret_from_exception:
 *
 * 功能：从异常/中断处理中返回，恢复所有寄存器状态并执行 eret
 *
 * Precondition:
 * - 栈指针 sp 必须指向一个有效且完整的 Trapframe 结构
 *
 * Postcondition:
 * - 所有通用寄存器和 CP0 寄存器被恢复
 * - 通过 eret 指令返回到异常发生前的执行点
 *
 * 副作用：
 * - 修改所有通用寄存器及 CP0 相关寄存器
 */
.text

FEXPORT(ret_from_exception)
	RESTORE_ALL
	eret

/* 概述：
 *   中断异常处理函数。负责识别中断源并分派到具体的中断服务例程。
 * 处理流程：
 *   1. 读取CP0_CAUSE和CP0_STATUS寄存器，计算有效中断源
 *   2. 检查是否是7号中断（定时器中断）
 *      - 若匹配则跳转至timer_irq处理
 *      - 其他中断未实现（见TODO注释）
 *   3. 定时器中断处理：
 *      - 设置调用调度函数`schedule(0)`
 *
 * Precondition：
 * - 处于内核模式，全局中断已禁用（由exc_gen_entry设置）
 * - 全局数组exception_handlers已正确初始化，索引0映射到本函数
 *
 * Postcondition：
 * - 对于定时器中断：调用`schedule(0)`触发调度
 * - 控制流最终通过ret_from_exception恢复执行环境
 *   `schedule` -> `env_run` -> `env_pop_tf` -> `ret_from_exception`
 *
 * 副作用：
 * - 修改t0,t1,t2,a0寄存器
 * - 可能通过schedule修改调度相关全局变量（env_sched_list/curenv）
 * - 未处理的其他中断类型保持pending状态
 */
NESTED(handle_int, TF_SIZE, zero)
	mfc0    t0, CP0_CAUSE
	mfc0    t2, CP0_STATUS
	and     t0, t2
	andi    t1, t0, STATUS_IM7
	bnez    t1, timer_irq
	// TODO: handle other irqs
timer_irq:
	li      a0, 0
	j       schedule
END(handle_int)

BUILD_HANDLER tlb do_tlb_refill

#if !defined(LAB) || LAB >= 4
BUILD_HANDLER mod do_tlb_mod
BUILD_HANDLER sys do_syscall
#endif

BUILD_HANDLER reserved do_reserved
