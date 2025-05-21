#include </usr/include/elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdint.h>

int main(int argc, char *argv[]) {
    if (argc <= 4) {
        printf("Usage: %s <input file> <symbol table binary> <string table binary> <super info binary>\n", argv[0]);
        return 1;
    }

    const char *filename = argv[1];

    const char *symtab_filename = argv[2];

    const char *strtab_filename = argv[3];

    const char *super_info_filename = argv[4];

    struct stat file_info = {0};

    int ret = 0;

    if ((ret = stat(filename, &file_info)) < 0) {
        perror("stat: ");
    }

    void *binary = malloc(file_info.st_size);

    FILE *f = fopen(filename, "r");
    fread(binary, sizeof(char), file_info.st_size, f);
    fclose(f);

    Elf64_Ehdr *header = (Elf64_Ehdr *)binary; 

    size_t section_offset = header->e_shoff;
    size_t section_count = header->e_shnum;

    Elf64_Shdr *section_header = (Elf64_Shdr *)((size_t)binary + section_offset);

    size_t symtab_offset = 0;
    size_t symtab_size = 0;

    size_t strtab_offset = 0;
    size_t strtab_size = 0;

    for (size_t i = 0; i < section_count; i ++) {
        Elf64_Shdr* current_section = &section_header[i];

        if (current_section->sh_type == SHT_SYMTAB) {
            symtab_offset = current_section->sh_offset;
            symtab_size = current_section->sh_size;
        } 
        
        if ((current_section->sh_type == SHT_STRTAB) && (strtab_size == 0)) {
            strtab_offset = current_section->sh_offset;
            strtab_size = current_section->sh_size;
        }
    }

    if (symtab_size == 0) {
        printf("No symtab found!\n");
        return 1;
    }

    if (strtab_size == 0) {
        printf("No strtab found!\n");
        return 1;
    }

    FILE *symtab_file = fopen(symtab_filename, "w");
    FILE *strtab_file = fopen(strtab_filename, "w");
    FILE *super_info_file = fopen(super_info_filename, "w");

    if (symtab_file == NULL) {
        printf("Cannot open symtab_file\n");
        return 1;
    }

    if (strtab_file == NULL) {
        printf("Cannot open strtab_file\n");
        return 1;
    }

    uint64_t super_info_buf[2] = {symtab_size, strtab_size};

    fwrite((void *)((size_t)binary + symtab_offset), sizeof(char), symtab_size, symtab_file);
    fwrite((void *)((size_t)binary + strtab_offset), sizeof(char), strtab_size, strtab_file);
    fwrite((void *)super_info_buf, sizeof(super_info_buf), 1, super_info_file);

    fclose(symtab_file);
    fclose(strtab_file);
    fclose(super_info_file);

    free(binary);
    return 0;
}