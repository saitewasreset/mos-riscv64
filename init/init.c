#include <asm/asm.h>
#include <sbi.h>
#include <types.h>

void riscv64_init(u_reg_t hart_id, void *dtb_address) {
  // printk("init.c:\tmips_init() is called\n");

  char str[] = "For Super Earth!\n";

  sbi_debug_console_write(sizeof(str), (u_reg_t)str, 0);

  while (1) {
  }

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
  // halt();
}
