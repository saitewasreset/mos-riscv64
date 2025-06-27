#include <lib.h>
#include <string.h>
#include <user_virtio.h>
#include <virtioreq.h>

static char buffer[SECTOR_SIZE] = {0};

void dump_sector(const char *buf);

int main(void) {
    debugf("virtiotest: begin test\n");
    int ret = virtio_read_sector(0, (void *)buffer);

    if (ret != VIRTIOREQ_SUCCESS) {
        user_panic("virtiotest: virtio_read_sector returned %d", ret);
    }

    debugf("virtiotest: dump sector\n");
    dump_sector(buffer);

    return 0;
}

void dump_sector(const char *buf) {
    for (size_t i = 0; i < SECTOR_SIZE; i++) {
        if (buf[i] == '\0') {
            break;
        }

        debugf("%c", buf[i]);
    }
}