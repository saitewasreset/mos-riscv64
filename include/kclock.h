#ifndef _KCLOCK_H_
#define _KCLOCK_H_

#include <asm/asm.h>

#define TIMER_INTERVAL (100000) // WARNING: DO NOT MODIFY THIS LINE!

// clang-format off
/*
 * RESET_KCLOCK 宏：
 * 功能：重置时钟计数器，设置下一次定时器中断
 * 注释翻译：
 *   - 使用 'mtc0' 向 CP0_COUNT 和 CP0_COMPARE 寄存器写入适当的值
 *   - 写入 CP0_COMPARE 寄存器会清除定时器中断
 *   - CP0_COUNT 寄存器以固定频率递增，当 CP0_COUNT 与 CP0_COMPARE 值相等时触发定时器中断
 * Precondition:
 * - TIMER_INTERVAL 必须已定义为有效的时钟间隔值
 * - 必须处于内核态以访问 CP0 寄存器
 * Postcondition:
 * - CP0_COUNT 被重置为 0
 * - CP0_COMPARE 被设置为 TIMER_INTERVAL
 * 副作用：
 * - 修改 CP0_COUNT 和 CP0_COMPARE 寄存器
 * - 清除待处理的定时器中断
 */
.macro RESET_KCLOCK
	rdtime t0

	li 	t1, TIMER_INTERVAL

	add t0, t0, t1

	mv a0, t0

	jal set_next_timer_interrupt

.endm
// clang-format on

#endif
