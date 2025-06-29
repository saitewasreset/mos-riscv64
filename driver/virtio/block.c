#include "block.h"
#include "driver.h"
#include "virtio.h"
#include <lib.h>
#include <user_interrupt.h>

extern void notify_sender(bool success);

BUILD_HANDLER(0)
BUILD_HANDLER(1)
BUILD_HANDLER(2)
BUILD_HANDLER(3)
BUILD_HANDLER(4)
BUILD_HANDLER(5)
BUILD_HANDLER(6)
BUILD_HANDLER(7)
BUILD_HANDLER(8)

void *block_interrupt_handler_list[MAX_BLOCK_DEVICE_COUNT + 1] = {
    HANDLER(0), HANDLER(1), HANDLER(2), HANDLER(3), HANDLER(4),
    HANDLER(5), HANDLER(6), HANDLER(7), HANDLER(8)};

// block device idx从1开始，0表示无映射
size_t block_device_idx_to_virtio_idx[MAX_VIRTIO_COUNT] = {0};

static size_t block_device_idx = 1;

static struct VirtQueueDesc queue_desc_area[MAX_BLOCK_DEVICE_COUNT]
                                           [MAX_QUEUE_SIZE]
    __attribute__((aligned(16)));

static struct VirtQueueAvail queue_avail_area[MAX_BLOCK_DEVICE_COUNT]
    __attribute__((aligned(2)));

static struct VirtQueueUsed queue_used_area[MAX_BLOCK_DEVICE_COUNT]
    __attribute__((aligned(4)));

static uint32_t queue_size_by_idx[MAX_BLOCK_DEVICE_COUNT];

static bool queue_desc_area_occupied[MAX_BLOCK_DEVICE_COUNT][MAX_QUEUE_SIZE] = {
    0};

static struct VirtIOBlockRequest request_buffer[MAX_BLOCK_DEVICE_COUNT]
                                               [MAX_QUEUE_SIZE] = {0};
static uint8_t status_buffer[MAX_BLOCK_DEVICE_COUNT][MAX_QUEUE_SIZE] = {0};

static uint16_t last_seen_idx[MAX_BLOCK_DEVICE_COUNT] = {0};

static uint16_t allocate_desc(size_t block_device_idx) {
    bool found = false;
    uint16_t result = 0;

    if (block_device_idx >= MAX_BLOCK_DEVICE_COUNT) {
        user_panic("allocate_desc: invalid block device id: %lu",
                   block_device_idx);
    }

    for (uint16_t i = 0; i < MAX_QUEUE_SIZE; i++) {
        if (queue_desc_area_occupied[block_device_idx][i] == false) {
            found = true;
            queue_desc_area_occupied[block_device_idx][i] = true;

            result = i;
            break;
        }
    }

    if (found == false) {
        user_panic("allocate_desc: no available queue descriptor for block "
                   "device id: %lu",
                   block_device_idx);
    }

    return result;
}

static void free_desc(size_t block_device_idx, uint16_t desc_id) {
    if (block_device_idx >= MAX_BLOCK_DEVICE_COUNT) {
        user_panic("free_desc: invalid block device id: %lu", block_device_idx);
    }

    if (desc_id >= MAX_QUEUE_SIZE) {
        user_panic("free_desc: invalid descriptor id: %u", desc_id);
    }

    queue_desc_area_occupied[block_device_idx][desc_id] = false;
}

static void init_last_seen_idx(size_t block_device_idx) {
    last_seen_idx[block_device_idx] = MAX_QUEUE_SIZE;
}

static bool handle_used(size_t block_device_idx, uint16_t used_idx);

bool init_block_device(size_t idx, uint32_t interrupt_code) {
    if (block_device_idx > MAX_BLOCK_DEVICE_COUNT) {
        debugf("init_block_device: %lu: too many block device\n", idx);
        return false;
    }

    virtio_device_driver(idx);

    u_reg_t current_base_addr = base_addr[idx];

    if (!validate_and_ack_feature_first_byte(idx, 0, (1 << VIRTIO_BLK_F_RO))) {
        debugf("init_block_device: %lu: validate feature failed\n", idx);
        return false;
    }

    uint32_t status =
        read_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_STATUS);

    if (status != VIRTIO_STATUS_FEATURES_OK) {
        debugf("init_block_device: %lu: unexpected device status: %u expected "
               "= %u\n",
               idx, status, VIRTIO_STATUS_FEATURES_OK);
        return false;
    }

    uint32_t capacity = read_virtio_dev_4b_unwrap(
        current_base_addr + VIRTIO_CONFIG + CONFIG_CAPACITY_OFFSET);

    write_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_QUEUE_SEL, 0);
    uint32_t max_queue_size =
        read_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_QUEUE_SIZE_MAX);

    uint32_t queue_size =
        max_queue_size < MAX_QUEUE_SIZE ? (max_queue_size) : (MAX_QUEUE_SIZE);

    debugf("init_block_device: %lu: capacity = %u sector max queue size = %u "
           "queue size = %u\n",
           idx, capacity, max_queue_size, queue_size);

    block_device_idx_to_virtio_idx[block_device_idx] = idx;

    queue_size_by_idx[block_device_idx] = queue_size;

    queue_avail_area[block_device_idx].flags = 0;
    queue_avail_area[block_device_idx].idx = 0;

    queue_used_area[block_device_idx].flags = 0;
    queue_used_area[block_device_idx].idx = 0;

    write_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_QUEUE_SIZE,
                               queue_size);

    u_reg_t queue_desc_pa =
        syscall_get_physical_address((void *)queue_desc_area[block_device_idx]);
    u_reg_t queue_avail_pa = syscall_get_physical_address(
        (void *)&queue_avail_area[block_device_idx]);
    u_reg_t queue_used_pa = syscall_get_physical_address(
        (void *)&queue_used_area[block_device_idx]);

    debugf("init_block_device: desc va = 0x%016lx avail va = 0x%016lx used va "
           "= 0x%016lx\n",
           queue_desc_area[block_device_idx],
           &queue_avail_area[block_device_idx],
           &queue_used_area[block_device_idx]);

    debugf("init_block_device: desc pa = 0x%016lx avail pa = 0x%016lx used pa "
           "= 0x%016lx\n",
           queue_desc_pa, queue_avail_pa, queue_used_pa);

    write_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_QUEUE_DESC_LOW,
                               (uint32_t)(queue_desc_pa & 0xFFFFFFFF));
    write_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_QUEUE_DESC_HIGH,
                               (uint32_t)((queue_desc_pa >> 32) & 0xFFFFFFFF));
    write_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_QUEUE_DRIVER_LOW,
                               (uint32_t)(queue_avail_pa & 0xFFFFFFFF));
    write_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_QUEUE_DRIVER_HIGH,
                               (uint32_t)((queue_avail_pa >> 32) & 0xFFFFFFFF));
    write_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_QUEUE_DEVICE_LOW,
                               (uint32_t)(queue_used_pa & 0xFFFFFFFF));
    write_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_QUEUE_DEVICE_HIGH,
                               (uint32_t)((queue_used_pa >> 32) & 0xFFFFFFFF));

    write_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_QUEUE_READY, 1);

    write_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_STATUS,
                               VIRTIO_STATUS_DRIVER_OK);

    status = read_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_STATUS);

    if (status != VIRTIO_STATUS_DRIVER_OK) {
        debugf("init_block_device: %lu: unexpected device status: %u expected: "
               "%u\n",
               idx, status, VIRTIO_STATUS_DRIVER_OK);
        return false;
    }

    debugf("init_block_device: %lu -> %lu: WE SHALL NEVER SURRENDER!\n", idx,
           block_device_idx);

    register_user_interrupt_handler(
        interrupt_code, block_interrupt_handler_list[block_device_idx]);

    block_device_idx++;

    init_last_seen_idx(block_device_idx);
    return true;
}

void block_cmd(size_t block_device_idx, uint32_t type, uint32_t sector,
               void *data) {
    uint16_t d1, d2, d3;

    uint32_t data_mode = 0;

    d1 = allocate_desc(block_device_idx);
    d2 = allocate_desc(block_device_idx);
    d3 = allocate_desc(block_device_idx);

    struct VirtIOBlockRequest *header = &request_buffer[block_device_idx][d1];
    uint8_t *status = &status_buffer[block_device_idx][d3];

    if ((type != VIRTIO_BLK_T_IN) && (type != VIRTIO_BLK_T_OUT)) {
        user_panic("block_cmd: invalid command type: %u", type);
    }

    header->type = type;
    header->sector = sector;

    queue_desc_area[block_device_idx][d1].len =
        sizeof(struct VirtIOBlockRequest);
    queue_desc_area[block_device_idx][d1].addr =
        syscall_get_physical_address((void *)header);
    queue_desc_area[block_device_idx][d1].flags = VIRTQ_DESC_F_NEXT;

    queue_desc_area[block_device_idx][d1].next = d2;

    if (type == VIRTIO_BLK_T_IN) {
        data_mode = VIRTQ_DESC_F_WRITE;
    }

    queue_desc_area[block_device_idx][d2].len = SECTOR_SIZE;
    queue_desc_area[block_device_idx][d2].addr =
        syscall_get_physical_address(data);
    queue_desc_area[block_device_idx][d2].flags = data_mode | VIRTQ_DESC_F_NEXT;

    queue_desc_area[block_device_idx][d2].next = d3;

    queue_desc_area[block_device_idx][d3].len = 1;
    queue_desc_area[block_device_idx][d3].addr =
        syscall_get_physical_address((void *)status);
    queue_desc_area[block_device_idx][d3].flags = VIRTQ_DESC_F_WRITE;

    queue_avail_area[block_device_idx]
        .ring[queue_avail_area[block_device_idx].idx] = d1;

    queue_avail_area[block_device_idx].idx =
        queue_avail_area[block_device_idx].idx + 1;

    u_reg_t current_base_addr =
        base_addr[block_device_idx_to_virtio_idx[block_device_idx]];

    write_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_QUEUE_NOTIFY, 0);
}

void handle_block_interrupt(size_t block_device_idx) {
    u_reg_t current_base_addr =
        base_addr[block_device_idx_to_virtio_idx[block_device_idx]];
    uint32_t interrupt_status =
        read_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_INTERRUPT_STATUS);

    if (((interrupt_status >> VIRTIO_BLK_INTERRUPT_STATUS_USED_BUFFER_OFFSET) &
         1) != 0) {
        uint16_t current = queue_used_area[block_device_idx].idx;
        uint16_t last = last_seen_idx[block_device_idx];

        bool success = true;

        if (current >= last) {
            for (uint16_t idx = last; idx < current; idx++) {
                success &= handle_used(block_device_idx, idx);
            }
        } else {
            for (uint16_t idx = last; idx < queue_size_by_idx[block_device_idx];
                 idx++) {
                success &= handle_used(block_device_idx, idx);
            }
            for (uint16_t idx = 0; idx < current; idx++) {
                success &= handle_used(block_device_idx, idx);
            }
        }

        last_seen_idx[block_device_idx] = current;

        notify_sender(success);
    }

    write_virtio_dev_4b_unwrap(current_base_addr + VIRTIO_INTERRUPT_ACK,
                               interrupt_status);
}

static bool handle_used(size_t block_device_idx, uint16_t used_idx) {
    uint16_t d1, d2, d3;

    d1 = queue_used_area[block_device_idx]
             .ring[used_idx % queue_size_by_idx[block_device_idx]]
             .id;

    if ((queue_desc_area[block_device_idx][d1].flags & VIRTQ_DESC_F_NEXT) ==
        0) {
        debugf(
            "handle_used: invalid d1 %u for block device id %lu used_idx %u: "
            "no VIRTQ_DESC_F_NEXT flag",
            d1, block_device_idx, used_idx);
        return false;
    }

    d2 = queue_desc_area[block_device_idx][d1].next;

    if ((queue_desc_area[block_device_idx][d2].flags & VIRTQ_DESC_F_NEXT) ==
        0) {
        debugf(
            "handle_used: invalid d2 %u for block device id %lu used_idx %u: "
            "no VIRTQ_DESC_F_NEXT flag",
            d2, block_device_idx, used_idx);
        return false;
    }

    d3 = queue_desc_area[block_device_idx][d2].next;

    if (queue_desc_area[block_device_idx][d1].len !=
        sizeof(struct VirtIOBlockRequest)) {
        debugf(
            "handle_used: invalid d1 %u for block device id %lu used_idx %u: "
            "invalid len %u, expected %zu",
            d1, block_device_idx, used_idx,
            queue_desc_area[block_device_idx][d1].len,
            sizeof(struct VirtIOBlockRequest));
        return false;
    }

    if (queue_desc_area[block_device_idx][d2].len != SECTOR_SIZE) {
        debugf(
            "handle_used: invalid d2 %u for block device id %lu used_idx %u: "
            "invalid len %u, expected %zu",
            d1, block_device_idx, used_idx,
            queue_desc_area[block_device_idx][d2].len, SECTOR_SIZE);
        return false;
    }

    if (queue_desc_area[block_device_idx][d3].len != 1) {
        debugf(
            "handle_used: invalid d3 %u for block device id %lu used_idx %u: "
            "invalid len %u, expected %zu",
            d1, block_device_idx, used_idx,
            queue_desc_area[block_device_idx][d3].len, 1);
        return false;
    }

    uint8_t *status = &status_buffer[block_device_idx][d3];

    if (*status != VIRTIO_BLK_S_OK) {
        debugf("handle_used: block device id %lu used_idx %u: bad status: %u",
               block_device_idx, used_idx, *status);
    }

    free_desc(block_device_idx, d1);
    free_desc(block_device_idx, d2);
    free_desc(block_device_idx, d3);

    return true;
}