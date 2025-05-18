#include "error.h"
#include "fs.h"
#include "mmu.h"
#include <env.h>
#include <fsreq.h>
#include <lib.h>

#define debug 0

u_char fsipcbuf[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

// Overview:
//  Send an IPC request to the file server, and wait for a reply.
//
// Parameters:
//  @type: request code, passed as the simple integer IPC value.
//  @fsreq: page to send containing additional request data, usually fsipcbuf.
//          Can be modified by server to return additional response info.
//  @dstva: virtual address at which to receive reply page, 0 if none.
//  @*perm: permissions of received page.
//
// Returns:
//  0 if successful,
//  < 0 on failure.
static int fsipc(u_int type, void *fsreq, void *dstva, u_int *perm) {
    u_int whom;
    // Our file system server must be the 2nd env.
    ipc_send(envs[1].env_id, type, fsreq, PTE_D);
    return ipc_recv(&whom, dstva, perm);
}

/*
 * 概述：
 *   请求打开文件的。根据路径和打开模式执行文件创建/打开操作，
 *   分配文件描述符并返回给调用进程（内存共享）。
 *
 *   注意：调用进程`fd`处的页被设置为共享页，并与文件系统服务进程
 *   `FILE_VA`中的文件描述符共享同一物理页。
 *
 *   若指定O_CREAT且文件不存在则创建，
 *   若指定O_TRUNC则清空文件内容。
 *
 * Precondition：
 *   - `path`不能长于MAXPATHLEN=1024
 *   - 全局opentab必须已通过serve_init初始化
 *
 * Postcondition：
 *   - 成功：通过IPC发送文件描述符页，返回0
 *   - 若文件系统服务无可用打开文件条目，返回-E_MAX_OPEN
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
int fsipc_open(const char *path, u_int omode, struct Fd *fd) {
    u_int perm;
    struct Fsreq_open *req;

    req = (struct Fsreq_open *)fsipcbuf;

    // The path is too long.
    if (strlen(path) >= MAXPATHLEN) {
        return -E_BAD_PATH;
    }

    strcpy((char *)req->req_path, path);
    req->req_omode = omode;
    return fsipc(FSREQ_OPEN, req, fd, &perm);
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
int fsipc_map(u_int fileid, u_int offset, void *dstva) {
    int r;
    u_int perm;
    struct Fsreq_map *req;

    req = (struct Fsreq_map *)fsipcbuf;
    req->req_fileid = fileid;
    req->req_offset = offset;

    if ((r = fsipc(FSREQ_MAP, req, dstva, &perm)) < 0) {
        return r;
    }
    // 实现差异：
    // ref: https://os.buaa.edu.cn/discussion/440
    // ref: https://os.buaa.edu.cn/discussion/457
    // 原实现要求perm = PTE_D | PTE_LIBRARY | PTE_V
    // 这是依赖于`sys_ipc_try_send`的不正确实现
    // 具体地，`sys_ipc_try_send`中，通过`page_insert`映射了页
    // 根据`page_insert`的参考实现，
    // 其设置标记位为`perm | PTE_C_CACHEABLE | PTE_V`
    // 而在`sys_ipc_try_send`中，错误地修改目标Env的`env_ipc_perm`字段
    // 为`perm | PTE_V`
    // 若修复了`sys_ipc_try_send`的不正确实现，将导致原代码中的下述检查不通过

    /*if ((perm & ~(PTE_D | PTE_LIBRARY | PTE_C_CACHEABLE)) != (PTE_V)) {
        user_panic("fsipc_map: unexpected permissions %08x for dstva %08x",
                   perm, dstva);
    }*/

    // 由于可能影响课上实验评测
    // 撤销了对`sys_ipc_try_send`的修复以及对本函数的修改
    // ref: https://os.buaa.edu.cn/discussion/457
    if ((perm & ~(PTE_D | PTE_LIBRARY)) != (PTE_V)) {
        user_panic("fsipc_map: unexpected permissions %08x for dstva %08x",
                   perm, dstva);
    }

    return 0;
}

/*
 * 概述：
 *   请求设置文件大小。通过文件ID查找已打开的文件，
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
int fsipc_set_size(u_int fileid, u_int size) {
    struct Fsreq_set_size *req;

    req = (struct Fsreq_set_size *)fsipcbuf;
    req->req_fileid = fileid;
    req->req_size = size;
    return fsipc(FSREQ_SET_SIZE, req, 0, 0);
}

/*
 * 概述：
 *   请求关闭文件。通过文件ID验证文件打开状态并执行关闭操作。
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
 *
 * 关键点：
 *   - 未清除Open结构体，实际文件描述符资源未释放
 */
int fsipc_close(u_int fileid) {
    struct Fsreq_close *req;

    req = (struct Fsreq_close *)fsipcbuf;
    req->req_fileid = fileid;
    return fsipc(FSREQ_CLOSE, req, 0, 0);
}

/*
 * 概述：
 *   请求标记文件脏页。通过文件ID定位已打开文件，
 *   将指定偏移量对应的数据块标记为脏。
 *
 * Precondition：
 *   - 全局opentab必须包含有效的文件条目
 *
 * Postcondition：
 *   - 成功：目标数据块页表项被标记为脏，IPC返回0
 *   - 失败：IPC返回错误码（E_INVAL等）
 *
 * 副作用：
 *   - 修改磁盘块缓存页面的PTE_DIRTY标志
 *
 * 关键点：
 *   - 脏标记仅影响缓存状态，实际写回依赖同步机制
 */
int fsipc_dirty(u_int fileid, u_int offset) {
    struct Fsreq_dirty *req;

    req = (struct Fsreq_dirty *)fsipcbuf;
    req->req_fileid = fileid;
    req->req_offset = offset;
    return fsipc(FSREQ_DIRTY, req, 0, 0);
}

/*
 * 概述：
 *   请求删除文件。通过路径直接调用文件系统接口执行删除，
 *   未验证文件占用状态，可能造成正在打开的文件数据不一致。
 *
 * Precondition：
 *   - 文件系统元数据需已正确初始化（超级块、位图等）
 *   - 路径必须符合文件系统格式要求
 *   - `path`不能长于MAXPATHLEN=1024，也不能为0
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
int fsipc_remove(const char *path) {
    // Step 1: Check the length of 'path' using 'strlen'.
    // If the length of path is 0 or larger than 'MAXPATHLEN', return
    // -E_BAD_PATH.
    /* Exercise 5.12: Your code here. (1/3) */
    u_long len = strlen(path);

    if ((len == 0) || (len > MAXPATHLEN)) {
        return -E_BAD_PATH;
    }

    // Step 2: Use 'fsipcbuf' as a 'struct Fsreq_remove'.
    struct Fsreq_remove *req = (struct Fsreq_remove *)fsipcbuf;

    // Step 3: Copy 'path' into the path in 'req' using 'strcpy'.
    /* Exercise 5.12: Your code here. (2/3) */

    strcpy(req->req_path, path);

    // Step 4: Send request to the server using 'fsipc'.
    /* Exercise 5.12: Your code here. (3/3) */
    return fsipc(FSREQ_REMOVE, (void *)req, 0, 0);
}

/*
 * 概述：
 *   请求文件系统同步。强制将内存中所有脏块写入磁盘，
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
int fsipc_sync(void) { return fsipc(FSREQ_SYNC, fsipcbuf, 0, 0); }
