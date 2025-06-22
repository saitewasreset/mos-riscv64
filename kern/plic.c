#include <device.h>
#include <device_tree.h>
#include <endian.h>
#include <interrupt.h>
#include <kmalloc.h>
#include <plic.h>
#include <printk.h>
#include <stdint.h>
#include <string.h>

static plic_interrupt_handler_func *interrupt_handler = NULL;

// 由`register_plic_device`函数设置
static struct Device *plic_device = NULL;
// 由`plic_init`函数设置
static u_reg_t plic_base_pa = 0;
// 由`plic_init`函数设置
static uint32_t plic_interrupt_count = 0;

void plic_init(void) {
    debugk("plic_init", "init plic\n");

    struct device_node *plic_node_list[1] = {0};

    size_t plic_count = find_by_type(&device_tree, "plic", plic_node_list, 1);

    if (plic_count == 0) {
        debugk("plic_init", "no plic found!\n");
        return;
    }

    struct PlicData plic_data = {0};

    parse_plic_device(plic_node_list[0], &plic_data);

    register_plic_device(&plic_data);

    plic_base_pa = plic_data.base_pa;
    plic_interrupt_count = plic_data.interrupt_count;

    debugk("plic_init",
           "begin pa = 0x%016lx len = 0x%016lx interrupt_count = %u "
           "s_interrupt = %lu\n",
           plic_data.base_pa, plic_data.len, plic_data.interrupt_count,
           plic_data.s_interrupt_code);

    // 初始化interrupt handler
    interrupt_handler = (plic_interrupt_handler_func *)kmalloc(
        sizeof(plic_interrupt_handler_func *) * plic_data.interrupt_count);

    memset(interrupt_handler, 0, sizeof(void *) * plic_data.interrupt_count);

    register_interrupt_handler(plic_data.s_interrupt_code,
                               handle_plic_interrupt);
    enable_interrupt(plic_data.s_interrupt_code);

    plic_set_prority_threshold(0);

    debugk("plic_init", "plic init success\n");
}

void register_plic_device(struct PlicData *device_data) {
    struct PlicData *cloned =
        (struct PlicData *)kmalloc(sizeof(struct PlicData));

    memcpy(cloned, device_data, sizeof(struct PlicData));

    struct Device *temp_slot =
        add_device("plic", cloned, sizeof(struct PlicData));

    plic_device = (struct Device *)kmalloc(sizeof(struct Device));

    add_mmio_range(temp_slot, device_data->base_pa, device_data->len);

    *plic_device = *temp_slot;
}

int parse_plic_device(struct device_node *node, struct PlicData *device_data) {
    int ret = 0;

    struct property *compatible_property = get_property(node, "compatible");

    if (compatible_property == NULL) {
        debugk("parse_plic_device", "%s: no \"compatible\" property\n",
               node->name);

        return 1;
    }

    if (contains_string((const char *)compatible_property->value,
                        compatible_property->length,
                        "sifive,plic-1.0.0") == 0) {
        debugk("parse_plic_device", "%s: invalid compatible: ", node->name);

        print_stringlist((const char *)compatible_property->value,
                         compatible_property->length);

        printk("\n");

        return 1;
    }

    struct property *interrupt_count_property =
        get_property(node, "riscv,ndev");

    if (interrupt_count_property == NULL) {
        debugk("parse_plic_device", "%s: no \"riscv,ndev\" property\n",
               node->name);

        return 1;
    }

    if (interrupt_count_property->length != sizeof(uint32_t)) {
        debugk("parse_plic_device",
               "%s: invalid \"riscv,ndev\" property length, expected: %lu "
               "actual: %u\n",
               node->name, sizeof(uint32_t), interrupt_count_property->length);

        return 1;
    }

    uint32_t interrupt_count =
        be32toh(*(uint32_t *)interrupt_count_property->value);

    device_data->interrupt_count = interrupt_count;

    if ((ret = get_reg_item(node, 0, &device_data->base_pa,
                            &device_data->len)) != 0) {
        return ret;
    }

    struct property *interrupt_extended_property =
        get_property(node, "interrupts-extended");

    if (interrupt_extended_property == NULL) {
        debugk("parse_plic_device", "%s: no \"interrupts-extended\" property\n",
               node->name);

        return 1;
    }

    if (interrupt_extended_property->length == 16) {
        device_data->s_interrupt_code =
            be32toh(*(uint32_t *)(((size_t)interrupt_extended_property->value) +
                                  3 * sizeof(uint32_t)));
    } else if (interrupt_extended_property->length == 8) {
        device_data->s_interrupt_code =
            be32toh(*(uint32_t *)(((size_t)interrupt_extended_property->value) +
                                  sizeof(uint32_t)));
    } else {
        debugk("parse_plic_device",
               "%s: invalid \"interrupts-extended\" length: %lu\n", node->name,
               interrupt_extended_property->length);

        return 1;
    }

    if (device_data->s_interrupt_code >= 64) {
        debugk("parse_plic_device", "%s: invalid interrupt code: %lu\n",
               node->name, device_data->s_interrupt_code);

        return 1;
    }

    return 0;
}

uint32_t plic_get_prority_threshold(void) {
    if (plic_device == NULL) {
        panic("plic_get_prority_threshold called while plic_device == NULL");
    }

    return ioread32(plic_device,
                    plic_base_pa + PLIC_PRIORITY_THRESHOLD_CONTEXT_1_OFFSET);
}
void plic_set_prority_threshold(uint32_t threshold) {
    if (plic_device == NULL) {
        panic("plic_set_prority_threshold called while plic_device == NULL");
    }

    iowrite32(plic_device, threshold,
              plic_base_pa + PLIC_PRIORITY_THRESHOLD_CONTEXT_1_OFFSET);
}

uint32_t plic_get_interrupt_code(void) {
    if (plic_device == NULL) {
        panic("plic_get_interrupt_code called while plic_device == NULL");
    }

    return ioread32(plic_device, plic_base_pa + PLIC_CLAIM_CONTEXT_1_OFFSET);
}
void plic_mark_finish(uint32_t interrupt_code) {
    if (plic_device == NULL) {
        panic("plic_mark_finish called while plic_device == NULL");
    }

    if (interrupt_code >= plic_interrupt_count) {
        panic(
            "plic_mark_finish: invalid interrupt code: %u interrupt count: %u",
            interrupt_code, plic_interrupt_count);
    }

    iowrite32(plic_device, interrupt_code,
              plic_base_pa + PLIC_CLAIM_CONTEXT_1_OFFSET);
}

void plic_enable_interrupt(uint32_t interrupt_code, uint32_t prority,
                           plic_interrupt_handler_func handler) {
    if (plic_device == NULL) {
        panic("plic_enable_interrupt called while plic_device == NULL");
    }

    if (interrupt_code >= plic_interrupt_count) {
        panic("plic_enable_interrupt: invalid interrupt code: %u interrupt "
              "count: %u",
              interrupt_code, plic_interrupt_count);
    }

    interrupt_handler[interrupt_code] = handler;

    uint32_t byte_offset = interrupt_code / 32;
    uint32_t offset = interrupt_code % 32;

    u_reg_t enable_register_va =
        plic_base_pa + PLIC_INTERRUPT_ENABLE_CONTEXT_1_OFFSET + byte_offset;

    uint32_t previous = ioread32(plic_device, enable_register_va);

    iowrite32(plic_device, previous | (1 << offset), enable_register_va);

    plic_set_prority(interrupt_code, prority);
}

void plic_set_prority(uint32_t interrupt_code, uint32_t prority) {
    if (plic_device == NULL) {
        panic("plic_set_prority called while plic_device == NULL");
    }

    if (interrupt_code >= plic_interrupt_count) {
        panic(
            "plic_set_prority: invalid interrupt code: %u interrupt count: %u",
            interrupt_code, plic_interrupt_count);
    }

    u_reg_t prority_register_va = plic_base_pa +
                                  PLIC_INTERRUPT_SOURCE_PRIORIY_OFFSET +
                                  interrupt_code * 4;

    iowrite32(plic_device, prority, prority_register_va);
}

void handle_plic_interrupt(struct Trapframe *tf) {
    if (plic_device == NULL) {
        panic("handle_plic_interrupt called while plic_device == NULL");
    }

    uint32_t interrupt_code = plic_get_interrupt_code();

    if (interrupt_code >= plic_interrupt_count) {
        panic("handle_plic_interrupt: invalid interrupt_code: %u interrupt "
              "count: %u",
              interrupt_code, plic_interrupt_count);
    }

    if (interrupt_handler[interrupt_code] != NULL) {
        interrupt_handler[interrupt_code](tf);
    } else {
        debugk("handle_plic_interrupt", "no handler for interrupt code: %u!\n",
               interrupt_code);
    }

    plic_mark_finish(interrupt_code);
}

uint32_t plic_get_interrupt_count(void) {
    if (plic_device == NULL) {
        panic("plic_get_interrupt_count called while plic_device == NULL");
    }

    return plic_interrupt_count;
}