#ifndef __INTERRUPT_H
#define __INTERRUPT_H

#include <trap.h>

typedef void (*interrupt_handler_func)(struct Trapframe *tf);

void register_interrupt_handler(u_reg_t interrupt_code,
                                interrupt_handler_func handler);
void enable_interrupt(u_reg_t interrupt_code);
void disable_interrupt(u_reg_t interrupt_code);
#endif