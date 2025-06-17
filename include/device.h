#ifndef __DEVICE_H
#define __DEVICE_H

#include <stdint.h>
#include <types.h>

#define DEVICE_TYPE_LEN 32
#define DEVICE_ARRAY_RESIZE_FACTOR 2

extern struct DeviceArray devices;

struct DeviceMMIORange {
    u_reg_t pa;
    size_t len;

    struct DeviceMMIORange *next;
};

struct Device {
    char device_type[DEVICE_TYPE_LEN];
    uint64_t device_id;
    struct DeviceMMIORange *mmio_range_list;
    void *device_data;
};

struct DeviceArray {
    struct Device *array;
    size_t len;
    size_t capacity;
};

struct Device *add_device(char *device_type, void *device_data);

void add_mmio_range(struct Device *target_device, u_reg_t pa, size_t len);

void dump_device();

#endif