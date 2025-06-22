#ifndef __SERIAL_H
#define __SERIAL_H

#include <device_tree.h>
#include <stdint.h>
#include <types.h>

struct SerialDeviceData {
    uint32_t interrupt_id;
    uint32_t interrupt_parent_id;

    uint32_t clock_frequency;

    u_reg_t begin_pa;
    size_t len;
};

void serial_init(void);
void register_serial_device(struct SerialDeviceData *device_data);
int parse_serial_device(struct device_node *node,
                        struct SerialDeviceData *device_data);

#endif