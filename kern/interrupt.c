#include <interrupt.h>
#include <printk.h>
#include <trap.h>

void (*interrupt_handler_map[64])(struct Trapframe *) = {0};

void register_interrupt_handler(u_reg_t interrupt_code,
                                interrupt_handler_func handler) {
    if (interrupt_code >= 64) {
        panic("register_interrupt_handler: invalid interrupt code: %lu",
              interrupt_code);
    }

    interrupt_handler_map[interrupt_code] = handler;
}

void enable_interrupt(u_reg_t interrupt_code) {
    uint64_t sie_value;

    // 读取当前SIE寄存器值
    asm volatile("csrr %0, sie" : "=r"(sie_value));

    // 设置指定中断位
    sie_value |= interrupt_code;

    // 写回SIE寄存器
    asm volatile("csrw sie, %0" : : "r"(sie_value));
}

void disable_interrupt(u_reg_t interrupt_code) {
    uint64_t sie_value;

    // 读取当前SIE寄存器值
    asm volatile("csrr %0, sie" : "=r"(sie_value));

    // 设置指定中断位
    sie_value &= (~interrupt_code);

    // 写回SIE寄存器
    asm volatile("csrw sie, %0" : : "r"(sie_value));
}