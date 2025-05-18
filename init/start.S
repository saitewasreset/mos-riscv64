#include <asm/asm.h>
#include <mmu.h>

.section .bss.stack
.global KERNEL_STACK
KERNEL_STACK:
    .skip 1 << 12 # 预留 4KiB 内核栈空间

.section .text.boot
EXPORT(_start)
/* Lab 1 Key Code "enter-kernel" */
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

	// 注意，KERNEL_STACK是栈底
	la t0, KERNEL_STACK

	// 计算栈顶地址，加载到sp寄存器
	li t1, 1
	slli t1, t1, 12
	add sp, t0, t1

	/* jump to riscv64_init */
	/* Exercise 1.3: Your code here. (2/2) */

	j riscv64_init
