#ifndef __ASM_RISCV64_REGDEF_H
#define __ASM_RISCV64_REGDEF_H

/*
 * Symbolic register names for 32 bit ABI
 */

#define zero x0 /* hardwired zero */
#define ra x1   /* return address */
#define sp x2   /* stack pointer */
#define gp x3   /* global pointer */
#define tp x4   /* thread pointer */
#define t0 x5   /* temporary register 0 */
#define t1 x6   /* temporary register 1 */
#define t2 x7   /* temporary register 2 */
#define s0 x8   /* saved register 0 / frame pointer */
#define fp x8   /* frame pointer (same as s0) */
#define s1 x9   /* saved register 1 */
#define a0 x10  /* function argument 0 / return value 0 */
#define a1 x11  /* function argument 1 / return value 1 */
#define a2 x12  /* function argument 2 */
#define a3 x13  /* function argument 3 */
#define a4 x14  /* function argument 4 */
#define a5 x15  /* function argument 5 */
#define a6 x16  /* function argument 6 */
#define a7 x17  /* function argument 7 */
#define s2 x18  /* saved register 2 */
#define s3 x19  /* saved register 3 */
#define s4 x20  /* saved register 4 */
#define s5 x21  /* saved register 5 */
#define s6 x22  /* saved register 6 */
#define s7 x23  /* saved register 7 */
#define s8 x24  /* saved register 8 */
#define s9 x25  /* saved register 9 */
#define s10 x26 /* saved register 10 */
#define s11 x27 /* saved register 11 */
#define t3 x28  /* temporary register 3 */
#define t4 x29  /* temporary register 4 */
#define t5 x30  /* temporary register 5 */
#define t6 x31  /* temporary register 6 */

#endif /* __ASM_RISCV64_REGDEF_H */
