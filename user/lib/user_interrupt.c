#include <lib.h>
#include <user_interrupt.h>

u_reg_t __user_interrupt_handler = 0;

extern void user_interrupt_wrap();

void register_user_interrupt_handler(uint32_t interrupt_code,
                                     void (*interrupt_handler)(void)) {
    int ret = 0;

    __user_interrupt_handler = (u_reg_t)interrupt_handler;

    if ((ret = syscall_set_interrupt_handler(
             interrupt_code, (u_reg_t)user_interrupt_wrap)) < 0) {
        user_panic("register_user_interrupt_handler: cannot set interrupt "
                   "handler: %d\n",
                   ret);
    }
}