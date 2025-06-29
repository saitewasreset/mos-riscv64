/*
 * operations on IDE disk.
 */

#include "serv.h"
#include "virtioreq.h"
#include <lib.h>
#include <mmu.h>
#include <user_virtio.h>

// 向VirtIO驱动发送请求，读取指定的块
void sector_read(uint32_t secno, void *dst, uint32_t nsecs) {
    for (uint32_t cur = secno; cur < secno + nsecs; cur++) {
        int ret = virtio_read_sector(cur, dst);

        if (ret != VIRTIOREQ_SUCCESS) {
            user_panic("block_read: virtio_read_sector returned %d", ret);
        }

        dst = (void *)((u_reg_t)dst + SECTOR_SIZE);
    }
}

// 向VirtIO驱动发送请求，写入指定的块
void sector_write(uint32_t secno, void *src, uint32_t nsecs) {
    for (uint32_t cur = secno; cur < secno + nsecs; cur++) {
        int ret = virtio_write_sector(cur, src);

        if (ret != VIRTIOREQ_SUCCESS) {
            user_panic("block_read: virtio_write_sector returned %d", ret);
        }

        src = (void *)((u_reg_t)src + SECTOR_SIZE);
    }
}
