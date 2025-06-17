#ifndef __VIRTIO_H
#define __VIRTIO_H

#include <device_tree.h>
#include <stdint.h>
#include <types.h>

#define MAX_VIRTIO_COUNT 64

typedef volatile struct __attribute__((packed)) {
    uint32_t MagicValue;
    uint32_t Version;
    uint32_t DeviceID;
    uint32_t VendorID;
    uint32_t DeviceFeatures;
    uint32_t DeviceFeaturesSel;
    uint32_t _reserved0[2];
    uint32_t DriverFeatures;
    uint32_t DriverFeaturesSel;
    uint32_t _reserved1[2];
    uint32_t QueueSel;
    uint32_t QueueNumMax;
    uint32_t QueueNum;
    uint32_t _reserved2[2];
    uint32_t QueueReady;
    uint32_t _reserved3[2];
    uint32_t QueueNotify;
    uint32_t _reserved4[3];
    uint32_t InterruptStatus;
    uint32_t InterruptACK;
    uint32_t _reserved5[2];
    uint32_t Status;
    uint32_t _reserved6[3];
    uint32_t QueueDescLow;
    uint32_t QueueDescHigh;
    uint32_t _reserved7[2];
    uint32_t QueueAvailLow;
    uint32_t QueueAvailHigh;
    uint32_t _reserved8[2];
    uint32_t QueueUsedLow;
    uint32_t QueueUsedHigh;
    uint32_t _reserved9[21];
    uint32_t ConfigGeneration;
    uint32_t Config[0];
} virtio_regs;

struct virtio_device_data {
    uint32_t interrupt_id;
    uint32_t interrupt_parent_id;

    u_reg_t begin_pa;
    size_t len;
};

void virtio_init(void);
int parse_virtio_device(struct device_node *node,
                        struct virtio_device_data *device_data);

#endif