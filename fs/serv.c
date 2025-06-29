/*
 * File system server main loop -
 * serves IPC requests from other environments.
 */

#include "serv.h"
#include "error.h"
#include <fd.h>
#include <fsreq.h>
#include <lib.h>
#include <mmu.h>
/*
 * Fields
 * o_file: mapped descriptor for open file
 * o_fileid: file id
 * o_mode: open mode
 * o_ff: va of filefd page
 */
struct Open {
    struct File *o_file;
    uint32_t o_fileid;
    int o_mode;
    struct Filefd *o_ff;
};

/*
 * Max number of open files in the file system at once
 */
#define MAXOPEN 1024

#define FILEVA 0x60000000ULL

/*
 * Open file table, a per-environment array of open files
 */
struct Open opentab[MAXOPEN];

/*
 * Virtual address at which to receive page mappings containing client requests.
 */
#define REQVA 0x0ffff000ULL

/*
 * 概述：
 *   初始化全局打开文件表并建立虚拟地址映射。
 *   遍历预定义的虚拟地址空间FILEVA=0x60000000，用于后续文件描述符存储。
 *
 * Precondition：
 *   - 全局数组opentab必须已定义且大小匹配MAXOPEN（1024）
 *   - FILEVA宏定义需符合地址空间规划（起始地址0x60000000）
 *   - BLOCK_SIZE必须等于页大小（4096字节）
 *
 * Postcondition：
 *   - opentab数组中每个元素的o_fileid被初始化为索引号
 *   - o_ff指针被设置为从FILEVA起始的连续虚拟地址（间隔4096字节）
 *   - 虚拟地址空间预分配至FILEVA + MAXOPEN*4096（未实际映射物理内存）
 *
 * 副作用：
 *   - 修改全局opentab数组内容，设置文件描述符虚拟地址映射
 *   - 未进行物理页分配，需后续操作完成实际内存映射
 *
 * 关键点：
 *   - 虚拟地址线性递增设计确保每个Filefd独占4KB页，便于后续与其它进程共享
 *   - 此函数仅初始化元数据，实际物理页映射由文件打开操作触发
 *   - 采用硬编码地址布局，依赖FILEVA与系统内存规划的一致性
 */
void serve_init(void) {
    int i;
    u_reg_t va;

    // Set virtual address to map.
    va = FILEVA;

    // Initial array opentab.
    for (i = 0; i < MAXOPEN; i++) {
        opentab[i].o_fileid = i;
        opentab[i].o_ff = (struct Filefd *)va;
        va += BLOCK_SIZE;
    }
}

/*
 * 概述：
 *   在全局打开文件表中分配一个可用条目。通过检查虚拟页引用计数，
 *   选择未被其它进程共享的条目，完成物理页映射及结构体初始化。
 *
 *   本函数不会实际打开文件，仅将对应的Filefd初始化为0
 *
 * Precondition：
 *   - 全局opentab数组必须已通过serve_init初始化虚拟地址映射
 *
 * Postcondition：
 *   - 成功：*o指向分配的Open结构体，对应页被清零，返回文件ID
 *   - 若无可用条目，返回-E_MAX_OPEN
 *   - 若分配用于共享Filefd的结构体时内存不足，返回-E_NO_MEM
 *
 * 副作用：
 *   - 可能调用syscall_mem_alloc修改页表，分配物理内存
 *   - 修改opentab[i].o_ff指向页的内容（通过memset清零）
 *   - 改变目标页的引用计数
 *
 * 关键点：
 *   - Fall-through设计：case 0分配物理页后继续执行case 1进行初始化
 *   - PTE_D|PTE_LIBRARY权限设置：允许共享映射且标记为脏页
 *   - memset使用BLOCK_SIZE确保整页初始化，避免残留数据
 */
int open_alloc(struct Open **o) {
    int i, r;

    // Find an available open-file table entry
    for (i = 0; i < MAXOPEN; i++) {
        // 此处通过检查该打开文件记录对应的Filefd对应的页的引用计数来判断该打开文件记录是否被使用
        // 若引用计数为0：该ilefd对应的页还未被分配，打开文件记录未被使用
        // 若引用计数为1：该ilefd对应的页只分配给了本进程，未分配给其它进程，打开文件记录未被使用
        // 若引用计数>=2：该ilefd对应的页还共享给了其它进程，说明，打开文件记录已经被使用
        switch (pageref(opentab[i].o_ff)) {
        case 0:
            if ((r = syscall_mem_alloc(0, opentab[i].o_ff,
                                       PTE_V | PTE_RW | PTE_USER |
                                           PTE_LIBRARY)) < 0) {
                return r;
            }
        // 注意：此处是Fallthrough case，没有`break`，
        // 从此处开始继续执行`case 1`的内容
        case 1:
            *o = &opentab[i];
            memset((void *)opentab[i].o_ff, 0, BLOCK_SIZE);
            return (*o)->o_fileid;
        }
    }

    return -E_MAX_OPEN;
}

// 注意：与现有注释不同，从函数似乎并未验证文件是否由envid打开
/*
 * 概述：
 *   通过文件ID在全局打开文件表中查找对应的文件条目。若找到且文件处于打开状态，
 *   则将打开文件条目的地址存入`*po`。
 *
 *   具体地，“文件处于打开状态”指的是该打开文件表项对应的Filefd结构体所在的页
 *   被其它进程共享，即，其引用计数>=2
 *
 *   当所有持有该打开文件表项的进程通过`fd_close`函数取消映射存储Filefd结构体所在的页后
 *   该页才会被认为可用。
 *
 *   ！！问题！！：本函数未验证目标文件是否由指定envid打开
 *
 * Precondition：
 *   - 全局opentab数组必须已正确初始化
 *
 * Postcondition：
 *   - 成功：*po指向opentab[fileid]条目，返回0
 *   - 若`fileid`大于最大允许值，返回-E_INVAL
 *   - 若文件未打开，返回-E_INVAL
 *
 * 副作用：
 *   无直接副作用，但可能暴露其他进程的文件描述符（因未验证envid权限）
 *
 * 关键点：
 *   - 仅通过pageref>1判断文件打开状态，未实际验证envid与文件的所属关系
 *   - 直接使用fileid作为数组索引，需严格校验范围防止越界
 */
int open_lookup(uint32_t envid, uint32_t fileid, struct Open **po) {
    struct Open *o;

    if (fileid >= MAXOPEN) {
        return -E_INVAL;
    }

    o = &opentab[fileid];

    /*
     * 通过页引用计数判断文件状态：
     * pageref<=1表示文件未被打开（仅文件系统服务进程持有引用）
     * 此处未验证envid是否实际打开该文件，可能返回其他进程打开的文件
     */
    if (pageref(o->o_ff) <= 1) {
        return -E_INVAL;
    }

    *po = o;
    return 0;
}
/*
 * Functions with the prefix "serve_" are those who
 * conduct the file system requests from clients.
 * The file system receives the requests by function
 * `ipc_recv`, when the requests are received, the
 * file system will call the corresponding `serve_`
 * and return the result to the caller by function
 * `ipc_send`.
 */

/*
 * 概述：
 *   处理打开文件的请求。根据路径和打开模式执行文件创建/打开操作，
 *   分配文件描述符并返回给其它进程（内存共享）。
 *
 *   若指定O_CREAT且文件不存在则创建，
 *   若指定O_TRUNC则清空文件内容。
 *
 * Precondition：
 *   - 全局opentab必须已通过serve_init初始化
 *
 * Postcondition：
 *   - 成功：通过IPC发送文件描述符页，返回0
 *   - 若无可用条目，返回-E_MAX_OPEN
 *   - 若分配用于共享Filefd的结构体时内存不足，返回-E_NO_MEM
 *   - 若路径过长，返回-E_BAD_PATH
 *   - 若不创建文件，路径不存在，返回-E_NOT_FOUND
 *   - 若创建文件，若中间目录不存在，返回-E_NOT_FOUND
 *   - 若创建文件，为目录文件分配间接指针存储块时无空闲块，返回-E_NO_DISK
 *   -
 * 若创建文件，为目录文件分配间接指针存储块进行缓存时，分配内存失败，返回-E_NO_MEM
 *   - 若创建文件，为目录文件分配新的存储块时无空闲块，返回-E_NO_DISK
 *   - 若创建文件，读取磁盘块到缓存中时内存不足，返回-E_NO_MEM
 *
 * 副作用：
 *   - 可能修改opentab条目内容
 *   - 可能创建新文件或截断现有文件
 *
 * 关键点：
 *   - 创建与打开的顺序：先尝试创建（若指定O_CREAT），再执行打开
 *   - 错误处理链式结构：任一环节失败立即终止并返回错误
 *   - 文件描述符页通过PTE_D|PTE_LIBRARY权限共享，允许跨进程访问
 */
void serve_open(uint32_t envid, struct Fsreq_open *rq) {
    struct File *f;
    struct Filefd *ff;
    int r;
    struct Open *o;

    // Find a file id.
    if ((r = open_alloc(&o)) < 0) {
        ipc_send(envid, r, 0, 0);
        return;
    }

    // 若指定`O_CREAT`打开标致，尝试创建文件，若创建文件出错，则返回错误码
    // （若文件已经存在，忽略`file_create`产生的`E_FILE_EXISTS`错误）
    if ((rq->req_omode & O_CREAT) && (r = file_create(rq->req_path, &f)) < 0 &&
        r != -E_FILE_EXISTS) {
        ipc_send(envid, r, 0, 0);
        return;
    }

    // Open the file.
    if ((r = file_open(rq->req_path, &f)) < 0) {
        ipc_send(envid, r, 0, 0);
        return;
    }

    // Save the file pointer.
    o->o_file = f;

    // If mode include O_TRUNC, set the file size to 0
    if (rq->req_omode & O_TRUNC) {
        if ((r = file_set_size(f, 0)) < 0) {
            ipc_send(envid, r, 0, 0);
        }
    }

    // Fill out the Filefd structure
    ff = (struct Filefd *)o->o_ff;
    ff->f_file = *f;
    ff->f_fileid = o->o_fileid;
    o->o_mode = rq->req_omode;
    ff->f_fd.fd_omode = o->o_mode;
    ff->f_fd.fd_dev_id = devfile.dev_id;
    ipc_send(envid, 0, o->o_ff, PTE_V | PTE_RW | PTE_USER | PTE_LIBRARY);
}

/*
 * 概述：
 *   处理客户端文件块映射请求。通过打开文件表中的文件ID和偏移量定位已打开文件，
 *   获取对应磁盘块缓存的页并通过IPC将该页可写地共享给客户端。
 *
 *   注意：由于`PAGE_SIZE`=`BLOCK_SIZE`共享一页刚好相当于共享一块。
 *
 * Precondition：
 *   - 全局opentab必须已正确初始化
 *   - 文件ID可以非法，此时将返回错误
 *
 * Postcondition：
 *   - 成功：通过IPC共享文件块缓存对应的页，返回0
 *   - 若`fileid`大于最大允许值，返回-E_INVAL
 *   - 若文件未打开，返回-E_INVAL
 *   - 若为文件分配间接指针存储块时无空闲块，返回-E_NO_DISK
 *   - 若为文件分配间接指针存储块进行缓存时，分配内存失败，返回-E_NO_MEM
 *   - 若`filebno`大于NINDIRECT=1024，返回-E_INVAL
 *   - 若为文件分配新的存储块时无空闲块，返回-E_NO_DISK
 *   - 若读取磁盘块到缓存中时内存不足，返回-E_NO_MEM
 *
 * 副作用：
 *   - 可能触发磁盘块加载到内存，分配物理页
 *   - 修改目标块的引用计数
 *
 * 关键点：
 *   - 文件块号计算采用整数除法，依赖BLOCK_SIZE对齐
 *   - IPC返回的虚拟地址直接暴露缓存页，依赖PTE_LIBRARY实现跨进程共享
 *   - 错误处理立即终止流程，未释放已分配资源
 */
void serve_map(uint32_t envid, struct Fsreq_map *rq) {
    struct Open *pOpen;
    uint32_t filebno;
    void *blk;
    int r;

    if ((r = open_lookup(envid, rq->req_fileid, &pOpen)) < 0) {
        ipc_send(envid, r, 0, 0);
        return;
    }

    filebno = rq->req_offset / BLOCK_SIZE;

    if ((r = file_get_block(pOpen->o_file, filebno, &blk)) < 0) {
        ipc_send(envid, r, 0, 0);
        return;
    }

    ipc_send(envid, 0, blk, PTE_V | PTE_RW | PTE_USER | PTE_LIBRARY);
}

/*
 * 概述：
 *   处理客户端设置文件大小的请求。通过文件ID查找已打开的文件，
 *   调用底层文件系统接口调整文件尺寸。
 *
 * Precondition：
 *   - opentab必须包含有效的文件条目
 *
 * Postcondition：
 *   - 成功：文件尺寸更新并通过IPC返回0
 *   - 若`fileid`大于最大允许值，返回-E_INVAL
 *   - 若文件未打开，返回-E_INVAL
 *
 * 副作用：
 *   - 可能触发文件截断操作，释放磁盘空间
 *   - 若文件包含父目录，会刷新文件元数据到磁盘
 *
 * 关键点：
 *   - 文件尺寸扩展时不预分配存储，依赖后续获取/写入操作
 */
void serve_set_size(uint32_t envid, struct Fsreq_set_size *rq) {
    struct Open *pOpen;
    int r;
    if ((r = open_lookup(envid, rq->req_fileid, &pOpen)) < 0) {
        ipc_send(envid, r, 0, 0);
        return;
    }

    // `file_set_size`正确处理了文件截断以及文件增长
    if ((r = file_set_size(pOpen->o_file, rq->req_size)) < 0) {
        ipc_send(envid, r, 0, 0);
        return;
    }

    ipc_send(envid, 0, 0, 0);
}

/*
 * 概述：
 *   处理客户端关闭文件的请求。通过文件ID验证文件打开状态并执行关闭操作。
 *
 *   同步文件中所有标记为脏的块到磁盘，并同步文件元数据（若适用）。
 *
 *   注意：即使通过本调用关闭了文件，其打开文件表表项也不会立即释放。
 *   而是需要请求打开该文件的进程手动解除。
 *
 *   具体地，这需要对应进程手动使用`syscall_mem_unmap`将打开文件时
 *   得到的含Filefd结构体的共享页从自身的地址空间中取消映射。
 *
 *   具体地，在用户态库函数`file_close`中实现了上述功能。
 *
 * Precondition：
 *   - 全局opentab必须包含有效条目
 *
 * Postcondition：
 *   - 成功：文件数据刷新至磁盘，IPC返回0
 *   - 若请求的`fileid`大于最大允许值，返回-E_INVAL
 *   - 若文件未打开，返回-E_INVAL
 *
 * 副作用：
 *   - 调用file_close触发文件数据同步，修改磁盘内容
 *   - 修改父目录元数据脏页状态
 */
void serve_close(uint32_t envid, struct Fsreq_close *rq) {
    struct Open *pOpen;

    int r;

    if ((r = open_lookup(envid, rq->req_fileid, &pOpen)) < 0) {
        ipc_send(envid, r, 0, 0);
        return;
    }

    file_close(pOpen->o_file);
    ipc_send(envid, 0, 0, 0);
}

/*
 * 概述：
 *   处理客户端删除文件的请求。通过路径直接调用文件系统接口执行删除，
 *   未验证文件占用状态，可能造成正在打开的文件数据不一致。
 *
 * Precondition：
 *   - 文件系统元数据需已正确初始化（超级块、位图等）
 *   - 路径必须符合文件系统格式要求
 *
 * Postcondition：
 *   - 成功：IPC返回0，文件被标记为删除且空间被释放
 *   - 若路径不存在，返回-E_NOT_FOUND
 *   - 若路径过长，返回-E_BAD_PATH
 *
 * 副作用：
 *   - 修改文件系统的目录结构和全局位图
 *   - 触发磁盘I/O操作更新元数据
 *   - 可能破坏已打开文件的缓存一致性
 *
 * 关键点：
 *   - 未检测文件是否被其他进程打开，存在数据损坏风险
 */
void serve_remove(uint32_t envid, struct Fsreq_remove *rq) {
    // Step 1: Remove the file specified in 'rq' using 'file_remove' and store
    // its return value.
    int r;
    /* Exercise 5.11: Your code here. (1/2) */
    r = file_remove(rq->req_path);

    // Step 2: Respond the return value to the caller 'envid' using 'ipc_send'.
    /* Exercise 5.11: Your code here. (2/2) */
    ipc_send(envid, r, 0, 0);
}

/*
 * 概述：
 *   处理客户端标记文件脏页的请求。通过文件ID定位已打开文件，
 *   将指定偏移量对应的数据块标记为脏。
 *
 * Precondition：
 *   - 全局opentab必须包含有效的文件条目
 *
 * Postcondition：
 *   - 成功：目标数据块页表项被标记为脏，IPC返回0
 *   - 若`fileid`大于最大允许值，返回-E_INVAL
 *   - 若文件未打开，返回-E_INVAL
 *   - 若逻辑块号越界，返回-E_INVAL
 *   - 若对应逻辑块未分配，返回-E_NOT_FOUND
 *
 * 副作用：
 *   - 修改磁盘块缓存页面的PTE_DIRTY标志
 *
 * 关键点：
 *   - 脏标记仅影响缓存状态，实际写回依赖同步机制
 */
void serve_dirty(uint32_t envid, struct Fsreq_dirty *rq) {
    struct Open *pOpen;
    int r;

    if ((r = open_lookup(envid, rq->req_fileid, &pOpen)) < 0) {
        ipc_send(envid, r, 0, 0);
        return;
    }

    if ((r = file_dirty(pOpen->o_file, rq->req_offset)) < 0) {
        ipc_send(envid, r, 0, 0);
        return;
    }

    ipc_send(envid, 0, 0, 0);
}

/*
 * 概述：
 *   处理客户端文件系统同步请求。强制将内存中所有脏块写入磁盘，
 *   确保数据持久化。
 *
 *   注意：假定同步操作始终成功。
 *
 * Precondition：
 *   - 全局超级块super必须已正确初始化
 *
 * Postcondition：
 *   - 成功：所有脏块物理写入磁盘，IPC返回0
 *   - 若失败，panic
 *
 * 副作用：
 *   - 触发全量磁盘I/O，可能造成系统短暂卡顿
 *   - 修改磁盘物理存储内容
 *
 * 关键点：
 *   - 响应延迟：需等待所有I/O完成才发送IPC确认
 */
void serve_sync(uint32_t envid) {
    fs_sync();
    ipc_send(envid, 0, 0, 0);
}

/*
 * The serve function table
 * File system use this table and the request number to
 * call the corresponding serve function.
 */
void *serve_table[MAX_FSREQNO] = {
    [FSREQ_OPEN] = serve_open,         [FSREQ_MAP] = serve_map,
    [FSREQ_SET_SIZE] = serve_set_size, [FSREQ_CLOSE] = serve_close,
    [FSREQ_DIRTY] = serve_dirty,       [FSREQ_REMOVE] = serve_remove,
    [FSREQ_SYNC] = serve_sync,
};

/*
 * Overview:
 *  The main loop of the file system server.
 *  It receives requests from other processes, if no request,
 *  the kernel will schedule other processes. Otherwise, it will
 *  call the corresponding serve function with the reqeust number
 *  to handle the request.
 */
void serve(void) {
    uint64_t req;
    uint32_t whom, perm;
    void (*func)(uint32_t, uint32_t);

    for (;;) {
        perm = 0;

        int ret = ipc_recv(&whom, &req, (void *)REQVA, &perm);

        if (ret != 0) {
            if (ret == -E_INTR) {
                continue;
            } else {
                debugf("fs: failed to receive request: %d\n", ret);
            }
        }

        // All requests must contain an argument page
        if (!(perm & PTE_V)) {
            debugf("fs: Invalid request from %08x: no argument page\n", whom);
            continue; // just leave it hanging, waiting for the next request.
        }

        // The request number must be valid.
        if (req < 0 || req >= MAX_FSREQNO) {
            debugf("fs: Invalid request code %d from %08x\n", req, whom);
            panic_on(syscall_mem_unmap(0, (void *)REQVA));
            continue;
        }

        // Select the serve function and call it.
        func = serve_table[req];
        func(whom, REQVA);

        // Unmap the argument page.
        panic_on(syscall_mem_unmap(0, (void *)REQVA));
    }
}

/*
 * Overview:
 *  The main function of the file system server.
 *  It will call the `serve_init` to initialize the file system
 *  and then call the `serve` to handle the requests.
 */
int main() {
    user_assert(sizeof(struct File) == FILE_STRUCT_SIZE);

    debugf("fs: FS is running\n");

    serve_init();
    fs_init();

    debugf("fs: WE SHALL NEVER SURRENDER!\n");

    serve();
    return 0;
}
