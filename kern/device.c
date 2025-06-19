#include "mmu.h"
#include <device.h>
#include <kmalloc.h>
#include <kmmap.h>
#include <printk.h>
#include <string.h>

// 不能返回指向`devices`的指针！因为`devices`是动态数组
// 可能随时被重分配到其它位置！
struct DeviceArray devices = {NULL, 0, 0};

static uint64_t global_device_id = 0;

static uint64_t get_next_device_id() { return global_device_id++; }

static void print_mmio_range(struct DeviceMMIORange *mmio_range_list);

static void *get_mapped_pa(struct Device *device, u_reg_t pa);

// 对设备列表进行任何修改操作后，返回的指针就可能失效！！
struct Device *add_device(char *device_type, void *device_data) {
    if (devices.len >= devices.capacity) {
        if (devices.capacity == 0) {
            devices.capacity = DEVICE_ARRAY_RESIZE_FACTOR;
        } else {
            devices.capacity *= DEVICE_ARRAY_RESIZE_FACTOR;
        }

        struct Device *new_array =
            (struct Device *)kmalloc(devices.capacity * sizeof(struct Device));

        if (new_array == NULL) {
            panic("add_device: failed to allocate memory for resize");
        }

        if (devices.array != NULL) {
            memcpy(new_array, devices.array,
                   devices.len * sizeof(struct Device));
            kfree(devices.array);
        }

        devices.array = new_array;
    }

    struct Device *target_slot = &devices.array[devices.len];

    strcpy(target_slot->device_type, device_type);
    target_slot->device_id = get_next_device_id();
    target_slot->mmio_range_list = NULL;
    target_slot->device_data = device_data;

    devices.len++;

    return target_slot;
}

void add_mmio_range(struct Device *target_device, u_reg_t pa, size_t len) {
    struct DeviceMMIORange *range =
        (struct DeviceMMIORange *)kmalloc(sizeof(struct DeviceMMIORange));

    range->pa = pa;
    range->len = len;

    range->mapped_va = (u_reg_t)kmmap_alloc(pa, ROUND(len, PAGE_SIZE),
                                            PTE_V | PTE_RW | PTE_GLOBAL);

    if (range->mapped_va == 0) {
        panic("add_mmio_range: cannot allocate mapped va for device id: %lu",
              target_device->device_id);
    }

    range->next = target_device->mmio_range_list;

    target_device->mmio_range_list = range;
}

static void print_mmio_range(struct DeviceMMIORange *mmio_range_list) {
    struct DeviceMMIORange *current = mmio_range_list;

    while (current != NULL) {
        printk("[0x%016lx, 0x%016lx) -> 0x%016lx", current->pa,
               current->pa + current->len, current->mapped_va);

        current = current->next;
    }
}

size_t find_device_by_type(char *device_type, struct Device *out_devices,
                           size_t max_count) {
    size_t count = 0;

    if (max_count == 0) {
        return 0;
    }

    for (size_t i = 0; i < devices.len; i++) {
        struct Device *current_device = &devices.array[i];

        if (strcmp(current_device->device_type, device_type) == 0) {
            out_devices[count] = *current_device;

            count++;

            if (count >= max_count) {
                break;
            }
        }
    }

    return count;
}

void dump_device() {
    for (size_t i = 0; i < devices.len; i++) {
        struct Device *current_device = &devices.array[i];

        printk("%02lu: type = %s id = %lu &data = 0x%016lx\n", i,
               current_device->device_type, current_device->device_id,
               current_device->device_data);

        printk("  MMIO: ");
        print_mmio_range(current_device->mmio_range_list);
        printk("\n");
    }
}

// 若pa非法（不在`device`允许的范围内，将panic）
static void *get_mapped_pa(struct Device *device, u_reg_t pa) {
    void *result = NULL;

    struct DeviceMMIORange *current_range = device->mmio_range_list;

    while (current_range != NULL) {
        if ((pa >= current_range->pa) &&
            (pa < current_range->pa + current_range->len)) {
            u_reg_t offset = pa - current_range->pa;

            result = (void *)(current_range->mapped_va + offset);

            break;
        }

        current_range = current_range->next;
    }

    if (result == NULL) {
        panic("get_mapped_pa: invalid mmio pa 0x%016lx for device id %u", pa,
              device->device_id);
    }

    return result;
}

// 若pa非法（不在`device`允许的范围内，将panic）
uint8_t ioread8(struct Device *device, u_reg_t paddr) {
    return *(volatile uint8_t *)get_mapped_pa(device, paddr);
}

// 若pa非法（不在`device`允许的范围内，将panic）
uint16_t ioread16(struct Device *device, u_reg_t paddr) {
    return *(volatile uint16_t *)get_mapped_pa(device, paddr);
}

// 若pa非法（不在`device`允许的范围内，将panic）
uint32_t ioread32(struct Device *device, u_reg_t paddr) {
    void *va = get_mapped_pa(device, paddr);
    return *(volatile uint32_t *)va;
}

// 若pa非法（不在`device`允许的范围内，将panic）
uint64_t ioread64(struct Device *device, u_reg_t paddr) {
    return *(volatile uint64_t *)get_mapped_pa(device, paddr);
}

// 若pa非法（不在`device`允许的范围内，将panic）
void iowrite8(struct Device *device, uint8_t data, u_reg_t paddr) {
    *(volatile uint8_t *)get_mapped_pa(device, paddr) = data;
}

// 若pa非法（不在`device`允许的范围内，将panic）
void iowrite16(struct Device *device, uint16_t data, u_reg_t paddr) {
    *(volatile uint16_t *)get_mapped_pa(device, paddr) = data;
}

// 若pa非法（不在`device`允许的范围内，将panic）
void iowrite32(struct Device *device, uint32_t data, u_reg_t paddr) {
    *(volatile uint32_t *)get_mapped_pa(device, paddr) = data;
}

// 若pa非法（不在`device`允许的范围内，将panic）
void iowrite64(struct Device *device, uint64_t data, u_reg_t paddr) {
    *(volatile uint64_t *)get_mapped_pa(device, paddr) = data;
}