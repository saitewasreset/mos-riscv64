#include <asm/asm.h>
#include <mmu.h>

.section .bss.stack
.global KERNEL_STACK
KERNEL_STACK:
    .skip 1 << 12 # 预留 4KiB 内核栈空间

.section .text.boot
EXPORT(_start)
/* Lab 1 Key Code "enter-kernel" */
	// 保存OpenSBI的参数
	mv s10, a0
	mv s11, a1

	// 临时栈
	li sp, KERNEL_END_ADDR_BEFORE_PAGING_IMM;

	// 启用分页，将[FFFF_FFC0_0000_0000, FFFF_FFC0_4000_0000)
	// 直接映射到物理内存
	jal enable_paging

	// 临时栈使用完毕，切换到内核栈
	la sp, KERNEL_STACK

	// 计算栈顶地址，加载到sp寄存器
	li t1, 1
	slli t1, t1, 12
	add sp, sp, t1

	// 移除低地址恒等映射
	
	la t1, KERNEL_END_ADDR
	sd zero, 16(t1)

	sfence.vma

	/* clear .bss segment */
	la      t0, bss_start
	la      t1, bss_end
clear_bss_loop:
	beq     t0, t1, clear_bss_done
	sb      zero, 0(t0)
	addi   t0, t0, 1
	j       clear_bss_loop
/* End of Key Code "enter-kernel" */

clear_bss_done:
	/* disable interrupts */
	// mtc0    zero, CP0_STATUS

	/* jump to riscv64_init */
	/* Exercise 1.3: Your code here. (2/2) */

	mv a0, s10
	mv a1, s11

	j riscv64_init

BEGIN(enable_paging, 16)
	// 返回地址和帧指针已由BEGIN宏处理
	
	// 将KERNEL_END_ADDR开始的一页作为页目录
	// 这要求该地址4字节对齐
	// **物理地址**：0x8040_0000
	// 物理页号：0x0008_0400
	// 将一级页表置零

	li a0, KERNEL_END_ADDR_BEFORE_PAGING_IMM
	li a1, PAGE_SIZE

	jal hell_bomb_armed_clear_the_area

	// 页表项：64位 = 10保留 + 44 物理页号 + 2 软件标志 + 8 硬件标志

	// PFN
	li t0, 0x80000000
	srli t0, t0, PAGE_SHIFT

	// 10保留 + 44 物理页号 + 2 软件标志 + 8 硬件标志
	slli t0, t0, FLAG_SHIFT

	// 有效位
	ori t0, t0, PTE_V

	// 可读可写可执行
	ori t0, t0, PTE_RWX

	// 全局
	ori t0, t0, PTE_GLOBAL

	// 现在，合法的一级页表项存储在t0中

	// 高地址映射
	// 将[FFFF_FFC0_0000_0000, FFFF_FFC0_4000_0000)（1GB）
	// 直接映射到物理内存
	// 0xFFFF_FFC0_0000_0000 -> 0x0040_0000_0000
	// P1X(0x0040_0000_0000) = 256 = 0x0100
	// 字节偏移 = 256 * 8 = 2048
	// 低地址恒等映射
	// P1X(0x8000_0000) = 2
	// 字节偏移 = 2 * 8 = 16

	// 低地址恒等映射
	li t1, KERNEL_END_ADDR_BEFORE_PAGING_IMM
	sd t0, 16(t1)

	// 高地址映射
	// 一级页表首地址
	
	// 加上字节偏移
	li t2, 2048
	add t1, t1, t2

	sd t0, 0(t1)

	// 设置satp寄存器
	li t0, KERNEL_END_ADDR_BEFORE_PAGING_IMM

	// PFN
	srli t0, t0, PAGE_SHIFT

	// 分页模式Rv39
	li t1, 8 << 60
	or t0, t0, t1

	// 设置satp寄存器
	csrw satp, t0

	// 刷新TLB
	sfence.vma

	// 调整返回地址

	li t0, BASE_ADDR_IMM;
	li t1, LOAD_ADDR_IMM;

	// 低地址到高地址的距离
	sub t0, t0, t1

	// 将sp、fp转化到高地址
	add sp, sp, t0
	add s0, s0, t0

	// 存储的低地址的返回地址
	ld t1, -8(s0)
	// 转化为高地址
	add t1, t1, t0

	sd t1, -8(s0)

	// 返回地址读取已由END宏处理
END(enable_paging, 16)

// 将从a0开始，长a1的内存区域置零，要求a0、a1对齐8字节边界
// 输入：
//     - a0：开始置零的内存地址，需要8字节对齐
//     - a1：置零长度，需要8字节对齐
// 输出：
//     无
BEGIN(hell_bomb_armed_clear_the_area, 16)
	// i = 0 in t0
	li t0, 0

clear_the_area_loop:
	// a0 + i in t1
	add t1, a0, t0

	// *(u_int64_t *)(a0 + i) = 0
	sd zero, 0(t1)

	addi t0, t0, 8
	bltu t0, a1, clear_the_area_loop

END(hell_bomb_armed_clear_the_area, 16)