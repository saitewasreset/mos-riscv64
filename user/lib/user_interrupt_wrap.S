#include <asm/asm.h>

#define SYS_interrupt_return 21

.global user_interrupt_wrap
user_interrupt_wrap:
	mv s0, sp

	la a0, __user_interrupt_handler
	ld a0, 0(a0)

    jalr a0

    mv sp, s0

	li	a5,0
	li	a4,0
	li	a3,0
	li	a2,0
	li	a1,0
	li  a0, SYS_interrupt_return

	j msyscall