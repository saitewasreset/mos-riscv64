#ifndef __SERIAL_REQ_H
#define __SERIAL_REQ_H

#include <mmu.h>
#include <stddef.h>

#define SERIALREQ_SUCCESS 0
// 发送的数据长度过大（PAGE_SIZE）
#define SERIALREQ_INVAL 1
// 请求号不合法
#define SERIALREQ_NO_FUNC 2
// 没有发送请求体
#define SERIALREQ_NO_PAYLOAD 3

#define MAX_PAYLOAD_SIZE PAGE_SIZE - sizeof(size_t)

enum {
    SERIALREQ_READ,
    SERIALREQ_WRITE,
    MAX_SERIALREQNO,
};

struct SerialReqPayload {
    size_t max_len;
    char buf[MAX_PAYLOAD_SIZE];
};

#endif