#include <lib.h>
#include <mmu.h>
#include <process.h>
#include <serialreq.h>
#include <string.h>
#include <user_serial.h>

char serialipcbuf[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

static uint32_t serial_service_envid = 0;

static void set_serial_service_envid();

size_t serial_read(char *buf, size_t len) {
    set_serial_service_envid();

    if (len > MAX_PAYLOAD_SIZE) {
        user_panic("serial_read: payload to long: %lu\n", len);
    }

    struct SerialReqPayload *payload = (struct SerialReqPayload *)serialipcbuf;

    payload->max_len = len;

    ipc_send(serial_service_envid, SERIALREQ_READ, (const void *)serialipcbuf,
             PTE_V | PTE_RW | PTE_USER);

    uint64_t actual_read = 0;
    uint32_t whom = 0;
    uint32_t perm = 0;

    ipc_recv(&whom, &actual_read, (void *)serialipcbuf, &perm);

    memcpy((void *)buf, (const void *)serialipcbuf, actual_read);

    return actual_read;
}
int serial_write(const char *buf, size_t len) {
    set_serial_service_envid();

    if (len > MAX_PAYLOAD_SIZE) {
        user_panic("serial_write: payload to long: %lu\n", len);
    }

    struct SerialReqPayload *payload = (struct SerialReqPayload *)serialipcbuf;

    payload->max_len = len;

    memcpy(payload->buf, buf, len);

    ipc_send(serial_service_envid, SERIALREQ_WRITE, (const void *)serialipcbuf,
             PTE_V | PTE_RW | PTE_USER);

    uint64_t val = 0;
    uint32_t whom = 0;

    return (int)ipc_recv(&whom, &val, NULL, 0);
}

static void set_serial_service_envid() {
    while (serial_service_envid == 0) {
        serial_service_envid = get_envid("serial");
    }
}