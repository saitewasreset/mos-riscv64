#ifndef __PLIC_H
#define __PLIC_H

#include <device_tree.h>
#include <stdint.h>
#include <trap.h>
#include <types.h>

#define PLIC_INTERRUPT_SOURCE_PRIORIY_OFFSET 0x000000
#define PLIC_INTERRUPT_PENDING_OFFSET 0x001000

#define PLIC_INTERRUPT_ENABLE_CONTEXT_0_OFFSET 0x002000

#define PLIC_INTERRUPT_ENABLE_CONTEXT_1_OFFSET 0x002080

#define PLIC_PRIORITY_THRESHOLD_CONTEXT_0_OFFSET 0x200000
#define PLIC_CLAIM_CONTEXT_0_OFFSET 0x200004

#define PLIC_PRIORITY_THRESHOLD_CONTEXT_1_OFFSET 0x201000
#define PLIC_CLAIM_CONTEXT_1_OFFSET 0x201004

struct PlicData {
    uint32_t interrupt_count;

    // 发送到S模式的中断码
    u_reg_t s_interrupt_code;

    u_reg_t base_pa;
    size_t len;
};

typedef void (*plic_interrupt_handler_func)(struct Trapframe *tf);

// 本函数将同时允许所有PLIC中断（设置threshold = 0）
void plic_init(void);
void register_plic_device(struct PlicData *device_data);
int parse_plic_device(struct device_node *node, struct PlicData *device_data);
void handle_plic_interrupt(struct Trapframe *tf);

uint32_t plic_get_interrupt_count(void);

uint32_t plic_get_prority_threshold(void);
void plic_set_prority_threshold(uint32_t threshold);

uint32_t plic_get_interrupt_code(void);
void plic_mark_finish(uint32_t interrupt_code);

void plic_enable_interrupt(uint32_t interrupt_code, uint32_t prority,
                           plic_interrupt_handler_func handler);

void plic_set_prority(uint32_t interrupt_code, uint32_t prority);
#endif