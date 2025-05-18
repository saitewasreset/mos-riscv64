#include <elf.h>
#include <pmap.h>

/*
 * 概述：
 *
 *   验证输入的二进制数据是否为有效的ELF可执行文件，并返回其ELF头结构指针。
 *   检查包括ELF魔数、文件头完整性以及文件类型是否为可执行文件（ET_EXEC）。
 *
 * Precondition：
 * - binary必须指向有效的二进制数据，且不为NULL
 * - size参数必须为二进制数据的长度
 *
 * Postcondition：
 * -
 * 若binary是合法的ELF可执行文件（魔数正确且e_type为ET_EXEC），返回指向其ELF头的指针
 * - 否则返回NULL表示非法ELF文件或非可执行类型
 *
 * 副作用：
 * - 无
 */
const Elf32_Ehdr *elf_from(const void *binary, size_t size) {
    const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)binary;
    if (size >= sizeof(Elf32_Ehdr) && ehdr->e_ident[EI_MAG0] == ELFMAG0 &&
        ehdr->e_ident[EI_MAG1] == ELFMAG1 &&
        ehdr->e_ident[EI_MAG2] == ELFMAG2 &&
        ehdr->e_ident[EI_MAG3] == ELFMAG3 && ehdr->e_type == 2) {
        return ehdr;
    }
    return NULL;
}

/*
 * 概述：
 *   加载一个 ELF 程序段到内存中。
 * 根据程序头的信息，通过回调函数将段的文件内容及内存空间映射到对应的虚拟地址。
 *   处理文件大小与内存大小不一致的情况（如 .bss 段），确保分配足够的页面。
 *
 *   实验中使用的`map_page`回调函数为`load_icode_mapper`。
 *
 *   `elf_mapper_t`的参数含义：
 *
 *  - `data`：传递给回调函数的上下文数据（如环境指针）。
 *  - `va`：要映射的虚拟地址(无需按页对齐)，用于指定要映射到哪个页中。
 *  -
 `offset`：在页内的偏移量，`src`中长`len`的数据将从页中的`offset`处开始映射。
 *  - `perm`：映射的页的标志位设置。
 *  - `src`：若不为空，从此处开始读取长`len`的数据进行映射；
 *           若为空，则填充**整个页**为0。
 *  - `len`：要映射的长度。
 *
 * 实现差异：
 * 在原实现中，对于`va`未对齐页面边界的情况，
 * 将i设置为i = MIN(bin_size, PAGE_SIZE - offset)k
 * 这能保证在之后的循环中，访问`bin + i`时不会越界
 * 但是，当`bin_size`较小时，`va + i`并不指向下一页
 * 而是仍然指向已经映射的当前页，导致页面被重复映射
 * （具体地，这是在`while (i < sgsize)`循环中发生的）
 * 这里我们将i初始化为`PAGE_SIZE - offset`，即使`bin_size`较小
 * 因为在`for (; i < bin_size; i += PAGE_SIZE)`循环中
 * 已经判断了`i < bin_size`，这可以保证访问`bin + i`时不会越界
 * 同时避免了重复映射当前页，导致数据丢失的问题
 *
 * Precondition：
 * - `ph` 必须指向有效的 Elf32_Phdr 结构体，描述待加载的程序段。
 * - `bin` 指向的缓冲区必须包含该段的文件内容，且其大小至少为 `ph->p_filesz`。
 * - `map_page` 回调函数必须能正确处理页面映射请求（如权限检查、物理页分配）。
 * - `data` 参数必须符合 `map_page` 回调函数所需的上下文。
 *
 * Postcondition：
 * - 成功时返回 0，表示所有页面已通过回调函数正确加载；
 *   失败返回负数（由回调函数或内部错误决定）。
 * - 程序段的文件内容被映射到 `ph->p_vaddr` 开始的虚拟地址，内存大小扩展至
 * `ph->p_memsz`。
 *
 * 副作用：
 * - 调用 `map_page` 回调函数多次，可能修改页表、分配物理页或改变内存映射状态。
 * - 若 `ph->p_memsz >
 * ph->p_filesz`，额外分配的页面内容未初始化（依赖回调函数的实现）。
 */
// Checked by DeepSeek R1 20250507 1716
int elf_load_seg(Elf32_Phdr *ph, const void *bin, elf_mapper_t map_page,
                 void *data) {
    u_long va = ph->p_vaddr;
    size_t bin_size = ph->p_filesz;
    size_t sgsize = ph->p_memsz;
    u_int perm = PTE_V;
    if (ph->p_flags & PF_W) {
        perm |= PTE_D;
    }

    int r;
    size_t i;
    // 检查程序段要映射到的虚拟地址`va`是否对齐页面边界
    // `offset`包含了`va`到页面边界的偏移量
    u_long offset = va - ROUNDDOWN(va, PAGE_SIZE);
    if (offset != 0) {
        // 单独处理未对齐的部分
        if ((r = map_page(data, va, offset, perm, bin,
                          MIN(bin_size, PAGE_SIZE - offset))) != 0) {
            return r;
        }
    }

    /* Step 1: load all content of bin into memory. */
    // 映射剩余的页面：在二进制文件中存在的部分
    // i表示(当前映射的页面的开始位置)距离(该段头部)的偏移量
    if (offset == 0) {
        i = 0;
    } else {
        // 实现差异：
        // 该情况表示存在未对齐部分，且未对齐部分已经映射了一页
        // 我们不能再次映射该页，即，`va + i`必须指向下一页

        // 若`bin_size`较大，则i = `PAGE_SIZE - offset`，`va + i`一定指向下一页
        // 若`bin_size`较小，**则下面的代码无法保证该不变量**
        // 原实现：
        // i = MIN(bin_size, PAGE_SIZE - offset);

        // 这里我们直接将i初始化为`PAGE_SIZE - offset`，即使`bin_size`较小
        // 也不会影响后续的映射，因为下面的循环中有`i < bin_size`的判断
        // 修改实现：
        i = PAGE_SIZE - offset;
        // 这时`va + i`一定指向下一页
    }

    for (; i < bin_size; i += PAGE_SIZE) {
        if ((r = map_page(data, va + i, 0, perm, bin + i,
                          MIN(bin_size - i, PAGE_SIZE))) != 0) {
            return r;
        }
    }

    /* Step 2: alloc pages to reach `sgsize` when `bin_size` < `sgsize`. */
    // 映射剩余的页面：在二进制文件中不存在的部分 -> 初始化为0
    while (i < sgsize) {
        if ((r = map_page(data, va + i, 0, perm, NULL,
                          MIN(sgsize - i, PAGE_SIZE))) != 0) {
            return r;
        }
        i += PAGE_SIZE;
    }
    return 0;
}
