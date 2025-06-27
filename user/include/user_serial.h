#ifndef __USER_SERIAL_H
#define __USER_SERIAL_H

#include <stddef.h>

size_t serial_read(char *buf, size_t len);
int serial_write(const char *buf, size_t len);

#endif