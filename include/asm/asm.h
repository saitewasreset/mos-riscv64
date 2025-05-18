#include <asm/regdef.h>

#define BEGIN(symbol, framesize)                                               \
  .globl symbol;                                                               \
  .align 2;                                                                    \
  .type symbol, @function;                                                     \
  symbol:                                                                      \
  .cfi_startproc;                                                              \
  addi sp, sp, -framesize; /* 分配动态栈空间 */                                \
  .cfi_def_cfa_offset framesize;                                               \
  sd ra, (framesize - 8)(sp);  /* 保存返回地址 */                              \
  sd s0, (framesize - 16)(sp); /* 保存帧指针 */                                \
  .cfi_offset ra, -8;                                                          \
  .cfi_offset s0, -16;                                                         \
  addi s0, sp, framesize; /* 设置新帧指针 */                                   \
  .cfi_def_cfa s0, 0

#define END(symbol, framesize)                                                 \
  ld s0, (framesize - 16)(sp); /* 恢复帧指针 */                                \
  .cfi_restore s0;                                                             \
  ld ra, (framesize - 8)(sp); /* 恢复返回地址 */                               \
  .cfi_restore ra;                                                             \
  .cfi_def_cfa sp, framesize;                                                  \
  addi sp, sp, framesize; /* 释放栈空间 */                                     \
  .cfi_def_cfa_offset 0;                                                       \
  jr ra;                                                                       \
  .cfi_endproc;                                                                \
  .size symbol, .- symbol

/*
 * EXPORT - export global symbol (RISC-V version)
 * 与MIPS版本基本相同
 */
#define EXPORT(symbol)                                                         \
  .globl symbol;                                                               \
  symbol:

/*
 * FEXPORT - export function symbol (RISC-V version)
 * 与MIPS版本基本相同
 */
#define FEXPORT(symbol)                                                        \
  .globl symbol;                                                               \
  .type symbol, @function;                                                     \
  symbol:
