#include <env.h>
#include <lib.h>
#include <mmu.h>
#include <syscall.h>
#include <trap.h>

void syscall_putchar(int ch) { msyscall(SYS_putchar, ch); }

int syscall_print_cons(const void *str, size_t num) {
    return msyscall(SYS_print_cons, str, num);
}

u_int syscall_getenvid(void) { return msyscall(SYS_getenvid); }

void syscall_yield(void) { msyscall(SYS_yield); }

int syscall_env_destroy(u_int envid) {
    return msyscall(SYS_env_destroy, envid);
}

int syscall_mem_alloc(u_int envid, void *va, u_int perm) {
    return msyscall(SYS_mem_alloc, envid, va, perm);
}

int syscall_mem_map(u_int srcid, void *srcva, u_int dstid, void *dstva,
                    u_int perm) {
    return msyscall(SYS_mem_map, srcid, srcva, dstid, dstva, perm);
}

int syscall_mem_unmap(u_int envid, void *va) {
    return msyscall(SYS_mem_unmap, envid, va);
}

int syscall_set_env_status(u_int envid, u_int status) {
    return msyscall(SYS_set_env_status, envid, status);
}

int syscall_set_trapframe(u_int envid, struct Trapframe *tf) {
    return msyscall(SYS_set_trapframe, envid, tf);
}

void syscall_panic(const char *msg) {
    int r = msyscall(SYS_panic, msg);
    user_panic("SYS_panic returned %d", r);
}

int syscall_ipc_try_send(uint32_t envid, uint64_t value, const void *srcva,
                         uint32_t perm) {
    return msyscall(SYS_ipc_try_send, envid, value, srcva, perm);
}

int syscall_ipc_recv(void *dstva) { return msyscall(SYS_ipc_recv, dstva); }

int syscall_cgetc() { return msyscall(SYS_cgetc); }

int syscall_write_dev(u_reg_t va, u_reg_t pa, u_reg_t len) {
    /* Exercise 5.2: Your code here. (1/2) */
    return msyscall(SYS_write_dev, va, pa, len);
}

int syscall_read_dev(u_reg_t va, u_reg_t pa, u_reg_t len) {
    /* Exercise 5.2: Your code here. (2/2) */
    return msyscall(SYS_read_dev, va, pa, len);
}

void syscall_map_user_vpt(void) { msyscall(SYS_map_user_vpt); }

void syscall_unmap_user_vpt(void) { msyscall(SYS_unmap_user_vpt); }

void syscall_sleep(void) { msyscall(SYS_sleep); }

int syscall_set_interrupt_handler(uint32_t interrupt_code, u_reg_t handler_va) {
    return msyscall(SYS_set_interrupt_handler, interrupt_code, handler_va);
}

int syscall_get_device_count(char *device_type) {
    return msyscall(SYS_get_device_count, device_type);
}

int syscall_get_device(char *device_type, size_t idx, size_t max_data_len,
                       u_reg_t out_device, u_reg_t out_device_data) {
    return msyscall(SYS_get_device, device_type, idx, max_data_len, out_device,
                    out_device_data);
}

__attribute__((always_inline)) inline void syscall_interrupt_return(void) {
    msyscall(SYS_interrupt_return, 0, 0, 0, 0, 0);
}

int syscall_get_process_list(int max_len, u_reg_t out_process_list) {
    return msyscall(SYS_get_process_list, max_len, out_process_list, 0, 0, 0);
}

u_reg_t syscall_get_physical_address(void *va) {
    return msyscall(SYS_get_physical_address, (u_reg_t)va);
}