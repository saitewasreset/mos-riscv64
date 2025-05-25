#include <backtrace.h>
#include <env.h>
#include <print.h>
#include <printk.h>
#include <stdint.h>

void outputk(void *data, const char *buf, size_t len);

void _panic(const char *file, int line, const char *func, const char *fmt,
            ...) {
    uint64_t zero, ra, sp, gp, tp;
    uint64_t t0, t1, t2;
    uint64_t s0_fp, s1;
    uint64_t a0, a1, a2, a3, a4, a5, a6, a7;
    uint64_t s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
    uint64_t t3, t4, t5, t6;

    // 第一部分：x0-x15
    asm volatile(
        "mv %[zero], x0\n"
        "mv %[ra], x1\n"
        "mv %[sp], x2\n"
        "mv %[gp], x3\n"
        "mv %[tp], x4\n"
        "mv %[t0], x5\n"
        "mv %[t1], x6\n"
        "mv %[t2], x7\n"
        "mv %[s0_fp], x8\n"
        "mv %[s1], x9\n"
        "mv %[a0], x10\n"
        "mv %[a1], x11\n"
        "mv %[a2], x12\n"
        "mv %[a3], x13\n"
        "mv %[a4], x14\n"
        "mv %[a5], x15\n"
        : [zero] "=r"(zero), [ra] "=r"(ra), [sp] "=r"(sp), [gp] "=r"(gp),
          [tp] "=r"(tp), [t0] "=r"(t0), [t1] "=r"(t1), [t2] "=r"(t2),
          [s0_fp] "=r"(s0_fp), [s1] "=r"(s1), [a0] "=r"(a0), [a1] "=r"(a1),
          [a2] "=r"(a2), [a3] "=r"(a3), [a4] "=r"(a4), [a5] "=r"(a5)
        :
        :);

    // 第二部分：x16-x31
    asm volatile(
        "mv %[a6], x16\n"
        "mv %[a7], x17\n"
        "mv %[s2], x18\n"
        "mv %[s3], x19\n"
        "mv %[s4], x20\n"
        "mv %[s5], x21\n"
        "mv %[s6], x22\n"
        "mv %[s7], x23\n"
        "mv %[s8], x24\n"
        "mv %[s9], x25\n"
        "mv %[s10], x26\n"
        "mv %[s11], x27\n"
        "mv %[t3], x28\n"
        "mv %[t4], x29\n"
        "mv %[t5], x30\n"
        "mv %[t6], x31\n"
        : [a6] "=r"(a6), [a7] "=r"(a7), [s2] "=r"(s2), [s3] "=r"(s3),
          [s4] "=r"(s4), [s5] "=r"(s5), [s6] "=r"(s6), [s7] "=r"(s7),
          [s8] "=r"(s8), [s9] "=r"(s9), [s10] "=r"(s10), [s11] "=r"(s11),
          [t3] "=r"(t3), [t4] "=r"(t4), [t5] "=r"(t5), [t6] "=r"(t6)
        :
        :);

    uint64_t badva, status, cause, epc;

    asm volatile(
        "csrr %[stval], stval\n"
        "csrr %[sstatus], sstatus\n"
        "csrr %[scause], scause\n"
        "csrr %[sepc], sepc\n"
        : [stval] "=r"(badva), [sstatus] "=r"(status), [scause] "=r"(cause),
          [sepc] "=r"(epc)
        : // 无输入操作数
        : // 无 clobber 列表
    );

    printk("panic at %s:%d (%s): ", file, line, func);

    va_list ap;
    va_start(ap, fmt);
    vprintfmt(outputk, NULL, fmt, ap);
    va_end(ap);

    printk("\n----- RISC-V Registers -----\n");

    // 1. 固定用途寄存器
    printk(">>> Fixed Registers:\n");
    printk("   zero (x0) = 0x%016lx  // Hard-wired zero\n", zero);
    printk("     ra (x1) = 0x%016lx  // Return address\n", ra);
    printk("     sp (x2) = 0x%016lx  // Stack pointer\n", sp);
    printk("     gp (x3) = 0x%016lx  // Global pointer\n", gp);
    printk("     tp (x4) = 0x%016lx  // Thread pointer\n", tp);

    // 2. 临时寄存器 (Caller-saved)
    printk("\n>>> Temporary Registers (Caller-saved):\n");
    printk("     t0 (x5) = 0x%016lx\n", t0);
    printk("     t1 (x6) = 0x%016lx\n", t1);
    printk("     t2 (x7) = 0x%016lx\n", t2);
    printk("    t3 (x28) = 0x%016lx\n", t3);
    printk("    t4 (x29) = 0x%016lx\n", t4);
    printk("    t5 (x30) = 0x%016lx\n", t5);
    printk("    t6 (x31) = 0x%016lx\n", t6);

    // 3. 保存寄存器 (Callee-saved)
    printk("\n>>> Saved Registers (Callee-saved):\n");
    printk("   s0/fp (x8) = 0x%016lx  // Frame pointer\n", s0_fp);
    printk("      s1 (x9) = 0x%016lx\n", s1);
    printk("     s2 (x18) = 0x%016lx\n", s2);
    printk("     s3 (x19) = 0x%016lx\n", s3);
    printk("     s4 (x20) = 0x%016lx\n", s4);
    printk("     s5 (x21) = 0x%016lx\n", s5);
    printk("     s6 (x22) = 0x%016lx\n", s6);
    printk("     s7 (x23) = 0x%016lx\n", s7);
    printk("     s8 (x24) = 0x%016lx\n", s8);
    printk("     s9 (x25) = 0x%016lx\n", s9);
    printk("    s10 (x26) = 0x%016lx\n", s10);
    printk("    s11 (x27) = 0x%016lx\n", s11);

    // 4. 函数参数/返回值 (Caller-saved)
    printk("\n>>> Function Arguments/Return Values:\n");
    printk("     a0 (x10) = 0x%016lx  // Return value/arg0\n", a0);
    printk("     a1 (x11) = 0x%016lx  // Return value/arg1\n", a1);
    printk("     a2 (x12) = 0x%016lx  // arg2\n", a2);
    printk("     a3 (x13) = 0x%016lx  // arg3\n", a3);
    printk("     a4 (x14) = 0x%016lx  // arg4\n", a4);
    printk("     a5 (x15) = 0x%016lx  // arg5\n", a5);
    printk("     a6 (x16) = 0x%016lx  // arg6\n", a6);
    printk("     a7 (x17) = 0x%016lx  // arg7\n", a7);

    printk("\n>>> Status:\n");
    printk("      badva = 0x%016lx\n", badva);
    printk("     status = 0x%016lx\n", status);
    printk("      cause = 0x%016lx\n", cause);
    printk("        epc = 0x%016lx\n", epc);

    printk("\n----- Backtrace -----\n");

    print_backtrace();

    printk("\nHelldivers never die!\n");

    /*
    u_long sp, ra, badva, sr, cause, epc;
    asm("move %0, $29" : "=r"(sp) :);
    asm("move %0, $31" : "=r"(ra) :);
    asm("mfc0 %0, $8" : "=r"(badva) :);
    asm("mfc0 %0, $12" : "=r"(sr) :);
    asm("mfc0 %0, $13" : "=r"(cause) :);
    asm("mfc0 %0, $14" : "=r"(epc) :);

    printk("panic at %s:%d (%s): ", file, line, func);

    va_list ap;
    va_start(ap, fmt);
    vprintfmt(outputk, NULL, fmt, ap);
    va_end(ap);

    printk("\n"
           "ra:    %08x  sp:  %08x  Status: %08x\n"
           "Cause: %08x  EPC: %08x  BadVA:  %08x\n",
           ra, sp, sr, cause, epc, badva);

    extern struct Env envs[];
    extern struct Env *curenv;
    extern struct Pde *cur_pgdir;

    if ((u_long)curenv >= KERNBASE) {
        printk("curenv:    %x (id = 0x%x, off = %d)\n", curenv, curenv->env_id,
               curenv - envs);
    } else if (curenv) {
        printk("curenv:    %x (invalid)\n", curenv);
    } else {
        printk("curenv:    NULL\n");
    }

    if ((u_long)cur_pgdir >= KERNBASE) {
        printk("cur_pgdir: %x\n", cur_pgdir);
    } else if (cur_pgdir) {
        printk("cur_pgdir: %x (invalid)\n", cur_pgdir);
    } else {
        printk("cur_pgdir: NULL\n", cur_pgdir);
    }
                */

#ifdef MOS_HANG_ON_PANIC
    while (1) {
    }
#else
    halt();
#endif
}
