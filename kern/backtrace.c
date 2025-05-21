#include <backtrace.h>
#include <printk.h>
#include <types.h>
#include <elf.h>

const char *lookup_function_name(size_t addr) {
    const char *function_name = NULL;

    size_t symbol_table_size = *(size_t *)((size_t)_super_info_start);
    size_t string_table_size = *(size_t *)((size_t)_super_info_start + sizeof(size_t));

    size_t string_table_end = (size_t)_strtab_start + string_table_size;

    size_t symbol_count = symbol_table_size / sizeof(Elf64_Sym);

    Elf64_Sym *symbol_table = (Elf64_Sym *)_symtab_start;

    for (size_t i = 0; i < symbol_count; i ++) {
        Elf64_Sym *current_symbol = &symbol_table[i];

        if (ELF64_ST_TYPE(current_symbol->st_info) == STT_FUNC) {
            size_t function_begin_va = current_symbol->st_value;
            size_t function_end_va = function_begin_va + current_symbol->st_size;

            if ((addr >= function_begin_va) && (addr < function_end_va)) {
                size_t symbol_name_va = (size_t)current_symbol->st_name + (size_t)_strtab_start;

                if (symbol_name_va < string_table_end) {
                    return (const char *)symbol_name_va;
                } else {
                    return NULL;
                }
            }
        }
    }

    return NULL;
}

void print_backtrace(void) {
    u_reg_t current_sp = 0;
    u_reg_t current_fp = 0;
    u_reg_t current_function = 0;

    asm volatile("auipc %0, 0\n" : "=r"(current_function));
    asm volatile("mv %0, sp" : "=r"(current_sp));
    asm volatile("mv %0, s0" : "=r"(current_fp));

    u_reg_t layer = 0;

    while (is_valid_stack_addr(current_fp)) {
        const char *current_function_name = lookup_function_name(current_function);

        if (current_function_name == NULL) {
            current_function_name = "?";
        }

        printk("%2lx: pc = 0x%016lx sp = 0x%016lx fp = 0x%016lx %s\n", layer,
               current_function, current_sp, current_fp, current_function_name);

        u_reg_t saved_ra = *(u_reg_t *)(current_fp - 8);
        u_reg_t saved_fp = *(u_reg_t *)(current_fp - 16);

        current_sp = current_fp;
        current_function = saved_ra - 4;
        current_fp = saved_fp;

        layer += 1;
    }
}