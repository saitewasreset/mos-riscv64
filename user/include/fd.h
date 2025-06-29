#ifndef _USER_FD_H_
#define _USER_FD_H_ 1

#include <fs.h>

#define debug 0

#define MAXFD 32
#define FILEBASE 0x60000000ULL

// 4KB
#define PTMAP P3MAP
// 4MB
#define PDMAP (2 * P2MAP)

// 开始存储本进程的文件描述符表的内存地址：[0x5FC0 0000, 0x6000 0000)，共4MB
// 每个文件描述符将占用1页（4KB）
#define FDTABLE (FILEBASE - PDMAP)

#define INDEX2FD(i) (FDTABLE + (i) * PTMAP)
#define INDEX2DATA(i) (FILEBASE + (i) * PDMAP)

// pre-declare for forward references
struct Fd;
struct Stat;
struct Dev;

// Device struct:
// It is used to read and write data from corresponding device.
// We can use the five functions to handle data.
// There are three devices in this OS: file, console and pipe.
// 注意，若返回值<0，表示错误；对于read/write，若大于等于0，表示读取/写入的字节数
struct Dev {
    int dev_id;
    char *dev_name;
    int (*dev_read)(struct Fd *fd, void *buf, uint32_t n, uint32_t offset);
    int (*dev_write)(struct Fd *fd, const void *buf, uint32_t n,
                     uint32_t offset);
    int (*dev_close)(struct Fd *fd);
    int (*dev_stat)(struct Fd *fd, struct Stat *p_stat);
    int (*dev_seek)(struct Fd *fd, uint32_t target_offset);
};

// file descriptor
struct Fd {
    uint32_t fd_dev_id;
    uint32_t fd_offset;
    uint32_t fd_omode;
};

// State
struct Stat {
    char st_name[MAXNAMELEN];
    uint32_t st_size;
    uint32_t st_isdir;
    struct Dev *st_dev;
};

// file descriptor + file
struct Filefd {
    struct Fd f_fd;
    uint32_t f_fileid;
    struct File f_file;
};

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
int fd_alloc(struct Fd **fd);
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
int fd_lookup(int fdnum, struct Fd **fd);
void *fd2data(struct Fd *);
int fd2num(struct Fd *);
int dev_lookup(int dev_id, struct Dev **dev);
int num2fd(int fd);
extern struct Dev devcons;
extern struct Dev devfile;
extern struct Dev devpipe;

#endif
