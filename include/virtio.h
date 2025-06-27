#ifndef __VIRTIO_H
#define __VIRTIO_H

#include <device_tree.h>
#include <stdint.h>
#include <types.h>

#define MAX_VIRTIO_COUNT 64

struct VirtioDeviceData {
    uint32_t interrupt_id;
    uint32_t interrupt_parent_id;

    u_reg_t begin_pa;
    size_t len;
};

void virtio_init(void);
void register_virtio_device(struct VirtioDeviceData *device_data);
int parse_virtio_device(struct device_node *node,
                        struct VirtioDeviceData *device_data);

#endif