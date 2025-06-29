#include "fd.h"
#include <fs.h>
#include <lib.h>

#define debug 0

static int file_close(struct Fd *fd);
static int file_read(struct Fd *fd, void *buf, u_int n, u_int offset);
static int file_write(struct Fd *fd, const void *buf, u_int n, u_int offset);
static int file_stat(struct Fd *fd, struct Stat *stat);

// Dot represents choosing the member within the struct declaration
// to initialize, with no need to consider the order of members.
struct Dev devfile = {
    .dev_id = 'f',
    .dev_name = "file",
    .dev_read = file_read,
    .dev_write = file_write,
    .dev_close = file_close,
    .dev_stat = file_stat,
};

/*
 * 概述：
 *   以模式`mode`打开`path`处的文件，返回文件描述符。
 *   分配文件描述符并返回给调用进程（内存共享）。
 *
 *   注意：存储文件描述符的页实际上与文件系统服务进程共享。
 *
 *   若指定O_CREAT且文件不存在则创建，
 *   若指定O_TRUNC则清空文件内容。
 *
 *   将在调用进程的虚拟地址空间中建立文件内容按逻辑顺序的映射。
 *   具体地，从`FILE_BASE`开始处，按照文件描述符计算合适的
 *   4MB内存空间，用以按逻辑顺序映射文件内容。该映射与文件系统
 *   服务中的缓存共享物理页，但该映射按文件逻辑顺序，而文件系统
 *   服务中的缓存按磁盘块顺序。
 *
 * Precondition：
 *   - `path`不能长于MAXPATHLEN=1024
 *   - 全局opentab必须已通过serve_init初始化
 *
 * Postcondition：
 *   - 成功：返回文件描述符编号（>=0）
 *   - 若无可用的文件描述符，返回-E_MAX_OPEN
 *   - 若文件系统服务无可用打开文件条目，返回-E_MAX_OPEN
 *   - 若分配用于共享Filefd的结构体时内存不足，返回-E_NO_MEM
 *   - 若路径过长，返回-E_BAD_PATH
 *   - 若不创建文件，路径不存在，返回-E_NOT_FOUND
 *   - 若创建文件，若中间目录不存在，返回-E_NOT_FOUND
 *   - 若创建文件，为目录文件分配间接指针存储块时无空闲块，返回-E_NO_DISK
 *   -
 若创建文件，为目录文件分配间接指针存储块进行缓存时，分配内存失败，返回-E_NO_MEM
 *   - 若创建文件，为目录文件分配新的存储块时无空闲块，返回-E_NO_DISK
 *   - 若创建文件，读取磁盘块到缓存中时内存不足，返回-E_NO_MEM
 *   - 若读取磁盘块到缓存中时内存不足，返回-E_NO_MEM
 */
// Checked by DeepSeek-R1 20250509 1509
int open(const char *path, int mode) {
    // Step 1: Alloc a new 'Fd' using 'fd_alloc' in fd.c.
    // Hint: return the error code if failed.
    struct Fd *fd;
    /* Exercise 5.9: Your code here. (1/5) */
    try(fd_alloc(&fd));

    // Step 2: Prepare the 'fd' using 'fsipc_open' in fsipc.c.
    /* Exercise 5.9: Your code here. (2/5) */
    // fsipc_open set fileid and size in `fd`(as Filefd)
    try(fsipc_open(path, mode, fd));

    // Step 3: Set 'va' to the address of the page where the 'fd''s data is
    // cached, using 'fd2data'. Set 'size' and 'fileid' correctly with the value
    // in 'fd' as a 'Filefd'.
    char *va;
    struct Filefd *ffd;
    u_int size, fileid;
    /* Exercise 5.9: Your code here. (3/5) */
    va = fd2data(fd);
    ffd = (struct Filefd *)fd;
    size = ffd->f_file.f_size;
    // file id:
    // 文件ID，在文件系统服务（`fs/serv.c`）中，对应打开的文件的下标，用于在文件系统服务中标识一个打开的文件
    fileid = ffd->f_fileid;

    // Step 4: Map the file content using 'fsipc_map'.
    // 实现差异：原实现增加步长为PTMAP
    // 在实际的fsipc_map，是按文件块计算
    // 虽然在实现中PTMAP = BLOCK_SIZE = 4096，但应与fsipc_map保持一致
    for (int i = 0; i < size; i += BLOCK_SIZE) {
        /* Exercise 5.9: Your code here. (4/5) */
        try(fsipc_map(fileid, i, va + i));
    }

    // Step 5: Return the number of file descriptor using 'fd2num'.
    /* Exercise 5.9: Your code here. (5/5) */
    return fd2num(fd);
}

/*
 * 概述：
 *   关闭文件描述符对应的文件。在关闭前，将文件的所有块标注为脏块
 *   在关闭时，文件所有块、文件元数据（若适用）被同步到磁盘。
 *   在关闭后，取消映射所有文件块。
 *
 *   注意：这不会取消映射存储文件描述符的共享页，因而不会释放
 *   文件系统服务中对应的打开文件表表项。需要上层（如`close`）
 *   调用`fd_close`释放。
 *
 * Precondition：
 *   - `fd`必须指向合法的文件描述符
 *
 * Postcondition：
 *   - 成功：返回0
 *   - 若文件未打开，返回-E_INVAL
 */
int file_close(struct Fd *fd) {
    int r;
    struct Filefd *ffd;
    void *va;
    u_int size, fileid;
    u_int i;

    ffd = (struct Filefd *)fd;
    fileid = ffd->f_fileid;
    size = ffd->f_file.f_size;

    // Set the start address storing the file's content.
    va = fd2data(fd);

    // Tell the file server the dirty page.
    for (i = 0; i < size; i += PTMAP) {
        if ((r = fsipc_dirty(fileid, i)) < 0) {
            debugf("cannot mark pages as dirty\n");
            return r;
        }
    }

    // Request the file server to close the file with fsipc.
    if ((r = fsipc_close(fileid)) < 0) {
        debugf("cannot close the file\n");
        return r;
    }

    // Unmap the content of file, release memory.
    if (size == 0) {
        return 0;
    }
    for (i = 0; i < size; i += PTMAP) {
        if ((r = syscall_mem_unmap(0, (void *)(va + i))) < 0) {
            debugf("cannont unmap the file\n");
            return r;
        }
    }
    return 0;
}

/*
 * 概述：
 *   从文件描述符`fd`对应的文件中偏移量为`offset`处开始，读取`n`个字节到`buf`。
 *   返回成功读取的字节数。
 *
 *   此函数不改变文件描述符中的偏移量。
 *
 * Precondition：
 *   - `fd`必须指向合法的文件描述符
 *   - `buf`必须对应至少能存储`n`个字节的内存区域的首地址
 *
 * Postcondition：
 *   - 返回成功读取的字节数
 *   - 若`offset`大于文件大小，返回0，不修改`buf`处的值
 *   - 若在读取过程中到达文件结尾（按文件大小计算），提前停止读取。
 */
static int file_read(struct Fd *fd, void *buf, u_int n, u_int offset) {
    u_int size;
    struct Filefd *f;
    f = (struct Filefd *)fd;

    // Avoid reading past the end of file.
    size = f->f_file.f_size;

    if (offset > size) {
        return 0;
    }

    if (offset + n > size) {
        n = size - offset;
    }

    memcpy(buf, (char *)fd2data(fd) + offset, n);
    return n;
}

/*
 * 概述：
 *   对于文件描述符号`fdnum`对应文件描述符的对应文件，设置`*blk`为其字节偏移量`offset`
 *   处映射到的本进程地址空间的虚拟地址。
 *
 *   此函数不改变文件描述符中的偏移量。
 *
 * Precondition：
 *   - `blk`必须指向能存储地址的内存区域
 *
 * Postcondition：
 *   - 成功时返回0
 *   - 若`fdnum`不对应有效的文件描述符（越界，未打开），返回-E_INVAL
 *   - 若文件描述符不对应文件，返回-E_INVAL
 *   - 若`offset`大于MAXFILESIZE=4MB，返回-E_NO_DISK
 *   - 若`offset`大于文件大小（即，对应文件映射区域未映射），返回-E_NO_DISK
 */
int read_map(int fdnum, u_int offset, void **blk) {
    int r;
    void *va;
    struct Fd *fd;

    if ((r = fd_lookup(fdnum, &fd)) < 0) {
        return r;
    }

    if (fd->fd_dev_id != devfile.dev_id) {
        return -E_INVAL;
    }

    va = fd2data(fd) + offset;

    if (offset >= MAXFILESIZE) {
        return -E_NO_DISK;
    }

    if (syscall_get_physical_address(va) == 0) {
        return -E_NO_DISK;
    }

    *blk = (void *)va;
    return 0;
}

// Overview:
//  Write 'n' bytes from 'buf' to 'fd' at the current seek position.
// ！！问题！！：函数功能描述似乎不正确
/*
 * 概述：
 *   对于文件描述符`fd`，从`buf`中读取`n`字节数据，并将从文件偏移量`offset`处开始写入。
 *   必要时，增加文件的大小。
 *
 *   此函数不改变文件描述符中的偏移量。
 *
 * Precondition：
 *   - `fd`必须指向有效的**文件类型**的文件描述符
 *   - `buf`处必须分配有至少`n`字节的内存
 *
 * Postcondition：
 *   - 返回成功写入的字节数
 *   - 若写入后文件大小将超过MAXFILESIZE=4MB，不执行写入，返回-E_NO_DISK
 */
static int file_write(struct Fd *fd, const void *buf, u_int n, u_int offset) {
    int r;
    u_int tot;
    struct Filefd *f;

    f = (struct Filefd *)fd;

    // Don't write more than the maximum file size.
    tot = offset + n;

    if (tot > MAXFILESIZE) {
        return -E_NO_DISK;
    }
    // Increase the file's size if necessary
    if (tot > f->f_file.f_size) {
        if ((r = ftruncate(fd2num(fd), tot)) < 0) {
            return r;
        }
    }

    // Write the data
    memcpy((char *)fd2data(fd) + offset, buf, n);
    return n;
}

/*
 * 概述：
 *   对于文件描述符`fd`，将其文件文件大小、是否为目录信息存储到`st`中。
 *
 *   此函数不改变文件描述符中的偏移量。
 *
 * Precondition：
 *   - `fd`必须指向有效的**文件类型**的文件描述符
 *   - `st`处必须分配有能存储`struct Stat`的内存
 *
 * Postcondition：
 *   - 本函数始终返回0
 */
static int file_stat(struct Fd *fd, struct Stat *st) {
    struct Filefd *f;

    f = (struct Filefd *)fd;

    strcpy(st->st_name, f->f_file.f_name);
    st->st_size = f->f_file.f_size;
    st->st_isdir = f->f_file.f_type == FTYPE_DIR;
    return 0;
}

/*
 * 概述：
 *   对于文件描述符号`fdnum`，将其对应的文件描述符对应的文件的大小设置为`size`字节。
 *
 *   若扩容了文件，将尝试实际分配并缓存、映射对应的块，若此时磁盘空间不足，撤销对文件元数据的修改
 *   并返回相应错误码。
 *
 *   若截断了文件，释放磁盘空间，并取消映射本进程虚拟地址空间中相应的共享页。
 *
 *   此函数不改变文件描述符中的偏移量。
 *
 * Precondition：
 *   - `fd`必须指向有效的**文件类型**的文件描述符
 *   - `buf`处必须分配有至少`n`字节的内存
 *
 * Postcondition：
 *   - 若成功，返回0
 *   - 若`size`超过MAXFILESIZE=4MB，返回-E_NO_DISK
 *   - 若`fdnum`不对应有效的文件描述符（越界，未打开），返回-E_INVAL
 *   - 若`fdnum`对应的文件描述符不是文件类型，返回-E_INVAL
 *   - 若为文件分配间接指针存储块时无空闲块，返回-E_NO_DISK
 *   - 若为文件分配间接指针存储块进行缓存时，分配内存失败，返回-E_NO_MEM
 *   - 若为文件分配新的存储块时无空闲块，返回-E_NO_DISK
 *   - 若读取磁盘块到缓存中时内存不足，返回-E_NO_MEM
 */
int ftruncate(int fdnum, u_int size) {
    int i, r;
    struct Fd *fd;
    struct Filefd *f;
    u_int oldsize, fileid;

    if (size > MAXFILESIZE) {
        return -E_NO_DISK;
    }

    if ((r = fd_lookup(fdnum, &fd)) < 0) {
        return r;
    }

    if (fd->fd_dev_id != devfile.dev_id) {
        return -E_INVAL;
    }

    f = (struct Filefd *)fd;
    fileid = f->f_fileid;
    oldsize = f->f_file.f_size;
    f->f_file.f_size = size;

    if ((r = fsipc_set_size(fileid, size)) < 0) {
        return r;
    }

    void *va = fd2data(fd);

    // Map any new pages needed if extending the file
    for (i = ROUND(oldsize, PTMAP); i < ROUND(size, PTMAP); i += PTMAP) {
        if ((r = fsipc_map(fileid, i, va + i)) < 0) {
            int _r = fsipc_set_size(fileid, oldsize);
            if (_r < 0) {
                return _r;
            }
            return r;
        }
    }

    // Unmap pages if truncating the file
    for (i = ROUND(size, PTMAP); i < ROUND(oldsize, PTMAP); i += PTMAP) {
        if ((r = syscall_mem_unmap(0, (void *)(va + i))) < 0) {
            user_panic("ftruncate: syscall_mem_unmap %08x: %d\n", va + i, r);
        }
    }

    return 0;
}

/*
 * 概述：
 *   删除路径`path`对应的文件。
 *
 * Precondition：
 *   - `path`必须指向有效的路径字符串
 *
 * Postcondition：
 *   - 若成功，返回0
 *   - 若路径不存在，返回-E_NOT_FOUND
 *   - 若路径过长，返回-E_BAD_PATH
 */
int remove(const char *path) {
    // Call fsipc_remove.

    /* Exercise 5.13: Your code here. */
    return fsipc_remove(path);
}

/*
 * 概述：
 *   同步文件系统到磁盘。
 *
 * Precondition：
 *
 * Postcondition：
 *   - 若成功，返回0
 *   - 若失败，panic
 */
int sync(void) { return fsipc_sync(); }
