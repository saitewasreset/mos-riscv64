#include <asm/asm.h>
#include <stackframe.h>

/*
 * tlb_miss_entry - TLB缺失异常入口
 *
 * 概述：
 *   TLB缺失异常的统一入口点。直接跳转至通用异常处理流程。
 *   用户态TLB缺失异常向量位于0x80000000，
 *   此处仅作简单跳转，实际处理由通用异常入口完成。
 *
 * Precondition：
 * - 必须链接到0x80000000地址
 * - 需保证exc_gen_entry已正确实现通用异常处理逻辑
 *
 * Postcondition：
 * - 控制流跳转至exc_gen_entry开始通用异常处理流程
 */
.section .text.tlb_miss_entry
tlb_miss_entry:
	j       exc_gen_entry

/*
 * exc_gen_entry - 通用异常处理入口
 *
 * 概述：
 *   系统所有异常（除TLB缺失）的统一处理入口。执行以下关键操作：
 *   1. 保存完整CPU上下文到内核栈（通过SAVE_ALL宏），之后即可在异常处理中使用其它寄存器
 *   2. 配置状态寄存器以保持内核模式，关闭中断，允许异常重入
 *   3. 根据异常原因码进行异常分发
 *   4. 设置sp指向内核栈（保存了上下文的位置）
 *
 * 处理流程细节：
 *   - 通过清除STATUS_UM确保内核模式执行
 *   - 清除STATUS_EXL允许嵌套异常处理
 *   - 禁用全局中断（STATUS_IE清零）
 *   - 提取异常原因码（ExcCode）作为索引查找处理函数
 *   - 跳转到具体异常处理例程
 *
 * Precondition：
 * - 必须链接到0x80000180地址
 * - 全局数组exception_handlers已正确初始化，索引对应MIPS异常码
 * - TF_SIZE必须与trapframe结构体大小严格匹配
 *
 * Postcondition：
 * - CPU处于内核模式，中断全局禁用
 * - 陷阱帧已完整构建于内核栈
 * - 控制流跳转至对应异常处理函数
 *
 * 副作用：
 * - 修改CP0_STATUS寄存器状态（UM/EXL/IE位清零）
 * - 占用t0寄存器进行状态操作
 * - 可能触发二级异常（若exception_handlers访问错误）
 */
.section .text.exc_gen_entry
exc_gen_entry:
	SAVE_ALL
	/*
	* Note: When EXL is set or UM is unset, the processor is in kernel mode.
	* When EXL is set, the value of EPC is not updated when a new exception occurs.
	* To keep the processor in kernel mode and enable exception reentrancy,
	* we unset UM and EXL, and unset IE to globally disable interrupts.
	*/
	mfc0    t0, CP0_STATUS
	and     t0, t0, ~(STATUS_UM | STATUS_EXL | STATUS_IE)
	mtc0    t0, CP0_STATUS
/* Exercise 3.9: Your code here. */
	mfc0	t0, CP0_CAUSE
	// get bit [2, 6]: ExcCode
	andi	t0, 0x7c
	lw		t0, exception_handlers(t0)
	jr		t0