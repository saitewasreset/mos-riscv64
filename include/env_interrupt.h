#ifndef __ENV_INTERRUPT_H
#define __ENV_INTERRUPT_H

#include <env.h>
#include <plic.h>
#include <stdint.h>
#include <trap.h>
#include <types.h>

#define MAX_INTERRUPT 1024

void register_env_interrupt(uint32_t interrupt_code, struct Env *env,
                            u_reg_t handler_function_va);

void handle_env_interrupt(struct Trapframe *tf, uint32_t interrupt_code);

int ret_env_interrupt(struct Trapframe *tf);

#endif