#include "serial.h"
#include <asm/asm.h>
#include <device.h>
#include <device_tree.h>
#include <env.h>
#include <kmalloc.h>
#include <machine.h>
#include <plic.h>
#include <pmap.h>
#include <printk.h>
#include <sbi.h>
#include <sched.h>
#include <trap.h>
#include <types.h>
#include <virtio.h>

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

    kmalloc_init();

    // physical_memory_manage_check();

    // page_check();

    env_init();

    // envid2env_check();

    env_check();

    // Device

    device_tree_init(dtb_address);

    plic_init();

    virtio_init();

    serial_init();

    dump_device();

    allocation_summarize();

    ENV_CREATE_NAME("serial", user_serial);
    ENV_CREATE_NAME("virtio", user_virtio);
    ENV_CREATE_NAME("fs_serv", fs_serv);
    ENV_CREATE_NAME("serial_test", user_serialtest);
    ENV_CREATE_NAME("virtio_test", user_virtiotest);
    ENV_CREATE_NAME("process_test", user_processtest);

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
    schedule(0);
    halt();
}

#endif