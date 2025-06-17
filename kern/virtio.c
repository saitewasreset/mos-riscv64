#include <device.h>
#include <device_tree.h>
#include <endian.h>
#include <kmalloc.h>
#include <mmu.h>
#include <pmap.h>
#include <printk.h>
#include <string.h>
#include <virtio.h>

void virtio_init(void) {
    debugk("virtio_init", "init virtio\n");

    debugk("virtio_init", "max virtio device count: %d\n", MAX_VIRTIO_COUNT);

    struct device_node *virtio_node_list[MAX_VIRTIO_COUNT] = {0};

    size_t virtio_device_count = find_by_type(
        &device_tree, "virtio_mmio", virtio_node_list, MAX_VIRTIO_COUNT);

    debugk("virtio_init", "found %lu virtio device\n", virtio_device_count);

    for (size_t i = 0; i < virtio_device_count; i++) {
        struct device_node *current_node = virtio_node_list[i];

        struct virtio_device_data device_data = {0};

        parse_virtio_device(current_node, &device_data);

        debugk("virtio_init",
               "virtio %02lu: %s interrupt = %x pa = 0x%016lx len = 0x%016lx\n",
               i, current_node->name, device_data.interrupt_id,
               device_data.begin_pa, device_data.len);

        register_virtio_device(&device_data);
    }

    debugk("virtio_init", "virtio init success\n");
}

int parse_virtio_device(struct device_node *node,
                        struct virtio_device_data *device_data) {
    int ret = 0;
    const struct property *compatible_property =
        get_property(node, "compatible");

    if (compatible_property == NULL) {
        debugk("parse_virtio_device", "%s: no \"compatible\" property",
               node->name);

        return 1;
    }

    if (contains_string((const char *)compatible_property->value,
                        compatible_property->length, "virtio,mmio") == 0) {
        debugk("parse_virtio_device", "%s: invalid compatible: ", node->name);

        print_stringlist((const char *)compatible_property->value,
                         compatible_property->length);

        printk("\n");

        return 1;
    }

    const struct property *interrupts_property =
        get_property(node, "interrupts");

    if (interrupts_property == NULL) {
        debugk("parse_virtio_device", "%s: no \"interrupts\" property",
               node->name);

        return 1;
    }

    if (((interrupts_property->length % sizeof(uint32_t)) != 0) ||
        (interrupts_property->length == 0)) {
        debugk("parse_virtio_device",
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
        debugk("parse_virtio_device", "%s: no \"interrupt-parent\" property",
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

    return 0;
}

void register_virtio_device(struct virtio_device_data *device_data) {
    struct virtio_device_data *cloned =
        (struct virtio_device_data *)kmalloc(sizeof(struct virtio_device_data));

    memcpy(cloned, device_data, sizeof(struct virtio_device_data));

    struct Device *slot = add_device("virtio_mmio", cloned);

    add_mmio_range(slot, device_data->begin_pa, device_data->len);
}