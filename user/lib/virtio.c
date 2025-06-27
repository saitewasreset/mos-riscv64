#include <lib.h>
#include <mmu.h>
#include <process.h>
#include <string.h>
#include <user_virtio.h>
#include <virtioreq.h>

char virtioipcbuf[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

static uint32_t virtio_service_envid = 0;

static void set_virtio_service_envid();

int virtio_read_sector(uint32_t sector, void *buf) {
    set_virtio_service_envid();

    struct VirtIOReqPayload *payload = (struct VirtIOReqPayload *)virtioipcbuf;

    payload->sector = sector;

    ipc_send(virtio_service_envid, VIRTIOREQ_READ, (const void *)virtioipcbuf,
             PTE_V | PTE_RW | PTE_USER);

    uint64_t ret = 0;
    uint32_t whom = 0;
    uint32_t perm = 0;

    ipc_recv(&whom, &ret, (void *)virtioipcbuf, &perm);

    memcpy((void *)buf, (const void *)virtioipcbuf, SECTOR_SIZE);

    return (int)ret;
}
int virtio_write_sector(uint32_t sector, const char *buf) {
    set_virtio_service_envid();

    struct VirtIOReqPayload *payload = (struct VirtIOReqPayload *)virtioipcbuf;

    payload->sector = sector;

    memcpy(payload->buffer, buf, SECTOR_SIZE);

    ipc_send(virtio_service_envid, VIRTIOREQ_WRITE, (const void *)virtioipcbuf,
             PTE_V | PTE_RW | PTE_USER);

    uint64_t val = 0;
    uint32_t whom = 0;

    return (int)ipc_recv(&whom, &val, NULL, 0);
}

static void set_virtio_service_envid() {
    while (virtio_service_envid == 0) {
        virtio_service_envid = get_envid("virtio");
    }
}