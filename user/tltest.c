#include <lib.h>

int main() {
    debugf("Smashing some kernel codes...\n"
           "If your implementation is correct, you may see unknown exception "
           "here:\n");
    *(u_reg_t *)BASE_ADDR_IMM = 0;
    debugf("My mission completed!\n");
    return 0;
}
