#include <asm/asm.h>

// 在调用msyscall时，函数参数由调用者正确传递，故`msyscall`函数内无需处理
LEAF(msyscall)
	// Just use 'syscall' instruction and return.

	/* Exercise 4.1: Your code here. */

	syscall

	jr ra

END(msyscall)
