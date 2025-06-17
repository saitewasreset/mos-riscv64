#include <device.h>
#include <kmalloc.h>
#include <printk.h>
#include <string.h>

struct DeviceArray devices = {NULL, 0, 0};

static uint64_t global_device_id = 0;

static uint64_t get_next_device_id() { return global_device_id++; }

static void print_mmio_range(struct DeviceMMIORange *mmio_range_list);

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

    range->next = target_device->mmio_range_list;

    target_device->mmio_range_list = range;
}

static void print_mmio_range(struct DeviceMMIORange *mmio_range_list) {
    struct DeviceMMIORange *current = mmio_range_list;

    while (current != NULL) {
        printk("[0x%016lx, 0x%016lx) ", current->pa,
               current->pa + current->len);

        current = current->next;
    }
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