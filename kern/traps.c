#include "mmu.h"
#include "types.h"
#include <backtrace.h>
#include <env.h>
#include <pmap.h>
#include <printk.h>
#include <trap.h>

extern char _kernel_end[];

extern void handle_clock(void);
extern void handle_int(void);
extern void handle_tlb(void);
extern void handle_sys(void);
extern void handle_reserved(void);
extern void handle_page_fault(void);

extern void set_exception_handler(void *exception_handler);
extern void exc_gen_entry(void);

void do_kernel_exception(struct Trapframe *tf);
void do_cow(struct Trapframe *tf);

extern void schedule(int yield);

const char *riscv_exceptions[65] = {
    /* 0 */ "Instruction address misaligned",
    /* 1 */ "Instruction access fault",
    /* 2 */ "Illegal instruction",
    /* 3 */ "Breakpoint",
    /* 4 */ "Load address misaligned",
    /* 5 */ "Load access fault",
    /* 6 */ "Store/AMO address misaligned",
    /* 7 */ "Store/AMO access fault",
    /* 8 */ "Environment call from U-mode",
    /* 9 */ "Environment call from S-mode",
    /* 10 */ "Reserved",
    /* 11 */ "Reserved",
    /* 12 */ "Instruction page fault",
    /* 13 */ "Load page fault",
    /* 14 */ "Reserved",
    /* 15 */ "Store/AMO page fault",
    /* 16 */ "Reserved",
    /* 17 */ "Reserved",
    /* 18 */ "Software check",
    /* 19 */ "Hardware error",
    /* 20 */ "Reserved",
    /* 21 */ "Reserved",
    /* 22 */ "Reserved",
    /* 23 */ "Reserved",
    /* 24 */ "Designated for custom use",
    /* 25 */ "Designated for custom use",
    /* 26 */ "Designated for custom use",
    /* 27 */ "Designated for custom use",
    /* 28 */ "Designated for custom use",
    /* 29 */ "Designated for custom use",
    /* 30 */ "Designated for custom use",
    /* 31 */ "Designated for custom use",
    /* 32 */ "Reserved",
    /* 33 */ "Reserved",
    /* 34 */ "Reserved",
    /* 35 */ "Reserved",
    /* 36 */ "Reserved",
    /* 37 */ "Reserved",
    /* 38 */ "Reserved",
    /* 39 */ "Reserved",
    /* 40 */ "Reserved",
    /* 41 */ "Reserved",
    /* 42 */ "Reserved",
    /* 43 */ "Reserved",
    /* 44 */ "Reserved",
    /* 45 */ "Reserved",
    /* 46 */ "Reserved",
    /* 47 */ "Reserved",
    /* 48 */ "Designated for custom use",
    /* 49 */ "Designated for custom use",
    /* 50 */ "Designated for custom use",
    /* 51 */ "Designated for custom use",
    /* 52 */ "Designated for custom use",
    /* 53 */ "Designated for custom use",
    /* 54 */ "Designated for custom use",
    /* 55 */ "Designated for custom use",
    /* 56 */ "Designated for custom use",
    /* 57 */ "Designated for custom use",
    /* 58 */ "Designated for custom use",
    /* 59 */ "Designated for custom use",
    /* 60 */ "Designated for custom use",
    /* 61 */ "Designated for custom use",
    /* 62 */ "Designated for custom use",
    /* 63 */ "Designated for custom use",
    /* 64 */ "Reserved"};

void (*exception_handlers[64])(void) = {
    [0 ... 63] = handle_reserved, [8] = handle_sys,
    [12] = handle_page_fault,     [13] = handle_page_fault,
    [15] = handle_page_fault,
};

void (*interrupt_handlers[64])(void) = {
    [0 ... 63] = handle_reserved,
    [5] = handle_clock,
};

void do_kernel_exception(struct Trapframe *tf) {
    u_reg_t cause = (u_reg_t)tf->scause;

    printk("\nException(%ld, %s) raised in kernel code!\n\n", cause,
           riscv_exceptions[cause]);

    print_backtrace(tf->sepc, tf->regs[8], tf->regs[2]);

    printk("\n");

    panic("Exception raised in kernel code!");
}

/* Overview:
 *   The fallback handler when an unknown exception code is encountered.
 *   'genex.S' wraps this function in 'handle_reserved'.
 */
void do_reserved(struct Trapframe *tf) {
    print_tf(tf);

    if ((reg_t)tf->scause < 0) {
        panic("Unknown Interrupt Code %2ld", -((reg_t)tf->scause));
    } else {
        panic("Unknown Exception Code %2ld - %s", (reg_t)tf->scause,
              riscv_exceptions[(reg_t)tf->scause]);

        if ((tf->sepc) >= BASE_ADDR_IMM && (tf->sepc < (u_reg_t)_kernel_end)) {
            do_kernel_exception(tf);
        }
    }
}

void do_clock(struct Trapframe *tf) { schedule(0); }

void do_page_fault(struct Trapframe *tf) {
    // 对于内核代码发生的缺页异常，使用do_kernel_exception处理
    if ((tf->sepc) >= BASE_ADDR_IMM && (tf->sepc < (u_reg_t)_kernel_end)) {
        // 若地址属于允许分配的地址，进行分配

        if (tf->badvaddr >= KMALLOC_BEGIN_VA && tf->badvaddr < KMALLOC_END_VA) {
            kernel_passive_alloc(tf->badvaddr);
        } else {
            do_kernel_exception(tf);
        }
    } else {
        // 异常发生在用户区域代码
        if (curenv == NULL) {
            panic("Page fault from user space but curenv is NULL!");
        }

        Pte *pte = NULL;

        if (page_lookup(curenv->env_pgdir, tf->badvaddr, &pte) == NULL) {
            // 对于用户程序，若请求的页不存在，直接分配页
            passive_alloc(tf->badvaddr, curenv->env_pgdir, curenv->env_id);
        } else {
            // 对于用户程序，若请求的页存在，检查是否是CoW页

            if ((*pte & PTE_COW) != 0) {
                do_cow(tf);
            } else {
                // 访问违例

                panic("Access violation for va = 0x%016lx pte = %016lx\n",
                      tf->badvaddr, *pte);
            }
        }
    }
}

void exception_init() { set_exception_handler((void *)exc_gen_entry); }

void do_cow(struct Trapframe *tf) {
    int ret = 0;

    // curenv 已在do_page_fault中检查
    Pte *pte = NULL;

    page_lookup(curenv->env_pgdir, tf->badvaddr, &pte);

    if (pte == NULL) {
        panic("CoW exception at va = 0x%016lx, but page_lookup returned null",
              tf->badvaddr);
    }

    uint32_t perm = PTE_FLAGS(*pte);

    // 该页一定是CoW页（已在do_page_fault中检查）

    perm = (perm & ~PTE_COW) | PTE_W;

    struct Page *new_page = NULL;

    if ((ret = page_alloc(&new_page)) < 0) {
        panic("Cannot allocate page for CoW for va = 0x%016lx: %d\n",
              tf->badvaddr, ret);
    }

    u_reg_t page_begin_va = ROUNDDOWN(tf->badvaddr, PAGE_SIZE);

    memcpy((void *)page2kva(new_page), (void *)P2KADDR(PTE_ADDR(*pte)),
           PAGE_SIZE);

    page_insert(curenv->env_pgdir, curenv->env_asid, new_page, tf->badvaddr,
                perm);
}