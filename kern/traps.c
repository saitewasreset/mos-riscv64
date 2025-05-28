#include "mmu.h"
#include "types.h"
#include "userspace.h"
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
extern void handle_page_mod(void);
extern void handle_reserved(void);
extern void handle_page_fault(void);

extern void set_exception_handler(void *exception_handler);
extern void exc_gen_entry(void);

void do_kernel_exception(struct Trapframe *tf);
void do_page_mod(struct Trapframe *tf);

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
        do_kernel_exception(tf);
    }

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
            do_page_mod(tf);
        } else {
            // 访问违例

            panic("Access violation for va = 0x%016lx pte = %016lx\n",
                  tf->badvaddr, *pte);
        }
    }
}

void exception_init() { set_exception_handler((void *)exc_gen_entry); }

/*
 * 概述：
 *   内核中的Page Mod异常处理函数。我们的内核允许用户程序在用户模式下处理Page
 * Mod异常，
 *   因此我们将异常上下文'tf'复制到用户异常栈(UXSTACK)中，并修改EPC寄存器指向注册的
 *   用户异常处理入口。
 *
 *   注意，处理函数将在用户态运行。
 *
 * 使用用户异常栈(UXSTACK)而非普通用户栈处理异常
 *
 * Precondition：
 * - tf必须指向有效的Trapframe结构
 * - 依赖全局状态：
 *   - curenv：当前运行环境，必须有效且已设置env_user_tlb_mod_entry
 *
 * Postcondition：
 * - 若用户处理函数已注册：
 *   - 将异常上下文保存到用户异常栈
 *   - 设置a0寄存器指向保存的上下文
 *   - 设置EPC指向用户处理函数
 * - 若未注册处理函数，触发panic
 *
 * 副作用：
 * - 修改用户异常栈内容
 * - 修改传入的Trapframe结构内容(寄存器值和EPC)
 */
void do_page_mod(struct Trapframe *tf) {

    struct Trapframe tmp_tf = *tf;

    // 若这是第一次发生异常，将用户栈设置为用户异常栈栈顶；
    // 否则，（说明发生嵌套TLB Mod异常），直接使用上次异常的sp
    // 这样可正确处理嵌套异常
    if (tf->regs[2] < USTACKTOP || tf->regs[2] >= UXSTACKTOP) {
        tf->regs[2] = UXSTACKTOP;
    }
    tf->regs[2] -= sizeof(struct Trapframe);

    copy_user_space(&tmp_tf, (void *)tf->regs[2], sizeof(struct Trapframe));

    Pte *pte;
    page_lookup(cur_pgdir, tf->badvaddr, &pte);

    if (curenv->env_user_tlb_mod_entry) {
        // 10 -> a0
        tf->regs[10] = tf->regs[2];

        // Hint: Set 'cp0_epc' in the context 'tf' to
        // 'curenv->env_user_tlb_mod_entry'.
        /* Exercise 4.11: Your code here. */
        tf->sepc = curenv->env_user_tlb_mod_entry;

    } else {
        panic("TLB Mod but no user handler registered");
    }
}