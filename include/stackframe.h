#ifndef __STACK_FRAME_H__
#define __STACK_FRAME_H__

#include <asm/asm.h>
#include <mmu.h>
#include <trap.h>

// clang-format off
/*
 * 概述：
 *
 *   在异常发生时保存所有处理器的寄存器状态到内核栈，构建陷阱帧（Trap Frame）。
 *   根据异常触发模式（用户态/内核态）自动切换栈指针：
 *   - 用户态异常：切换到内核栈顶 KSTACKTOP
 *   - 内核态异常（异常重入）：复用当前内核栈，可在上一个异常的栈下继续保存
 *   保存所有通用寄存器、sstatus、sepc、BadVAddr 等重要寄存器状态。
 *
 *   关键逻辑说明：
 *   - 通过检查 SPP 位判断异常来源模式
 *   - 为两种模式统一预留 TF_SIZE 大小的栈空间
 *   - 按固定偏移将 32 个通用寄存器+部分状态寄存器存入栈帧
 *
 * Precondition：
 * - 必须在异常处理的最初阶段调用（处于异常处理上下文）
 * - sstatus 寄存器包含有效的异常状态信息
 * - 要求全局常量 KSTACKTOP 已正确初始化（指向内核栈顶）
 * - 要求 TF_SIZE 与 trapframe 结构体大小严格一致
 *
 * Postcondition：
 * - 栈指针 sp 指向完整构造的陷阱帧起始地址
 * - 陷阱帧包含异常发生瞬间完整的处理器状态快照
 * - 用户态异常的栈位置：KSTACKTOP - TF_SIZE
 * - 内核态异常的栈位置：原内核栈顶 - TF_SIZE
 *
 * 副作用：
 * - 修改栈指针 sp 的值
 * - 覆盖 sscratch 寄存器的值（作为临时寄存器）
 * - 写入 TF_SIZE 字节的栈内存空间
 */
.macro SAVE_ALL
	// 第一阶段：检测异常模式并准备栈指针
	// 保存t0到sscratch
	csrw	sscratch, t0

	csrr	t0, sstatus

	andi    t0, t0, SSTATUS_SPP			// 掩码提取 SPP 位（bit[8]）
	bnez    t0, 1f					// SPP != 0（内核模式）跳转到标签1
	/*
	* If SPP != 0, the exception was triggered in kernel mode.
	* $sp is already a kernel stack pointer, we don't need to set it again.
	*/
	// 用户模式异常处理路径
	move    t0, sp					// 保存原 sp 到 t0
	li      sp, KSTACKTOP			// 加载内核栈顶地址到 sp
	j		2f
1:	// 统一处理入口（含内核模式异常重入）
	move    t0, sp					// 保存原 sp 到 t0
2:
	addi    sp, sp, -TF_SIZE			// 在目标栈上分配陷阱帧空间
	// 第二阶段：保存关键寄存器到陷阱帧
	sd      t0, TF_REG2(sp)		// 保存原始 sp（用户栈或内核栈）

	csrr    t0, sstatus
	sd      t0, TF_SSTATUS(sp)

	csrr    t0, stval
	sd      t0, TF_BADVADDR(sp)

	csrr    t0, scause
	sd      t0, TF_SCAUSE(sp)

	csrr    t0, sepc
	sd      t0, TF_SEPC(sp)

	csrr    t0, sip
	sd      t0, TF_SIP(sp)

	csrr    t0, sie
	sd      t0, TF_SIE(sp)

	// 恢复t0
	csrr	t0, sscratch

	sd      x0, TF_REG0(sp)
	sd      x1, TF_REG1(sp)
	sd      x3, TF_REG3(sp)
	sd      x4, TF_REG4(sp)
	sd      x5, TF_REG5(sp)
	sd      x6, TF_REG6(sp)
	sd      x7, TF_REG7(sp)
	sd      x8, TF_REG8(sp)
	sd      x9, TF_REG9(sp)
	sd      x10, TF_REG10(sp)
	sd      x11, TF_REG11(sp)
	sd      x12, TF_REG12(sp)
	sd      x13, TF_REG13(sp)
	sd      x14, TF_REG14(sp)
	sd      x15, TF_REG15(sp)
	sd      x16, TF_REG16(sp)
	sd      x17, TF_REG17(sp)
	sd      x18, TF_REG18(sp)
	sd      x19, TF_REG19(sp)
	sd      x20, TF_REG20(sp)
	sd      x21, TF_REG21(sp)
	sd      x22, TF_REG22(sp)
	sd      x23, TF_REG23(sp)
	sd      x24, TF_REG24(sp)
	sd      x25, TF_REG25(sp)
	sd      x26, TF_REG26(sp)
	sd      x27, TF_REG27(sp)
	sd      x28, TF_REG28(sp)
	sd      x29, TF_REG29(sp)
	sd      x30, TF_REG30(sp)
	sd      x31, TF_REG31(sp)

.endm

/*
 * RESTORE_ALL 宏：
 *
 * 功能：从栈中恢复所有寄存器状态（包括状态寄存器）
 *
 * Precondition:
 * - 栈指针 sp 必须指向符合 Trapframe 结构的有效内存区域
 *
 * Postcondition:
 * - 所有通用寄存器（x1-x31）被恢复
 * - sstatus、badvaddr、scause、sepc 寄存器被恢复
 * - 栈指针 sp 被重新定位到 Trapframe 中存储的地址
 *
 * 副作用：
 * - 修改所有通用寄存器及部分状态寄存器
 * - 修改栈指针 sp 的值
 */
.macro RESTORE_ALL
	ld      a0, TF_SSTATUS(sp)
	csrw    sstatus, a0

	ld		a0, TF_BADVADDR(sp)
	csrw	stval, a0

	ld		a0, TF_SCAUSE(sp)
	csrw	scause, a0

	ld		a0, TF_SEPC(sp)
	csrw	sepc, a0

	ld		a0, TF_SIE(sp)
	csrw	sie, a0

	ld		a0, TF_SIP(sp)
	csrw	sip, a0

	ld      x31, TF_REG31(sp)
	ld      x30, TF_REG30(sp)
	ld      x29, TF_REG29(sp)
	ld      x28, TF_REG28(sp)
	ld      x27, TF_REG27(sp)
	ld      x26, TF_REG26(sp)
	ld      x25, TF_REG25(sp)
	ld      x24, TF_REG24(sp)
	ld      x23, TF_REG23(sp)
	ld      x22, TF_REG22(sp)
	ld      x21, TF_REG21(sp)
	ld      x20, TF_REG20(sp)
	ld      x19, TF_REG19(sp)
	ld      x18, TF_REG18(sp)
	ld      x17, TF_REG17(sp)
	ld      x16, TF_REG16(sp)
	ld      x15, TF_REG15(sp)
	ld      x14, TF_REG14(sp)
	ld      x13, TF_REG13(sp)
	ld      x12, TF_REG12(sp)
	ld      x11, TF_REG11(sp)
	ld      x10, TF_REG10(sp)
	ld      x9, TF_REG9(sp)
	ld      x8, TF_REG8(sp)
	ld      x7, TF_REG7(sp)
	ld      x6, TF_REG6(sp)
	ld      x5, TF_REG5(sp)
	ld      x4, TF_REG4(sp)
	ld      x3, TF_REG3(sp)
	ld      x1, TF_REG1(sp)
	ld      sp, TF_REG2(sp) /* Deallocate stack */
.endm

#endif