#include "error.h"
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
    uint32_t self = syscall_getenvid();
    set_virtio_service_envid();

    int ipc_ret = 0;

    struct VirtIOReqPayload *payload = (struct VirtIOReqPayload *)virtioipcbuf;

    payload->sector = sector;

    while (1) {
        while ((ipc_ret = syscall_ipc_try_send(
                    virtio_service_envid, VIRTIOREQ_READ,
                    (const void *)virtioipcbuf, PTE_V | PTE_RW | PTE_USER)) ==
               -E_IPC_NOT_RECV) {
            continue;
        }

        uint64_t ret = 0;
        uint32_t whom = 0;
        uint32_t perm = 0;

        ipc_ret = ipc_recv(virtio_service_envid, &whom, &ret,
                           (void *)virtioipcbuf, &perm);

        if (ipc_ret == 0) {
            break;
        } else {
            // 若该次请求被中断，重新请求
            if (ipc_ret == -E_INTR) {
                continue;
            } else {
                break;
            }
        }
    }

    u_reg_t pa = syscall_get_physical_address(virtioipcbuf);

    memcpy((void *)buf, (const void *)virtioipcbuf, SECTOR_SIZE);

    return 0;
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

    int result = (int)ipc_recv(virtio_service_envid, &whom, &val, NULL, 0);

    return result;
}

static void set_virtio_service_envid() {
    while (virtio_service_envid == 0) {
        virtio_service_envid = get_envid("virtio");
    }
}