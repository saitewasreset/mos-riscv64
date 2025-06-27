#include "block.h"
#include "driver.h"
#include <device.h>
#include <lib.h>
#include <string.h>
#include <user_interrupt.h>
#include <virtio.h>
#include <virtioreq.h>

#define REQVA 0x6000000

static volatile bool in_progress = false;
uint32_t req_whom = 0;

uint32_t req_type = 0;

static void serve_read(uint32_t whom, struct VirtIOReqPayload *payload);
static void serve_write(uint32_t whom, struct VirtIOReqPayload *payload);

static void *serve_table[MAX_VIRTIOREQ] = {
    [VIRTIOREQ_READ] = serve_read, [VIRTIOREQ_WRITE] = serve_write};

u_reg_t base_addr[MAX_VIRTIO_COUNT] = {0};

static void *driver[MAX_DEVICE_ID] = {0};
static void *driver_interrupt[MAX_DEVICE_ID] = {0};

static void register_driver(void) {
    driver[BLOCK_DEVICE_ID] = init_block_device;
}

uint8_t read_virtio_dev_1b_unwrap(u_reg_t addr) {
    uint8_t val = 0;
    int ret = syscall_read_dev((u_reg_t)&val, addr, 1);

    if (ret != 0) {
        user_panic("read_virtio_dev_1b_unwrap: syscall read dev returned: %d "
                   "for addr 0x%016lx",
                   ret, addr);
    }

    return val;
}

void write_virtio_dev_1b_unwrap(u_reg_t addr, uint8_t val) {
    int ret = syscall_write_dev((u_reg_t)&val, addr, 1);

    if (ret != 0) {
        user_panic("write_virtio_dev_1b_unwrap: syscall write dev returned: %d "
                   "for addr 0x%016lx",
                   ret, addr);
    }
}

uint16_t read_virtio_dev_2b_unwrap(u_reg_t addr) {
    uint16_t val = 0;
    int ret = syscall_read_dev((u_reg_t)&val, addr, 2);

    if (ret != 0) {
        user_panic("read_virtio_dev_2b_unwrap: syscall read dev returned: %d "
                   "for addr 0x%016lx",
                   ret, addr);
    }

    return val;
}

void write_virtio_dev_2b_unwrap(u_reg_t addr, uint16_t val) {
    int ret = syscall_write_dev((u_reg_t)&val, addr, 2);

    if (ret != 0) {
        user_panic("write_virtio_dev_2b_unwrap: syscall write dev returned: %d "
                   "for addr 0x%016lx",
                   ret, addr);
    }
}

uint32_t read_virtio_dev_4b_unwrap(u_reg_t addr) {
    uint32_t val = 0;
    int ret = syscall_read_dev((u_reg_t)&val, addr, 4);

    if (ret != 0) {
        user_panic("read_virtio_dev_4b_unwrap: syscall read dev returned: %d "
                   "for addr 0x%016lx",
                   ret, addr);
    }

    return val;
}

void write_virtio_dev_4b_unwrap(u_reg_t addr, uint32_t val) {
    int ret = syscall_write_dev((u_reg_t)&val, addr, 4);

    if (ret != 0) {
        user_panic("write_virtio_dev_4b_unwrap: syscall write dev returned: %d "
                   "for addr 0x%016lx",
                   ret, addr);
    }
}

uint64_t read_virtio_dev_8b_unwrap(u_reg_t addr) {
    uint64_t val = 0;
    int ret = syscall_read_dev((u_reg_t)&val, addr, 8);

    if (ret != 0) {
        user_panic("read_virtio_dev_8b_unwrap: syscall read dev returned: %d "
                   "for addr 0x%016lx",
                   ret, addr);
    }

    return val;
}

void write_virtio_dev_8b_unwrap(u_reg_t addr, uint64_t val) {
    int ret = syscall_write_dev((u_reg_t)&val, addr, 8);

    if (ret != 0) {
        user_panic("write_virtio_dev_8b_unwrap: syscall write dev returned: %d "
                   "for addr 0x%016lx",
                   ret, addr);
    }
}

void virtio_dev_init(size_t virtio_device_idx) {
    struct UserDevice virtio_device = {0};
    struct VirtioDeviceData virtio_device_data = {0};

    int ret = syscall_get_device(
        "virtio_mmio", virtio_device_idx, sizeof(struct VirtioDeviceData),
        (u_reg_t)&virtio_device, (u_reg_t)&virtio_device_data);

    if (ret < 0) {
        debugf("virtio: cannot get virtio device %lu: %d\n", virtio_device_idx,
               ret);
        return;
    }

    base_addr[virtio_device_idx] = virtio_device_data.begin_pa;

    uint32_t magic_value = read_virtio_dev_4b_unwrap(
        base_addr[virtio_device_idx] + VIRTIO_MAGIC_VALUE);
    uint32_t version = read_virtio_dev_4b_unwrap(base_addr[virtio_device_idx] +
                                                 VIRTIO_VERSION);
    uint32_t device_id = read_virtio_dev_4b_unwrap(
        base_addr[virtio_device_idx] + VIRTIO_DEVICE_ID);
    uint32_t vendor_id = read_virtio_dev_4b_unwrap(
        base_addr[virtio_device_idx] + VIRTIO_VENDOR_ID);

    debugf("virtio: found device %lu: magic = 0x%08x version = %u device_id = "
           "0x%08x vendor_id = 0x%08x\n",
           virtio_device_idx, magic_value, version, device_id, vendor_id);

    if (magic_value != MAGIC_VALUE) {
        debugf("virtio: invalid magic 0x%08x for device %lu\n", magic_value,
               virtio_device_idx);
        return;
    }

    virtio_device_reset(virtio_device_idx);
    virtio_device_ack(virtio_device_idx);

    if ((device_id >= MAX_DEVICE_ID) || (driver[device_id] == NULL)) {
        debugf("virtio: no driver for device %lu\n", virtio_device_idx);
    } else {
        void *ptr = driver[device_id];
        bool (*driver)(size_t idx, uint32_t interrupt_code) =
            (bool (*)(size_t, uint32_t))ptr;

        driver(virtio_device_idx, virtio_device_data.interrupt_id);
    }
}

void virtio_device_reset(size_t virtio_device_idx) {
    write_virtio_dev_4b_unwrap(base_addr[virtio_device_idx] + VIRTIO_STATUS,
                               VIRTIO_STATUS_RESET);
}
void virtio_device_ack(size_t virtio_device_idx) {
    write_virtio_dev_4b_unwrap(base_addr[virtio_device_idx] + VIRTIO_STATUS,
                               VIRTIO_STATUS_ACKNOWLEDGE);
}

void virtio_device_driver(size_t virtio_device_idx) {
    write_virtio_dev_4b_unwrap(base_addr[virtio_device_idx] + VIRTIO_STATUS,
                               VIRTIO_STATUS_DRIVER);
}

void virtio_device_features_ok(size_t virtio_device_idx) {
    write_virtio_dev_4b_unwrap(base_addr[virtio_device_idx] + VIRTIO_STATUS,
                               VIRTIO_STATUS_FEATURES_OK);
}

void virtio_device_failed(size_t virtio_device_idx) {
    write_virtio_dev_4b_unwrap(base_addr[virtio_device_idx] + VIRTIO_STATUS,
                               VIRTIO_STATUS_FAILED);
}

bool validate_and_ack_feature_first_byte(size_t virtio_device_idx,
                                         uint32_t required_mask,
                                         uint32_t forbidden_mask) {
    u_reg_t current_base_addr = base_addr[virtio_device_idx];
    write_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_DEVICE_FEATURES_SEL,
                               0);

    uint32_t device_features =
        read_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_DEVICE_FEATURES);

    // device_features 中对应required_mask的位必须全为1

    bool success = true;

    if ((device_features & required_mask) != required_mask) {
        debugf("validate_and_ack_feature_first_byte: device %lu missing "
               "require feature, provided = %08x required = %08x\n",
               virtio_device_idx, device_features, required_mask);
        success = false;
    }

    // device_features 中对应forbidden_mask的位必须全为0

    if ((device_features & forbidden_mask) != 0) {
        debugf("validate_and_ack_feature_first_byte: device %lu having "
               "forbidden feature, provided = %08x forbidden = %08x\n",
               virtio_device_idx, device_features, forbidden_mask);
        success = false;
    }

    if (success) {
        write_virtio_dev_4b_unwrap(
            current_base_addr + VIRTIO_DRIVER_FEATURES_SEL, 0);
        write_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_DRIVER_FEATURES,
                                   required_mask);
        virtio_device_features_ok(virtio_device_idx);
    } else {
        virtio_device_failed(virtio_device_idx);
    }

    return success;
}

static char buffer[SECTOR_SIZE] = {0};

int main(void) {
    debugf("virtio: init virtio\n");

    register_driver();

    int device_count = syscall_get_device_count("virtio_mmio");

    if (device_count < 0) {
        user_panic("virtio: syscall_get_device_count returned: %d\n",
                   device_count);
    }

    debugf("virtio: found %d virtio device\n", device_count);

    for (int i = 0; i < device_count; i++) {
        virtio_dev_init((size_t)i);
    }

    debugf("virtio: WE SHALL NEVER SURRENDER!\n");

    uint32_t whom = 0;
    uint64_t val = 0;
    uint32_t perm = 0;

    void (*func)(uint32_t whom, struct VirtIOReqPayload *payload) = NULL;

    while (1) {

        int ret = ipc_recv(&whom, &val, (void *)REQVA, &perm);

        if (ret != 0) {
            if (ret == -E_INTR) {
                continue;
            } else {
                debugf("virtio: failed to receive request: %d\n", ret);
            }
        }

        if (val >= MAX_VIRTIOREQ) {
            debugf("virtio: invalid request code %lu from %08x\n", val, whom);

            ipc_send(whom, (uint64_t)-VIRTIOREQ_NO_FUNC, NULL, 0);

            panic_on(syscall_mem_unmap(0, (void *)REQVA));

            continue;
        }

        if (!(perm & PTE_V)) {
            debugf("virtio: invalid request from %08x: no argument page\n",
                   whom);
            ipc_send(whom, (uint64_t)-VIRTIOREQ_NO_PAYLOAD, NULL, 0);
            continue;
        }

        func = serve_table[val];

        func(whom, (struct VirtIOReqPayload *)REQVA);
    }
}

static void serve_read(uint32_t whom, struct VirtIOReqPayload *payload) {
    while (in_progress) {
    }

    in_progress = true;
    req_whom = whom;
    req_type = VIRTIO_BLK_T_IN;

    block_cmd(1, VIRTIO_BLK_T_IN, payload->sector, (void *)REQVA);
}

static void serve_write(uint32_t whom, struct VirtIOReqPayload *payload) {
    while (in_progress) {
    }

    in_progress = true;
    req_whom = whom;
    req_type = VIRTIO_BLK_T_OUT;

    block_cmd(1, VIRTIO_BLK_T_OUT, payload->sector, (void *)payload->buffer);
}

void notify_sender(bool success) {
    if (in_progress == false) {
        user_panic("notify_sender called while in_process == false");
    }

    if (!success) {
        ipc_send(req_whom, (uint64_t)-VIRTIOREQ_IOERROR, 0, 0);
    } else {
        if (req_type == VIRTIO_BLK_T_IN) {
            ipc_send(req_whom, VIRTIOREQ_SUCCESS, (void *)REQVA,
                     PTE_V | PTE_RW | PTE_USER);
        } else if (req_type == VIRTIO_BLK_T_OUT) {
            ipc_send(req_whom, VIRTIOREQ_SUCCESS, 0, 0);
        }
    }

    in_progress = false;
    panic_on(syscall_mem_unmap(0, (void *)REQVA));
}