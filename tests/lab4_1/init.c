void riscv64_init(u_reg_t hart_id, void *dtb_address) {
    printk("init.c:\triscv64_init() is called\n");

    exception_init();

    riscv64_detect_memory();
    riscv64_vm_init();
    page_init();

    env_init();

    envid2env_check();
    halt();
}
