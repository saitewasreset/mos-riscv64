#include <env.h>
#include <pmap.h>
#include <printk.h>
#include <trap.h>

extern void handle_int(void);
extern void handle_tlb(void);
extern void handle_sys(void);
extern void handle_mod(void);
extern void handle_reserved(void);

extern void set_exception_handler(void *exception_handler);
extern void exc_gen_entry(void);

void (*exception_handlers[64])(void) = {
    [0 ... 63] = handle_reserved,
};

void (*interrupt_handlers[64])(void) = {
    [0 ... 63] = handle_reserved,
};

/* Overview:
 *   The fallback handler when an unknown exception code is encountered.
 *   'genex.S' wraps this function in 'handle_reserved'.
 */
void do_reserved(struct Trapframe *tf) {
    print_tf(tf);

    if ((reg_t)tf->scause < 0) {
        panic("Unknown Interrupt Code %2ld", -((reg_t)tf->scause));
    } else {
        panic("Unknown Exception Code %2ld", (reg_t)tf->scause);
    }
}

void exception_init() { set_exception_handler((void *)exc_gen_entry); }