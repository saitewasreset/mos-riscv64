#ifndef __DEVICE_H
#define __DEVICE_H

#include <stdint.h>
#include <types.h>

#define DEVICE_TYPE_LEN 32
#define DEVICE_ARRAY_RESIZE_FACTOR 2

#define DEVICE_USER_MMIO_ARRAY_LEN 32

extern struct DeviceArray devices;

struct DeviceMMIORange {
    u_reg_t pa;
    size_t len;

    u_reg_t mapped_va;

    struct DeviceMMIORange *next;
};

struct Device {
    char device_type[DEVICE_TYPE_LEN];
    uint64_t device_id;
    struct DeviceMMIORange *mmio_range_list;
    void *device_data;
    size_t device_data_len;
};

struct DeviceArray {
    struct Device *array;
    size_t len;
    size_t capacity;
};

struct UserDeviceMMIORange {
    u_reg_t pa;
    size_t len;
};

struct UserDevice {
    char device_type[DEVICE_TYPE_LEN];
    uint64_t device_id;
    struct UserDeviceMMIORange mmio_range_list[DEVICE_USER_MMIO_ARRAY_LEN];
    size_t mmio_range_list_len;
    size_t device_data_len;
};

// 对设备列表进行任何修改操作后，返回的指针就可能失效！！
struct Device *add_device(char *device_type, void *device_data,
                          size_t device_data_len);

void add_mmio_range(struct Device *target_device, u_reg_t pa, size_t len);

size_t find_device_by_type(char *device_type, struct Device *out_devices,
                           size_t max_count);

size_t get_device_count(char *device_type);

int user_find_device_by_type(char *device_type, size_t idx, size_t max_data_len,
                             struct UserDevice *out_device,
                             void *out_device_data);

void dump_device();

// 若pa非法（不在`device`允许的范围内，将panic）
uint8_t ioread8(struct Device *device, u_reg_t paddr);
// 若pa非法（不在`device`允许的范围内，将panic）
uint16_t ioread16(struct Device *device, u_reg_t paddr);
// 若pa非法（不在`device`允许的范围内，将panic）
uint32_t ioread32(struct Device *device, u_reg_t paddr);
// 若pa非法（不在`device`允许的范围内，将panic）
uint64_t ioread64(struct Device *device, u_reg_t paddr);
// 若pa非法（不在`device`允许的范围内，将panic）
void iowrite8(struct Device *device, uint8_t data, u_reg_t paddr);
// 若pa非法（不在`device`允许的范围内，将panic）
void iowrite16(struct Device *device, uint16_t data, u_reg_t paddr);
// 若pa非法（不在`device`允许的范围内，将panic）
void iowrite32(struct Device *device, uint32_t data, u_reg_t paddr);
// 若pa非法（不在`device`允许的范围内，将panic）
void iowrite64(struct Device *device, uint64_t data, u_reg_t paddr);

#endif