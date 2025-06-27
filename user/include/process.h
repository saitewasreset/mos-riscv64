#ifndef __USER_PROCESS_H
#define __USER_PROCESS_H

#include <env.h>

size_t get_process_list(size_t max_len, struct Process *out_process_list);
size_t find_process_by_name(const char *name, size_t max_len,
                            struct Process *out_process_list);

uint32_t get_envid(const char *name);

#endif