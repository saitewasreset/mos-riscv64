#include <mmu.h>
#include <pmap.h>
#include <print.h>
#include <printk.h>
#include <sbi.h>
#include <trap.h>
#include <types.h>

/* Lab 1 Key Code "outputk" */
void outputk(void *data, const char *buf, size_t len) {
    sbi_debug_console_write(len, (u_reg_t)va2pa(kernel_boot_pgdir, (u_long)buf),
                            0);
}
/* End of Key Code "outputk" */

/* Lab 1 Key Code "printk" */
void printk(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintfmt(outputk, NULL, fmt, ap);
    va_end(ap);
}
/* End of Key Code "printk" */

void debugk(const char *scope, const char *fmt, ...) {
    printk("%s: ", scope);

    va_list ap;
    va_start(ap, fmt);
    vprintfmt(outputk, NULL, fmt, ap);
    va_end(ap);
}

void print_tf(struct Trapframe *tf) {
    printk("\n>>> Trapframe:\n");
    for (size_t i = 0; i < sizeof(tf->regs) / sizeof(tf->regs[0]); i++) {
        printk("$%2d = 0x%016lx\n", i, tf->regs[i]);
    }

    printk("sstatus = %016lx\n", tf->sstatus);
    printk("badvaddr = %016lx\n", tf->badvaddr);
    printk("scause = %016lx\n", tf->scause);
    printk("sepc = %016lx\n", tf->sepc);
    printk("sie = %016lx\n", tf->sie);
    printk("sip = %016lx\n", tf->sip);
}
