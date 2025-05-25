#ifndef _BACK_TRACE_H_
#define _BACK_TRACE_H_

#include <mmu.h>
#include <stdbool.h>
#include <types.h>

extern char _super_info_start[];
extern char _symtab_start[];
extern char _strtab_start[];

const char *lookup_function_name(size_t addr);
void print_backtrace(void);

static inline bool is_valid_stack_addr(u_reg_t va) {
    return (va >= KSTACKBOTTOM) &&
           (va <= KSTACKTOP);
}

#endif