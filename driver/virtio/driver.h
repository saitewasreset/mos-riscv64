#ifndef __DRIVER_VIRTIO_H
#define __DRIVER_VIRTIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <virtio.h>

#define MAX_DEVICE_ID 64
#define BLOCK_SIZE 512
#define MAGIC_VALUE 0x74726976

#define VIRTIO_MAGIC_VALUE 0x000
#define VIRTIO_VERSION 0x004
#define VIRTIO_DEVICE_ID 0x008
#define VIRTIO_VENDOR_ID 0x00c
#define VIRTIO_DEVICE_FEATURES 0x010
#define VIRTIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_DRIVER_FEATURES 0x020
#define VIRTIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_QUEUE_SEL 0x030
#define VIRTIO_QUEUE_SIZE_MAX 0x034
#define VIRTIO_QUEUE_SIZE 0x038
#define VIRTIO_QUEUE_READY 0x044
#define VIRTIO_QUEUE_NOTIFY 0x050
#define VIRTIO_INTERRUPT_STATUS 0x060
#define VIRTIO_INTERRUPT_ACK 0x064
#define VIRTIO_STATUS 0x070
#define VIRTIO_QUEUE_DESC_LOW 0x080
#define VIRTIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_QUEUE_DRIVER_LOW 0x090
#define VIRTIO_QUEUE_DRIVER_HIGH 0x094
#define VIRTIO_QUEUE_DEVICE_LOW 0x0a0
#define VIRTIO_QUEUE_DEVICE_HIGH 0x0a4
#define VIRTIO_CONFIG_GENERATION 0x0fc
#define VIRTIO_CONFIG 0x100

#define BLOCK_DEVICE_ID 2

#define VIRTIO_STATUS_RESET 0
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_FAILED 128
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_NEEDS_RESET 64

extern u_reg_t base_addr[MAX_VIRTIO_COUNT];

void virtio_dev_init(size_t virtio_device_idx);

void virtio_device_reset(size_t virtio_device_idx);
void virtio_device_ack(size_t virtio_device_idx);
void virtio_device_driver(size_t virtio_device_idx);
void virtio_device_features_ok(size_t virtio_device_idx);
void virtio_device_failed(size_t virtio_device_idx);

uint8_t read_virtio_dev_1b_unwrap(u_reg_t addr);
void write_virtio_dev_1b_unwrap(u_reg_t addr, uint8_t val);
uint16_t read_virtio_dev_2b_unwrap(u_reg_t addr);
void write_virtio_dev_2b_unwrap(u_reg_t addr, uint16_t val);
uint32_t read_virtio_dev_4b_unwrap(u_reg_t addr);
void write_virtio_dev_4b_unwrap(u_reg_t addr, uint32_t val);
uint64_t read_virtio_dev_8b_unwrap(u_reg_t addr);
void write_virtio_dev_8b_unwrap(u_reg_t addr, uint64_t val);

bool validate_and_ack_feature_first_byte(size_t virtio_device_idx,
                                         uint32_t required_mask,
                                         uint32_t forbidden_mask);

#endif