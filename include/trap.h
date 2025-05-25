#ifndef _TRAP_H_
#define _TRAP_H_

#ifndef __ASSEMBLER__

#include <types.h>

// 保存进程的运行上下文：通用寄存器、hi、lo、status、epc、cause、badvaddr
struct Trapframe {
    /* Saved main processor registers. */
    u_reg_t regs[32];

    /* Saved special registers. */
    u_reg_t sstatus;
    u_reg_t badvaddr;
    u_reg_t scause;
    u_reg_t sepc;
};

void print_tf(struct Trapframe *tf);

void exception_init();

#endif /* !__ASSEMBLER__ */

/*
 * Stack layout for all exceptions
 */

#define TF_REG0 0
#define TF_REG1 ((TF_REG0) + 8)
#define TF_REG2 ((TF_REG1) + 8)
#define TF_REG3 ((TF_REG2) + 8)
#define TF_REG4 ((TF_REG3) + 8)
#define TF_REG5 ((TF_REG4) + 8)
#define TF_REG6 ((TF_REG5) + 8)
#define TF_REG7 ((TF_REG6) + 8)
#define TF_REG8 ((TF_REG7) + 8)
#define TF_REG9 ((TF_REG8) + 8)
#define TF_REG10 ((TF_REG9) + 8)
#define TF_REG11 ((TF_REG10) + 8)
#define TF_REG12 ((TF_REG11) + 8)
#define TF_REG13 ((TF_REG12) + 8)
#define TF_REG14 ((TF_REG13) + 8)
#define TF_REG15 ((TF_REG14) + 8)
#define TF_REG16 ((TF_REG15) + 8)
#define TF_REG17 ((TF_REG16) + 8)
#define TF_REG18 ((TF_REG17) + 8)
#define TF_REG19 ((TF_REG18) + 8)
#define TF_REG20 ((TF_REG19) + 8)
#define TF_REG21 ((TF_REG20) + 8)
#define TF_REG22 ((TF_REG21) + 8)
#define TF_REG23 ((TF_REG22) + 8)
#define TF_REG24 ((TF_REG23) + 8)
#define TF_REG25 ((TF_REG24) + 8)
/*
 * $26 (k0) and $27 (k1) not saved
 */
#define TF_REG26 ((TF_REG25) + 8)
#define TF_REG27 ((TF_REG26) + 8)
#define TF_REG28 ((TF_REG27) + 8)
#define TF_REG29 ((TF_REG28) + 8)
#define TF_REG30 ((TF_REG29) + 8)
#define TF_REG31 ((TF_REG30) + 8)

#define TF_SSTATUS ((TF_REG31) + 8)

#define TF_BADVADDR ((TF_SSTATUS) + 8)
#define TF_SCAUSE ((TF_BADVADDR) + 8)
#define TF_SEPC ((TF_SCAUSE) + 8)
/*
 * Size of stack frame, word/double word alignment
 */
#define TF_SIZE ((TF_SEPC) + 8)
#endif /* _TRAP_H_ */
