#include <device.h>
#include <lib.h>
#include <virtio.h>

int main(void) {
    debugf("Getting virtio_mmio device count\n");
    int virtio_device_count = syscall_get_device_count("virtio_mmio");

    if (virtio_device_count < 0) {
        user_panic("syscall_get_device_count returned: %d",
                   virtio_device_count);
    }

    debugf("Getting virtio_mmio device\n");

    for (int i = 0; i < virtio_device_count; i++) {
        struct UserDevice device = {0};
        struct virtio_device_data device_data = {0};

        int ret = syscall_get_device("virtio_mmio", (size_t)i,
                                     sizeof(struct virtio_device_data),
                                     (u_reg_t)&device, (u_reg_t)&device_data);

        if (ret < 0) {
            user_panic("syscall_get_device returned: %d", ret);
        }

        debugf(
            "%2d: id = %lu mmio_range_list_len = %lu device_data_len = %lu\n",
            i, device.device_id, device.mmio_range_list_len,
            device.device_data_len);

        debugf("  ");

        for (size_t j = 0; j < device.mmio_range_list_len; j++) {
            struct UserDeviceMMIORange mmio_range = device.mmio_range_list[j];

            debugf("[0x%016lx, 0x%016lx) ", mmio_range.pa,
                   mmio_range.pa + mmio_range.len);
        }

        debugf("\n");

        debugf("  interrupt_id = %u interrupt_parent_id = %u begin_pa = "
               "0x%016lx len = 0x%016lx\n",
               device_data.interrupt_id, device_data.interrupt_parent_id,
               device_data.begin_pa, device_data.len);
    }

    debugf("WE SHALL NEVER SURRENDER!\n");
}