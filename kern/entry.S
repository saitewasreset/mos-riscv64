#include <asm/asm.h>
#include <stackframe.h>

.macro BUILD_HANDLER exception handler
.global handle_\exception
handle_\exception:
	mv   	a0, sp
	jal     \handler
	
	RESTORE_ALL

	sret
.endm

// 设置异常处理函数（统一处理所有异常、中断）
// 输入：
//     - a0：异常处理函数的地址，需要对齐4字节边界
// 输出：
//     无
.section .text
BEGIN(set_exception_handler, 16)

	li t0, 0xFFFFFFFFFFFFFFFC
	and a0, a0, t0

	csrw stvec, a0

END(set_exception_handler, 16)

/*
 * exc_gen_entry - 通用异常处理入口
 *
 * 概述：
 *   系统所有异常的统一处理入口。执行以下关键操作：
 *   1. 保存完整CPU上下文到内核栈（通过SAVE_ALL宏），之后即可在异常处理中使用其它寄存器
 *   2. 根据异常原因码进行异常分发
 *   3. 设置sp指向内核栈（保存了上下文的位置）
 *
 * 处理流程细节：
 *   - 提取异常原因码（ExcCode）作为索引查找处理函数
 *   - 跳转到具体异常处理例程
 *
 * Precondition：
 * - 全局数组exception_handlers已正确初始化，索引对应RISCV64异常码
 * - TF_SIZE必须与trapframe结构体大小严格匹配
 *
 * Postcondition：
 * - CPU处于内核模式，中断全局禁用
 * - 陷阱帧已完整构建于内核栈
 * - 控制流跳转至对应异常处理函数
 *
 * 副作用：
 * - 占用t0寄存器进行状态操作
 * - 可能触发二级异常（若exception_handlers访问错误）
 */
.section .text.exc_gen_entry
.global exc_gen_entry
exc_gen_entry:
	SAVE_ALL

	csrr	t0, scause
	
	// 最高位为1，表示是中断，此时有符号比较<0
	bltz	t0, __ege_handle_int

	la		t1, exception_handlers
	slli	t0, t0, 3
	add		t0, t0, t1

	ld		t0, (t0)
	jr		t0
__ege_handle_int:
	// 清除最高位的1
	slli	t0, t0, 1
	srli	t0, t0, 1

	la		t1, interrupt_handlers
	slli	t0, t0, 3
	add		t0, t0, t1

	ld		t0, (t0)
	jr		t0

BUILD_HANDLER reserved do_reserved
BUILD_HANDLER clock do_clock
BUILD_HANDLER page_fault do_page_fault
BUILD_HANDLER sys do_syscall