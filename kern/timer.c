#include <stdint.h>
#include <timer.h>

void set_next_timer_interrupt(u_reg_t next_tick) {
    sbi_timer_set_timer((uint64_t)next_tick);
}