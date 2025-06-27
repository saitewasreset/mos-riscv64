#ifndef __DRIVER_VIRTIO_BLOCK_H
#define __DRIVER_VIRTIO_BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <virtioreq.h>

#define MAX_QUEUE_SIZE 512
#define MAX_BLOCK_DEVICE_COUNT 8

#define BUILD_HANDLER(idx)                                                     \
    static void handle_interrupt_##idx(void) { handle_block_interrupt(idx); }

#define HANDLER(idx) handle_interrupt_##idx
#define CALL_HANDLER(idx) handle_interrupt_##idx()

struct VirtQueueDesc {
    // 物理地址
    uint64_t addr;
    uint32_t len;
/* This marks a buffer as continuing via the next field. */
#define VIRTQ_DESC_F_NEXT 1
/* This marks a buffer as device write-only (otherwise device read-only). */
#define VIRTQ_DESC_F_WRITE 2
/* This means the buffer contains a list of buffer descriptors. */
#define VIRTQ_DESC_F_INDIRECT 4
    /* The flags as indicated above. */
    uint16_t flags;
    /* Next field if flags & NEXT */
    uint16_t next;
} __attribute__((packed));

struct VirtQueueAvail {
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[MAX_QUEUE_SIZE];
} __attribute__((packed));

struct VirtQueueUsedElement {
    /* Index of start of used descriptor chain. */
    uint32_t id;
    /*
     * The number of bytes written into the device writable portion of
     * the buffer described by the descriptor chain.
     */
    uint32_t len;
} __attribute__((packed));

struct VirtQueueUsed {
#define VIRTQ_USED_F_NO_NOTIFY 1
    uint16_t flags;
    uint16_t idx;
    struct VirtQueueUsedElement ring[MAX_QUEUE_SIZE];
} __attribute__((packed));

#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1
#define VIRTIO_BLK_T_FLUSH 4
#define VIRTIO_BLK_T_GET_ID 8
#define VIRTIO_BLK_T_GET_LIFETIME 10
#define VIRTIO_BLK_T_DISCARD 11
#define VIRTIO_BLK_T_WRITE_ZEROES 13
#define VIRTIO_BLK_T_SECURE_ERASE 14

struct VirtIOBlockRequest {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

struct __attribute__((packed)) virtio_blk_config {
    // 0 8B
    uint64_t capacity;
    // 8 4B
    uint32_t size_max;
    // 12 4B
    uint32_t seg_max;
    // 16 4B
    struct virtio_blk_geometry {
        // 16 2B
        uint16_t cylinders;
        // 18 1B
        uint8_t heads;
        // 19 1B
        uint8_t sectors;
    } geometry;
    // 20 4B
    uint32_t blk_size;
    // 24 8B
    struct virtio_blk_topology {
        // # of logical blocks per physical block (log2)
        // 24 1B
        uint8_t physical_block_exp;
        // offset of first aligned logical block
        // 25 1B
        uint8_t alignment_offset;
        // suggested minimum I/O size in blocks
        // 26 2B
        uint16_t min_io_size;
        // optimal (suggested maximum) I/O size in blocks
        // 28 4B
        uint32_t opt_io_size;
    } topology;
    // 32 1B
    uint8_t writeback;
    // 33 1B
    uint8_t unused0;
    // 34 2B
    uint16_t num_queues;
    // 36 4B
    uint32_t max_discard_sectors;
    // 40 4B
    uint32_t max_discard_seg;
    // 44 4B
    uint32_t discard_sector_alignment;
    // 48 4B
    uint32_t max_write_zeroes_sectors;
    // 52 4B
    uint32_t max_write_zeroes_seg;
    // 56 1B
    uint8_t write_zeroes_may_unmap;
    // 57 3B
    uint8_t unused1[3];
    // 60 4B
    uint32_t max_secure_erase_sectors;
    // 64 4B
    uint32_t max_secure_erase_seg;
    // 68 4B
    uint32_t secure_erase_sector_alignment;
    // 72 24B
    struct virtio_blk_zoned_characteristics {
        // 72 4B
        uint32_t zone_sectors;
        // 76 4B
        uint32_t max_open_zones;
        // 80 4B
        uint32_t max_active_zones;
        // 84 4B
        uint32_t max_append_sectors;
        // 88 4B
        uint32_t write_granularity;
        // 89 1B
        uint8_t model;
        // 90 3B
        uint8_t unused2[3];
    } zoned;
};

#define CONFIG_CAPACITY_OFFSET 0
#define CONFIG_SIZE_MAX_OFFSET 8
#define CONFIG_SEG_MAX_OFFSET 12
#define CONFIG_GEOMETRY_OFFSET 16
#define CONFIG_BLK_SIZE_OFFSET 20
#define CONFIG_TOPOLOGY_OFFSET 24
#define CONFIG_WRITEBACK_OFFSET 32
#define CONFIG_UNUSED0_OFFSET 33
#define CONFIG_NUM_QUEUES_OFFSET 34
#define CONFIG_MAX_DISCARD_SECTORS_OFFSET 36
#define CONFIG_MAX_DISCARD_SEG_OFFSET 40
#define CONFIG_DISCARD_SECTOR_ALIGNMENT_OFFSET 44
#define CONFIG_MAX_WRITE_ZEROES_SECTORS_OFFSET 48
#define CONFIG_MAX_WRITE_ZEROES_SEG_OFFSET 52
#define CONFIG_WRITE_ZEROES_MAY_UNMAP_OFFSET 56
#define CONFIG_UNUSED1_OFFSET 57
#define CONFIG_MAX_SECURE_ERASE_SECTORS_OFFSET 60
#define CONFIG_MAX_SECURE_ERASE_SEG_OFFSET 64
#define CONFIG_SECURE_ERASE_SECTOR_ALIGNMENT_OFFSET 68
#define CONFIG_ZONED_OFFSET 72

// Maximum size of any single segment is in size_max
#define VIRTIO_BLK_F_SIZE_MAX 1

// Maximum number of segments in a request is in seg_max
#define VIRTIO_BLK_F_SEG_MAX 2

// Disk-style geometry specified in geometry
#define VIRTIO_BLK_F_GEOMETRY 4

// Device is read-only
#define VIRTIO_BLK_F_RO 5

// Block size of disk is in blk_size
#define VIRTIO_BLK_F_BLK_SIZE 6

// Cache flush command support
#define VIRTIO_BLK_F_FLUSH 9

// Device exports information on optimal I/O alignment
#define VIRTIO_BLK_F_TOPOLOGY 10

// Device can toggle its cache between writeback and writethrough modes
#define VIRTIO_BLK_F_CONFIG_WCE 11

// Device supports multiqueue
#define VIRTIO_BLK_F_MQ 12

// Device can support discard command, maximum discard sectors size in
// max_discard_sectors and maximum discard segment number in max_discard_seg
#define VIRTIO_BLK_F_DISCARD 13

// Device can support write zeroes command, maximum write zeroes
// sectors size in max_write_zeroes_sectors and maximum write zeroes segment
// number in max_write_zeroes_seg
#define VIRTIO_BLK_F_WRITE_ZEROES 14

// Device supports providing storage lifetime information
#define VIRTIO_BLK_F_LIFETIME 15

// Device supports secure erase command, maximum erase sectors
// count in max_secure_erase_sectors and maximum erase segment number in
// max_secure_erase_seg
#define VIRTIO_BLK_F_SECURE_ERASE 16

// Device is a Zoned Block Device, that is, a device that follows the zoned
// storage device behavior that is also supported by industry standards such as
// the T10 Zoned Block Command standard (ZBC r05) or the NVMe(TM) NVM Express
// Zoned Namespace Command Set Specification 1.1b (ZNS)
#define VIRTIO_BLK_F_ZONED 17

#define VIRTIO_BLK_S_OK 0
#define VIRTIO_BLK_S_IOERR 1
#define VIRTIO_BLK_S_UNSUPP 2

#define VIRTIO_BLK_INTERRUPT_STATUS_USED_BUFFER_OFFSET 0

bool init_block_device(size_t idx, uint32_t interrupt_code);

void handle_block_interrupt(size_t idx);
void block_cmd(size_t block_device_idx, uint32_t type, uint32_t sector,
               void *data);

#endif