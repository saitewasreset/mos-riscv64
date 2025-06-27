#include <lib.h>
#include <string.h>
#include <user_serial.h>

const char *message =
    "serial_test: For Super Earth!\nNot Today!\nWE SHALL NEVER SURRENDER!\n";

int main(void) {
    size_t message_len = strlen(message);

    serial_write(message, message_len);

    size_t recv_len = 0;

    char buffer[512] = {0};

    debugf("buffer at 0x%016lx\n", buffer);

    while (1) {
        while (recv_len == 0) {
            recv_len = serial_read(buffer, 511);
        }

        debugf("recv_len = %lu\n", recv_len);

        for (size_t i = 0; i < recv_len; i++) {
            debugf("%c ", buffer[i]);
        }

        recv_len = 0;
    }

    return 0;
}