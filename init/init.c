#include <asm/asm.h>
#include <env.h>
#include <machine.h>
#include <pmap.h>
#include <printk.h>
#include <sbi.h>
#include <trap.h>
#include <types.h>

/*
 * Note:
 * When build with 'make test lab=?_?', we will replace your 'mips_init' with a
 * generated one from 'tests/lab?_?'.
 */

#ifdef MOS_INIT_OVERRIDDEN
#include <generated/init_override.h>
#else

void riscv64_init(u_reg_t hart_id, void *dtb_address) __attribute__((noreturn));

void riscv64_init(u_reg_t hart_id, void *dtb_address) {
    printk("init.c:\triscv64_init() is called\n");

    exception_init();

    riscv64_detect_memory();
    riscv64_vm_init();
    page_init();

    // physical_memory_manage_check();

    // page_check();

    env_init();

    // envid2env_check();

    env_check();

    printk("My life for Super Earth!\n");
    // lab2:
    // mips_detect_memory(ram_low_size);
    // mips_vm_init();
    // page_init();

    // lab3:
    // env_init();

    // lab3:
    // ENV_CREATE_PRIORITY(user_bare_loop, 1);
    // ENV_CREATE_PRIORITY(user_bare_loop, 2);

    // lab4:
    // ENV_CREATE(user_tltest);
    // ENV_CREATE(user_fktest);
    // ENV_CREATE(user_pingpong);

    // lab6:
    // ENV_CREATE(user_icode);  // This must be the first env!

    // lab5:
    // ENV_CREATE(user_fstest);
    // ENV_CREATE(fs_serv);  // This must be the second env!
    // ENV_CREATE(user_devtst);

    // lab3:
    // schedule(0);
    halt();
}

#endif