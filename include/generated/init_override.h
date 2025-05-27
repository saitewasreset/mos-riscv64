void schedule(int yield);
void riscv64_init(u_reg_t hart_id, void *dtb_address) {
	printk("init.c:\triscv64_init() is called\n");

	exception_init();

    riscv64_detect_memory();
    riscv64_vm_init();
    page_init();

	env_init();

 ENV_CREATE_PRIORITY(test_quick_sort, 1); ENV_CREATE_PRIORITY(test_quick_sort, 2); ENV_CREATE_PRIORITY(test_quick_sort, 3);

	schedule(0);
	panic("init.c:\tend of riscv64_init() reached!");
}
