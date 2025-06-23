#ifndef __USER_INTERRUPT_H
#define __USER_INTERRUPT_H

#include <stdint.h>

void register_user_interrupt_handler(uint32_t interrupt_code,
                                     void (*interrupt_handler)(void));

#endif