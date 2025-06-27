#ifndef __USER_VIRTIO_H
#define __USER_VIRTIO_H

#include <stddef.h>
#include <stdint.h>

int virtio_read_sector(uint32_t sector, void *buf);
int virtio_write_sector(uint32_t sector, const char *buf);

#endif