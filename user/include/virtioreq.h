#ifndef __VIRTIO_REQ_H
#define __VIRTIO_REQ_H

#include <stdint.h>

#define SECTOR_SIZE 512

#define VIRTIOREQ_SUCCESS 0
#define VIRTIOREQ_IOERROR 1
// 请求号不合法
#define VIRTIOREQ_NO_FUNC 2
// 没有发送请求体
#define VIRTIOREQ_NO_PAYLOAD 3

enum {
    VIRTIOREQ_READ,
    VIRTIOREQ_WRITE,
    MAX_VIRTIOREQ,
};

struct VirtIOReqPayload {
    uint32_t sector;
    char buffer[512];
};

#endif