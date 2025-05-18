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
 *   保存所有通用寄存器、CP0 状态寄存器、EPC、BadVAddr 等重要寄存器状态。
 *
 *   关键逻辑说明：
 *   - 通过检查 STATUS_UM 位判断异常来源模式
 *   - 利用延迟槽特性在分支跳转前保存原栈指针
 *   - 为两种模式统一预留 TF_SIZE 大小的栈空间
 *   - 按固定偏移将 32 个通用寄存器+协处理器寄存器存入栈帧
 *
 * Precondition：
 * - 必须在异常处理的最初阶段调用（处于异常处理上下文）
 * - CP0_STATUS 寄存器包含有效的异常状态信息
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
 * - 覆盖 k0/k1 寄存器的值（作为临时寄存器）
 * - 写入 TF_SIZE 字节的栈内存空间
 *
 * 设计要点：
 * - 通过 andi + beqz 实现高效的 UM 位检测
 * - 延迟槽技巧确保 sp 切换不影响原栈指针保存
 * - 寄存器保存顺序遵循 MIPS 调用约定
 * - 显式处理 $0 寄存器（虽然恒为0，但保证帧结构一致性）
 */
.macro SAVE_ALL
.set noat							// 禁用 $at 宏展开，防止与手动寄存器操作冲突
.set noreorder						// 保持指令顺序，确保延迟槽语义正确
	// 第一阶段：检测异常模式并准备栈指针
	mfc0    k0, CP0_STATUS			// 获取异常时的状态寄存器
	andi    k0, STATUS_UM			// 掩码提取 UM 位（bit[4]）
	beqz    k0, 1f					// UM=0（内核模式）跳转到标签1
	move    k0, sp					// 延迟槽指令：始终执行，保存原 sp 到 k0
	/*
	* If STATUS_UM is not set, the exception was triggered in kernel mode.
	* $sp is already a kernel stack pointer, we don't need to set it again.
	*/
	// 用户模式异常处理路径
	li      sp, KSTACKTOP			// 加载内核栈顶地址到 sp
1:	// 统一处理入口（含内核模式异常重入）
	subu    sp, sp, TF_SIZE			// 在目标栈上分配陷阱帧空间

	// 第二阶段：保存关键寄存器到陷阱帧
	sw      k0, TF_REG29(sp)		// 保存原始 sp（用户栈或内核栈）
	mfc0    k0, CP0_STATUS
	sw      k0, TF_STATUS(sp)
	mfc0    k0, CP0_CAUSE
	sw      k0, TF_CAUSE(sp)
	mfc0    k0, CP0_EPC
	sw      k0, TF_EPC(sp)
	mfc0    k0, CP0_BADVADDR
	sw      k0, TF_BADVADDR(sp)
	mfhi    k0
	sw      k0, TF_HI(sp)
	mflo    k0
	sw      k0, TF_LO(sp)
	sw      $0, TF_REG0(sp)
	sw      $1, TF_REG1(sp)
	sw      $2, TF_REG2(sp)
	sw      $3, TF_REG3(sp)
	sw      $4, TF_REG4(sp)
	sw      $5, TF_REG5(sp)
	sw      $6, TF_REG6(sp)
	sw      $7, TF_REG7(sp)
	sw      $8, TF_REG8(sp)
	sw      $9, TF_REG9(sp)
	sw      $10, TF_REG10(sp)
	sw      $11, TF_REG11(sp)
	sw      $12, TF_REG12(sp)
	sw      $13, TF_REG13(sp)
	sw      $14, TF_REG14(sp)
	sw      $15, TF_REG15(sp)
	sw      $16, TF_REG16(sp)
	sw      $17, TF_REG17(sp)
	sw      $18, TF_REG18(sp)
	sw      $19, TF_REG19(sp)
	sw      $20, TF_REG20(sp)
	sw      $21, TF_REG21(sp)
	sw      $22, TF_REG22(sp)
	sw      $23, TF_REG23(sp)
	sw      $24, TF_REG24(sp)
	sw      $25, TF_REG25(sp)
	sw      $26, TF_REG26(sp)
	sw      $27, TF_REG27(sp)
	sw      $28, TF_REG28(sp)
	sw      $30, TF_REG30(sp)
	sw      $31, TF_REG31(sp)
.set at
.set reorder
.endm

/*
 * RESTORE_ALL 宏：
 *
 * 功能：从栈中恢复所有寄存器状态（包括 CP0 寄存器）
 *
 * Precondition:
 * - 栈指针 sp 必须指向符合 Trapframe 结构的有效内存区域
 *
 * Postcondition:
 * - 所有通用寄存器（$1-$31）被恢复
 * - CP0_STATUS、LO、HI、EPC 寄存器被恢复
 * - 栈指针 sp 被重新定位到 Trapframe 中存储的地址
 *
 * 副作用：
 * - 修改所有通用寄存器及部分 CP0 寄存器
 * - 修改栈指针 sp 的值
 */
.macro RESTORE_ALL
.set noreorder
.set noat
	lw      v0, TF_STATUS(sp)
	mtc0    v0, CP0_STATUS
	lw      v1, TF_LO(sp)
	mtlo    v1
	lw      v0, TF_HI(sp)
	lw      v1, TF_EPC(sp)
	mthi    v0
	mtc0    v1, CP0_EPC
	lw      $31, TF_REG31(sp)
	lw      $30, TF_REG30(sp)
	lw      $28, TF_REG28(sp)
	lw      $25, TF_REG25(sp)
	lw      $24, TF_REG24(sp)
	lw      $23, TF_REG23(sp)
	lw      $22, TF_REG22(sp)
	lw      $21, TF_REG21(sp)
	lw      $20, TF_REG20(sp)
	lw      $19, TF_REG19(sp)
	lw      $18, TF_REG18(sp)
	lw      $17, TF_REG17(sp)
	lw      $16, TF_REG16(sp)
	lw      $15, TF_REG15(sp)
	lw      $14, TF_REG14(sp)
	lw      $13, TF_REG13(sp)
	lw      $12, TF_REG12(sp)
	lw      $11, TF_REG11(sp)
	lw      $10, TF_REG10(sp)
	lw      $9, TF_REG9(sp)
	lw      $8, TF_REG8(sp)
	lw      $7, TF_REG7(sp)
	lw      $6, TF_REG6(sp)
	lw      $5, TF_REG5(sp)
	lw      $4, TF_REG4(sp)
	lw      $3, TF_REG3(sp)
	lw      $2, TF_REG2(sp)
	lw      $1, TF_REG1(sp)
	lw      sp, TF_REG29(sp) /* Deallocate stack */
.set at
.set reorder
.endm
