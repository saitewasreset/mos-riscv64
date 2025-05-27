#include <types.h>

#ifndef _ASM_RISCV_SBI_H
#define _ASM_RISCV_SBI_H

#define SBI_SUCCESS 0
#define SBI_ERR_FAILED -1
#define SBI_ERR_NOT_SUPPORTED -2
#define SBI_ERR_INVALID_PARAM -3
#define SBI_ERR_DENIED -4
#define SBI_ERR_INVALID_ADDRESS -5
#define SBI_ERR_ALREADY_AVAILABLE -6
#define SBI_ERR_ALREADY_STARTED -7
#define SBI_ERR_ALREADY_STOPPED -8
#define SBI_ERR_NO_SHMEM -9
#define SBI_ERR_INVALID_STATE -10
#define SBI_ERR_BAD_RANGE -11
#define SBI_ERR_TIMEOUT -12
#define SBI_ERR_IO -13

struct sbiret {
    reg_t error;
    reg_t value;
};

static inline struct sbiret riscv_sbicall(u_reg_t extension_id,
                                          u_reg_t function_id, u_reg_t arg0,
                                          u_reg_t arg1, u_reg_t arg2,
                                          u_reg_t arg3) {
    register u_reg_t a0 asm("a0") = (u_reg_t)(arg0);
    register u_reg_t a1 asm("a1") = (u_reg_t)(arg1);
    register u_reg_t a2 asm("a2") = (u_reg_t)(arg2);
    register u_reg_t a3 asm("a3") = (u_reg_t)(arg3);
    register u_reg_t a6 asm("a6") = (u_reg_t)(function_id);
    register u_reg_t a7 asm("a7") = (u_reg_t)(extension_id);
    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a6), "r"(a7)
                 : "memory");

    struct sbiret ret = {(reg_t)a0, (reg_t)a1};

    return ret;
}

// Debug Console Extension

#define DEBUG_CONSOLE_EXTENSION_ID 0x4442434E

#define DEBUG_CONSOLE_WRITE 0
#define DEBUG_CONSOLE_READ 1
#define DEBUG_CONSOLE_WRITE_BYTE 2

static inline struct sbiret sbi_debug_console_write(u_reg_t num_bytes,
                                                    u_reg_t base_addr_lo,
                                                    u_reg_t base_addr_hi) {
    return riscv_sbicall(DEBUG_CONSOLE_EXTENSION_ID, DEBUG_CONSOLE_WRITE,
                         num_bytes, base_addr_lo, base_addr_hi, 0);
}

static inline struct sbiret sbi_debug_console_read(u_reg_t num_bytes,
                                                   u_reg_t base_addr_lo,
                                                   u_reg_t base_addr_hi) {
    return riscv_sbicall(DEBUG_CONSOLE_EXTENSION_ID, DEBUG_CONSOLE_READ,
                         num_bytes, base_addr_lo, base_addr_hi, 0);
}

static inline struct sbiret sbi_debug_console_write_byte(uint8_t byte) {
    return riscv_sbicall(DEBUG_CONSOLE_EXTENSION_ID, DEBUG_CONSOLE_WRITE_BYTE,
                         byte, 0, 0, 0);
}

// System Reset Extension

#define SYSTEM_RESET_EXTENSION_ID 0x53525354

#define SYSTEM_RESET_RESET 0

#define RESET_TYPE_SHUTDOWN 0
#define RESET_TYPE_COLD_REBOOT 1
#define RESET_TYPE_WARM_REBOOT 2

static inline struct sbiret sbi_system_reset(uint32_t reset_type,
                                             uint32_t reset_reason) {
    return riscv_sbicall(SYSTEM_RESET_EXTENSION_ID, SYSTEM_RESET_RESET,
                         reset_type, reset_reason, 0, 0);
}

// Timer Extension

#define TIMER_EXTENSION_ID 0x54494D45

#define TIMER_SET_TIMER 0

static inline struct sbiret sbi_timer_set_timer(uint64_t next_tick) {
    return riscv_sbicall(TIMER_EXTENSION_ID, TIMER_SET_TIMER, next_tick, 0, 0,
                         0);
}

#endif