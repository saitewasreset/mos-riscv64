#include <device.h>
#include <device_tree.h>
#include <endian.h>
#include <kmalloc.h>
#include <mmu.h>
#include <pmap.h>
#include <printk.h>
#include <serial.h>
#include <string.h>

void serial_init(void) {
    debugk("serial_init", "init serial\n");

    struct device_node *serial_node = NULL;

    size_t serial_device_count =
        find_by_type(&device_tree, "serial", &serial_node, 1);

    if (serial_device_count == 0) {
        debugk("serial_init", "no serial device found\n");
        return;
    }

    struct SerialDeviceData device_data = {0};

    parse_serial_device(serial_node, &device_data);

    debugk(
        "serial_init",
        "%s clock frequency = %u interrupt = %x pa = 0x%016lx len = 0x%016lx\n",
        serial_node->name, device_data.clock_frequency,
        device_data.interrupt_id, device_data.begin_pa, device_data.len);

    register_serial_device(&device_data);

    debugk("serial_init", "serial init success\n");
}

int parse_serial_device(struct device_node *node,
                        struct SerialDeviceData *device_data) {
    int ret = 0;
    const struct property *compatible_property =
        get_property(node, "compatible");

    if (compatible_property == NULL) {
        debugk("parse_serial_device", "%s: no \"compatible\" property",
               node->name);

        return 1;
    }

    if (contains_string((const char *)compatible_property->value,
                        compatible_property->length, "ns16550a") == 0) {
        debugk("parse_serial_device", "%s: invalid compatible: ", node->name);

        print_stringlist((const char *)compatible_property->value,
                         compatible_property->length);

        printk("\n");

        return 1;
    }

    const struct property *interrupts_property =
        get_property(node, "interrupts");

    if (interrupts_property == NULL) {
        debugk("parse_serial_device", "%s: no \"interrupts\" property",
               node->name);

        return 1;
    }

    if (((interrupts_property->length % sizeof(uint32_t)) != 0) ||
        (interrupts_property->length == 0)) {
        debugk("parse_serial_device",
               "%s: invalid interrupts property length: %u\n", node->name,
               interrupts_property->length);

        return 1;
    }

    const uint32_t *interrupts_list_be =
        (const uint32_t *)interrupts_property->value;

    uint32_t interrupt_id = be32toh(interrupts_list_be[0]);

    const struct property *interrupt_parent_property =
        get_property(node, "interrupt-parent");

    if (interrupt_parent_property == NULL) {
        debugk("parse_serial_device", "%s: no \"interrupt-parent\" property",
               node->name);

        return 1;
    }

    uint32_t interrupt_parent_id =
        be32toh(*(uint32_t *)(interrupt_parent_property->value));

    if ((ret = get_reg_item(node, 0, &device_data->begin_pa,
                            &device_data->len)) != 0) {
        return ret;
    }

    device_data->interrupt_id = interrupt_id;
    device_data->interrupt_parent_id = interrupt_parent_id;

    const struct property *clock_frequency_property =
        get_property(node, "clock-frequency");

    if (clock_frequency_property == NULL) {
        debugk("parse_serial_device", "%s: no \"clock-frequency\" property",
               node->name);

        return 1;
    }

    if (clock_frequency_property->length != sizeof(uint32_t)) {
        debugk("parse_serial_device",
               "%s: invalid clock-frequency property length: %u\n", node->name,
               clock_frequency_property->length);

        return 1;
    }

    device_data->clock_frequency =
        be32toh(*(uint32_t *)clock_frequency_property->value);

    return 0;
}

void register_serial_device(struct SerialDeviceData *device_data) {
    struct SerialDeviceData *cloned =
        (struct SerialDeviceData *)kmalloc(sizeof(struct SerialDeviceData));

    memcpy(cloned, device_data, sizeof(struct SerialDeviceData));

    struct Device *temp_slot =
        add_device("serial", cloned, sizeof(struct SerialDeviceData));

    add_mmio_range(temp_slot, device_data->begin_pa, device_data->len);
}