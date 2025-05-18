#include "error.h"
#include <env.h>
#include <fd.h>
#include <lib.h>
#include <mmu.h>

static struct Dev *devtab[] = {&devfile, &devcons,
#if !defined(LAB) || LAB >= 6
                               &devpipe,
#endif
                               0};

int dev_lookup(int dev_id, struct Dev **dev) {
    for (int i = 0; devtab[i]; i++) {
        if (devtab[i]->dev_id == dev_id) {
            *dev = devtab[i];
            return 0;
        }
    }
    *dev = NULL;
    debugf("[%08x] unknown device type %d\n", env->env_id, dev_id);
    return -E_INVAL;
}

/*
 * 概述：
 *   在进程文件描述符表中寻找首个未映射的fd页。通过遍历虚拟地址空间，
 *   检查页目录项与页表项的有效位，定位可用的文件描述符槽位。
 *
 * Precondition：
 *   - 全局页目录vpd和页表vpt必须已正确映射，反映当前进程地址空间状态
 *   - MAXFD宏定义必须与系统文件描述符表布局一致（FDTABLE+MAXFD*PTMAP不越界）
 *   - 调用者需在获取返回的fd地址后分配物理页，否则后续调用可能返回相同地址
 *
 * Postcondition：
 *   - 成功：*fd设置为可用fd页的虚拟地址，返回0
 *   - 失败：所有fd页已映射，返回-E_MAX_OPEN
 *
 * 副作用：
 *   无直接副作用（仅进行地址空间状态查询）
 *   间接依赖调用者后续的物理页分配操作完成fd页初始化
 *
 * 关键点：
 *   - 双重检查机制：先验证页目录项PTE_V，再验证页表项PTE_V，确保地址未映射
 *   - INDEX2FD宏将fd编号转换为FDTABLE区域的虚拟地址（0x5FC00000起始）
 *   - 循环上限为MAXFD-1（共32个fd槽位），确保不越界
 *   - 返回的虚拟地址需由调用者实际分配物理页
 */
int fd_alloc(struct Fd **fd) {
    u_int va;
    u_int fdno;

    for (fdno = 0; fdno < MAXFD - 1; fdno++) {
        va = INDEX2FD(fdno);

        if ((vpd[va / PDMAP] & PTE_V) == 0) {
            *fd = (struct Fd *)va;
            return 0;
        }

        if ((vpt[va / PTMAP] & PTE_V) == 0) { // the fd is not used
            *fd = (struct Fd *)va;
            return 0;
        }
    }

    return -E_MAX_OPEN;
}

/*
 * 概述：
 *   关闭`fd`对应的文件描述符。
 *   这将解除`fd`所在页和文件系统服务/其它服务中相关页的共享映射。
 *
 * Precondition：
 *   - `fd`必须指向已打开的文件描述符
 *
 * Postcondition：
 *   - 若取消映射`fd`对应的页失败，panic
 */
void fd_close(struct Fd *fd) { panic_on(syscall_mem_unmap(0, fd)); }

/*
 * 概述：
 *   查找文件描述符号`fdnum`对应的文件描述符，
 *   设置`*fd`指向该文件描述符（位于本进程的FDTABLE区域）。
 *
 *   此函数不改变文件描述符中的偏移量。
 *
 * Precondition：
 *   - `fd`必须指向能存储文件描述符的内存区域
 *
 * Postcondition：
 *   - 成功时返回0
 *   - 若`fdnum`不对应有效的文件描述符（越界，未打开），返回-E_INVAL
 */
int fd_lookup(int fdnum, struct Fd **fd) {
    u_int va;

    if (fdnum >= MAXFD) {
        return -E_INVAL;
    }

    va = INDEX2FD(fdnum);

    if ((vpt[va / PTMAP] & PTE_V) != 0) { // the fd is used
        *fd = (struct Fd *)va;
        return 0;
    }

    return -E_INVAL;
}

void *fd2data(struct Fd *fd) { return (void *)INDEX2DATA(fd2num(fd)); }

int fd2num(struct Fd *fd) { return ((u_int)fd - FDTABLE) / PTMAP; }

int num2fd(int fd) { return fd * PTMAP + FDTABLE; }

/*
 * 概述：
 *   关闭文件描述符号`fdnum`对应的文件描述符，
 *   根据文件描述符对应的类型（文件/设备）
 *   执行底层释放资源操作，并最终释放文件描述符。
 *
 * Precondition：
 *   - `fd`必须指向能存储文件描述符的内存区域
 *
 * Postcondition：
 *   - 成功时返回0
 *   - 若`fdnum`不对应有效的文件描述符（越界，未打开），返回-E_INVAL
 *   - 若底层释放操作失败，返回对应的错误代码。
 *       - 对于文件类型，释放失败时将panic
 */
int close(int fdnum) {
    int r;
    struct Dev *dev = NULL;
    struct Fd *fd;

    if ((r = fd_lookup(fdnum, &fd)) < 0 ||
        (r = dev_lookup(fd->fd_dev_id, &dev)) < 0) {
        return r;
    }

    r = (*dev->dev_close)(fd);
    fd_close(fd);
    return r;
}

/*
 * 概述：
 *   关闭本进程打开的所有文件描述符。
 *
 * Precondition：
 *
 * Postcondition：
 *   - 成功时返回0
 *   - 若底层释放操作失败，返回对应的错误代码。
 *       - 对于文件类型，释放失败时将panic
 */
void close_all(void) {
    int i;

    for (i = 0; i < MAXFD; i++) {
        close(i);
    }
}

/*
 * 概述：
 *   复制文件描述符号`oldfdnum`对应的文件描述符到文件描述符号`newfdnum`。
 *
 *   将先关闭文件描述符号`newfdnum`对应的文件描述符，关闭过程中产生的错误
 *   被静默忽略。
 *   之后，将新文件描述符和旧文件描述符映射到相同页。
 *   将新文件描述符对应的文件在虚拟地址空间中的缓存与旧文件描述符映射到相同页。
 *
 * Precondition：
 *   - `newfdnum`必须小于MAXFD=32
 * Postcondition：
 *   - 成功时返回0
 *   - 若`oldfdnum`不对应有效的文件描述符（越界，未打开），返回-E_INVAL
 *
 * 潜在问题：
 *   ！！问题！！：未检查`newfdnum`的合法性。
 */
int dup(int oldfdnum, int newfdnum) {
    int i, r;
    void *ova, *nva;
    u_int pte;
    struct Fd *oldfd, *newfd;

    /* Step 1: Check if 'oldnum' is valid. if not, return an error code, or get
     * 'fd'. */
    if ((r = fd_lookup(oldfdnum, &oldfd)) < 0) {
        return r;
    }

    /* Step 2: Close 'newfdnum' to reset content. */
    close(newfdnum);
    /* Step 3: Get 'newfd' reffered by 'newfdnum'. */
    newfd = (struct Fd *)INDEX2FD(newfdnum);
    /* Step 4: Get data address. */
    ova = fd2data(oldfd);
    nva = fd2data(newfd);
    /* Step 5: Dunplicate the data and 'fd' self from old to new. */
    if ((r = syscall_mem_map(0, oldfd, 0, newfd,
                             vpt[VPN(oldfd)] & (PTE_D | PTE_LIBRARY))) < 0) {
        goto err;
    }

    if (vpd[PDX(ova)]) {
        for (i = 0; i < PDMAP; i += PTMAP) {
            pte = vpt[VPN(ova + i)];

            if (pte & PTE_V) {
                // should be no error here -- pd is already allocated
                if ((r = syscall_mem_map(0, (void *)(ova + i), 0,
                                         (void *)(nva + i),
                                         pte & (PTE_D | PTE_LIBRARY))) < 0) {
                    goto err;
                }
            }
        }
    }

    return newfdnum;

err:
    /* If error occurs, cancel all map operations. */
    panic_on(syscall_mem_unmap(0, newfd));

    for (i = 0; i < PDMAP; i += PTMAP) {
        panic_on(syscall_mem_unmap(0, (void *)(nva + i)));
    }

    return r;
}

/*
 * 概述：
 *   从文件描述符号`fdnum`对应的文件描述符中读取至多`n`个字节到`buf`。
 *
 *   将按成功读取的字节数修改文件偏移量。
 *
 * Precondition：
 *   - `buf`处必须分配有至少`n`字节的内存
 * Postcondition：
 *   - 返回成功读取的字节数
 *   - 若`oldfdnum`不对应有效的文件描述符（越界，未打开），返回-E_INVAL
 *   - 若文件描述符是只写的，返回-E_INVAL
 *   - 若底层读取错误，返回底层错误码
 *      - 对于文件，由于进行的是缓存中的读取，总是会成功
 */
// Checked by DeepSeek-R1 20250509 1529
int read(int fdnum, void *buf, u_int n) {
    int r = 0;

    // Similar to the 'write' function below.
    // Step 1: Get 'fd' and 'dev' using 'fd_lookup' and 'dev_lookup'.
    struct Dev *dev = NULL;
    struct Fd *fd = NULL;
    /* Exercise 5.10: Your code here. (1/4) */

    try(fd_lookup(fdnum, &fd));
    try(dev_lookup(fd->fd_dev_id, &dev));

    // Step 2: Check the open mode in 'fd'.
    // Return -E_INVAL if the file is opened for writing only (O_WRONLY).
    /* Exercise 5.10: Your code here. (2/4) */

    if ((fd->fd_omode & O_ACCMODE) == O_WRONLY) {
        return -E_INVAL;
    }

    // Step 3: Read from 'dev' into 'buf' at the seek position (offset in 'fd').
    /* Exercise 5.10: Your code here. (3/4) */

    r = dev->dev_read(fd, buf, n, fd->fd_offset);

    if (r < 0) {
        return r;
    }

    // Step 4: Update the offset in 'fd' if the read is successful.
    /* Hint: DO NOT add a null terminator to the end of the buffer!
     *  A character buffer is not a C string. Only the memory within [buf,
     * buf+n) is safe to use. */
    /* Exercise 5.10: Your code here. (4/4) */
    fd->fd_offset += r;

    return r;
}

/*
 * 概述：
 *   从文件描述符号`fdnum`对应的文件描述符中读取`n`个字节到`buf`。
 *
 *   将按成功读取的字节数修改文件偏移量。
 *
 *   若返回错误，到达文件尾（read返回0），提前返回。
 *   否则，循环读取直到读取到`n`个字节。
 *
 * Precondition：
 *   - `buf`处必须分配有至少`n`字节的内存
 * Postcondition：
 *   - 返回成功读取的字节数
 *   - 若`oldfdnum`不对应有效的文件描述符（越界，未打开），返回-E_INVAL
 *   - 若文件描述符是只写的，返回-E_INVAL
 *   - 若底层读取错误，返回底层错误码
 *      - 对于文件，由于进行的是缓存中的读取，总是会成功
 */
int readn(int fdnum, void *buf, u_int n) {
    int m, tot;

    for (tot = 0; tot < n; tot += m) {
        m = read(fdnum, (char *)buf + tot, n - tot);

        if (m < 0) {
            return m;
        }

        if (m == 0) {
            break;
        }
    }

    return tot;
}

/*
 * 概述：
 *   向文件描述符号`fdnum`对应的文件描述符中写入从`buf`读取的至多`n`个字节。
 *
 *   将按成功写入的字节数修改文件偏移量。
 *
 * Precondition：
 *   - `buf`处必须分配有至少`n`字节的内存
 * Postcondition：
 *   - 返回成功吸入的字节数
 *   - 若`oldfdnum`不对应有效的文件描述符（越界，未打开），返回-E_INVAL
 *   - 若文件描述符是只读的，返回-E_INVAL
 *   - 若底层写入错误，返回底层错误码
 *      -
 * 对于文件，若写入后文件大小将超过MAXFILESIZE=4MB，不执行写入，返回-E_NO_DISK
 */
int write(int fdnum, const void *buf, u_int n) {
    int r;
    struct Dev *dev;
    struct Fd *fd;

    if ((r = fd_lookup(fdnum, &fd)) < 0 ||
        (r = dev_lookup(fd->fd_dev_id, &dev)) < 0) {
        return r;
    }

    if ((fd->fd_omode & O_ACCMODE) == O_RDONLY) {
        return -E_INVAL;
    }

    r = dev->dev_write(fd, buf, n, fd->fd_offset);
    if (r > 0) {
        fd->fd_offset += r;
    }

    return r;
}

/*
 * 概述：
 *   移动文件描述符号`fdnum`对应的文件描述符的文件偏移到`offset`。
 *
 * Precondition：
 *
 * Postcondition：
 *   - 若成功，返回0
 *   - 若`oldfdnum`不对应有效的文件描述符（越界，未打开），返回-E_INVAL
 */
int seek(int fdnum, u_int offset) {
    int r;
    struct Fd *fd;

    if ((r = fd_lookup(fdnum, &fd)) < 0) {
        return r;
    }

    fd->fd_offset = offset;
    return 0;
}

/*
 * 概述：
 *   获取文件描述符号`fdnum`对应的文件描述符的文件名、文件大小、是否是目录、所在设备信息
 *   保存到`*stat`对应的内存中。
 *
 * Precondition：
 *   - `stat`处必须分配有至少能存储`struct State`的内存
 * Postcondition：
 *   - 若成功，返回0
 *   - 若`oldfdnum`不对应有效的文件描述符（越界，未打开），返回-E_INVAL
 */
int fstat(int fdnum, struct Stat *stat) {
    int r;
    struct Dev *dev = NULL;
    struct Fd *fd;

    if ((r = fd_lookup(fdnum, &fd)) < 0 ||
        (r = dev_lookup(fd->fd_dev_id, &dev)) < 0) {
        return r;
    }

    stat->st_name[0] = 0;
    stat->st_size = 0;
    stat->st_isdir = 0;
    stat->st_dev = dev;
    return (*dev->dev_stat)(fd, stat);
}

/*
 * 概述：
 *   获取路径`path`对应的文件文件的文件名、文件大小、是否是目录、所在设备信息
 *   保存到`*stat`对应的内存中。
 *
 *   具体地，先使用`open`打开文件，再使用`fstat`获取信息。之后关闭文件描述符。
 *
 * Precondition：
 *   - `stat`处必须分配有至少能存储`struct State`的内存
 * Postcondition：
 *   - 若成功，返回0
 *   - 若`oldfdnum`不对应有效的文件描述符（越界，未打开），返回-E_INVAL
 *   - 若无可用的文件描述符，返回-E_MAX_OPEN
 *   - 若文件系统服务无可用打开文件条目，返回-E_MAX_OPEN
 *   - 若分配用于共享Filefd的结构体时内存不足，返回-E_NO_MEM
 *   - 若路径过长，返回-E_BAD_PATH
 *   - 若路径不存在，返回-E_NOT_FOUND
 *   - 若读取磁盘块到缓存中时内存不足，返回-E_NO_MEM
 */
int stat(const char *path, struct Stat *stat) {
    int fd, r;

    if ((fd = open(path, O_RDONLY)) < 0) {
        return fd;
    }

    r = fstat(fd, stat);
    close(fd);
    return r;
}
