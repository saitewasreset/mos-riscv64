#ifndef _ENV_H_
#define _ENV_H_

#include <mmu.h>
#include <queue.h>
#include <trap.h>
#include <types.h>

// log2(Env总数量)
#define LOG2NENV 10
// Env总数量
#define NENV (1 << LOG2NENV)
// 从envid中提取env下标（低10位），可用于访问`envs`数组
#define ENVX(envid) ((envid) & (NENV - 1))

// `Env::env_status`的所有可能取值
#define ENV_FREE 0
#define ENV_RUNNABLE 1
#define ENV_NOT_RUNNABLE 2

/*
 * 进程创建步骤(`env_create`)
 *
 * 1. 申请一个空闲的 PCB：`env_create` -> `env_alloc`
 *
 * 2. 手工初始化进程控制块：`env_create` -> `env_alloc` -> `asid_alloc`
 *
 * 3. 为新进程初始化页目录：
 *    - 只读的pages、envs、User VPT区域：`env_create` -> `env_alloc` ->
 `env_setup_vm`
 *    - 映射二进制镜像、设置入口位置（EPC）：`env_create` -> `load_icode`
 *
 * 4. 运行进程：将进程加入调度队列. `env_create`
*/

// 设置/重置时钟中断：`env_run` -> `env_pop_tf`

// 环境（进程）的控制块
struct Env {
    struct Trapframe
        env_tf; // 上次上下文切换时，保存的当前进程的上下文：通用寄存器、hi、lo、status、epc、cause、badvaddr
    LIST_ENTRY(Env) env_link; // 用于空闲Env链表(`env_free_list`)的指针域
    u_int
        env_id; // 进程id，注意：即使不同进程复用了同一个Env，他们的`env_id`也不同
    uint16_t env_asid;   // 该Env的ASID
    u_int env_parent_id; // 该Env父进程的**env_id**
    u_int env_status;    // 该Env的状态：ENV_FREE/ENV_RUNNABLE/ENV_NOT_RUNNABLE
    Pte *env_pgdir;      // 该Env的页目录地址（虚拟地址）
    TAILQ_ENTRY(Env) env_sched_link; // 用于调度队列(`env_sched_list`)的指针域
    u_int env_pri;                   // 调度优先级

    // Lab 4 IPC
    u_int env_ipc_value; // IPC发送方发送的值
    u_int env_ipc_from;  // IPC发送方的envid
    // 该Env是否正在阻塞地等待接收数据
    // 0 -> 不可接受数据 1 -> 等待接受数据中
    u_int env_ipc_recving;
    // 接收到的页面需要与自身哪个虚拟页面完成映射
    u_int env_ipc_dstva;
    // 传递的页面的权限位
    u_int env_ipc_perm;

    // Lab 4 fault handling
    u_int env_user_tlb_mod_entry; // userspace TLB Mod handler

    // Lab 6 scheduler counts
    u_int env_runs; // number of times we've been env_run'ed
};

LIST_HEAD(Env_list, Env);
TAILQ_HEAD(Env_sched_list, Env);
extern struct Env *curenv; // 当前运行的Env，定义在`env.c`中，由`env_run`修改
extern struct Env_sched_list
    env_sched_list; // 调度队列，只应含有`ENV_RUNNABLE`状态的Env，定义在`env.c`中，由`env_init`初始化

/*
 * 概述：
 *
 *   初始化所有Env结构体为空闲，将其加入空闲Env链表，并初始化调度队列。设置页目录模板，
 *   映射用户空间只读区域（UPAGES/UENVS）。
 *
 *   具体地，空闲链表中Env的顺序和全局变量`envs`数组中的相同。
 *
 * Precondition：
 * - 全局变量`envs`数组必须分配且大小为NENV
 * - 物理页管理子系统已初始化（`page_init`已执行），确保`page_alloc`可用
 *
 * Postcondition：
 * - 所有Env状态为ENV_FREE，并按数组顺序插入`env_free_list`（头节点为envs[0]）
 * - `env_sched_list`被初始化为空队列
 * - `base_pgdir`建立映射：UPAGES映射到物理页`pages`数组，UENVS映射到`envs`数组
 *   映射属性包含PTE_G（全局映射），用户空间可读但不可写
 *
 * 副作用：
 * - 修改全局变量`env_free_list`的链表结构
 * - 修改全局变量`env_sched_list`的队列结构
 * - 修改所有Env结构体的`env_status`字段
 * - 分配物理页用于`base_pgdir`，增加对应Page的`pp_ref`计数
 * - 建立部分物理页的映射（通过map_segment）
 */
void env_init(void);

/*
 * 概述：
 *
 *   分配并初始化一个新的Env结构体。
 *   成功时，新Env将存储在'*new'中。
 *
 * Precondition：
 * - 若新Env没有父进程，'parent_id'应为0
 * - 'env_init'必须已被调用，以初始化Env相关数据结构
 * - 全局变量'env_free_list'必须包含有效的空闲Env链表
 * - 全局变量'envs'数组必须已正确初始化
 * - 'asid_alloc'依赖的ASID管理数据结构必须已初始化（`asid_bitmap`）
 *
 * Postcondition：
 * - 成功时返回0，新Env的以下字段被初始化：
 *   'env_id', 'env_asid', 'env_parent_id', 'env_tf.regs[29]',
 * 'env_tf.cp0_status', 'env_user_tlb_mod_entry', 'env_runs'
 * - 失败时返回错误码：
 *   -E_NO_FREE_ENV：无空闲Env可用
 *   -E_NO_MEM：内存分配失败（env_setup_vm）
 *   -E_NO_FREE_ENV：ASID分配失败（asid_alloc）
 *   失败时目标Env不会从空闲链表移除
 *
 * 副作用：
 * - 成功时修改全局变量'env_free_list'（移除分配的Env）
 * - 成功时可能修改ASID分配状态（通过asid_alloc）
 * - 成功时可能修改物理页管理状态（通过env_setup_vm分配页目录）
 */
int env_alloc(struct Env **e, u_int parent_id);

/*
 * 概述：
 *
 *   （含 TLB 操作）释放环境e及其使用的所有内存资源。包括：
 *   - 用户地址空间的所有映射页（只含到UTOP的页不包括映射的pages、envs、User
 * VPT）
 *   - 页表结构
 *   - 页目录结构
 *   - ASID资源
 *   - TLB相关项
 *
 *   将进程从调度队列移除。
 *   最后将环境结构体归还到空闲环境链表。
 *
 * Precondition：
 * - e必须指向有效的Env结构体
 * - e的env_status必须不是ENV_FREE（即处于运行或不可运行状态）
 * - 全局变量env_free_list必须已初始化（空闲环境链表）
 * - 全局变量env_sched_list必须已初始化（调度队列）
 * - 全局变量curenv必须正确反映当前运行环境（可能为NULL）
 *
 * Postcondition：
 * - e的所有内存资源（页表、页目录、用户页）已被释放
 * - e的env_status被设为ENV_FREE
 * - e被插入env_free_list头部
 * - e从env_sched_list中移除（如果存在）
 * - 对应的ASID被释放回系统
 * - TLB中所有相关映射被无效化
 *
 * 副作用：
 * - 修改全局变量env_free_list（插入释放的环境）
 * - 修改全局变量env_sched_list（移除环境）
 * - 可能修改物理页管理状态（通过page_decref）
 * - 可能修改ASID分配状态（通过asid_free）
 * - 修改TLB状态（通过tlb_invalidate）
 * - 修改环境结构体的状态字段（env_status）
 */
void env_free(struct Env *);
/*
 * 概述：
 *   创建一个具有指定二进制镜像和优先级的新环境。
 *   该函数主要用于内核初始化阶段创建早期环境，在第一个环境被调度之前使用。
 *   通过加载ELF可执行镜像初始化环境地址空间，并将其插入调度队列。
 *
 * Precondition：
 * - 'binary'必须指向内存中的有效ELF可执行镜像（通过elf_from校验）
 * - 'size'必须等于ELF文件的实际大小
 * - 'priority'必须为有效优先级值
 * - asid_bitmap必须有可用ASID（asid_alloc）
 * - 物理页管理器必须能分配足够页面（用于页目录和ELF加载）
 * - env_free_list必须已正确初始化（`env_init`）
 * - env_free_list必须包含至少一个空闲Env结构
 * - env_sched_list必须已正确初始化（`env_init`）
 *
 * Postcondition：
 * - 成功时返回新创建Env指针，其状态为ENV_RUNNABLE，并插入调度队列头部
 * - 新Env具有以下特征：
 *   - 父ID为0
 *   - 分配了唯一env_id和ASID
 *   - 页目录包含内核空间映射和ELF段映射
 *   - EPC寄存器设置为ELF入口地址
 * - 失败时返回NULL（可能因无空闲Env或内存不足）
 *
 * 副作用：
 * - 修改全局变量env_free_list（通过env_alloc移出空闲Env）
 * - 修改全局变量env_sched_list（插入新Env）
 * - 可能修改ASID分配状态（通过asid_alloc）
 * - 可能分配物理页（通过env_setup_vm和load_icode）
 * - 若ELF校验失败触发panic（通过load_icode）
 */
struct Env *env_create(const void *binary, size_t size, uint32_t priority);
/*
 * 概述：
 *
 *   释放环境e，并调度新环境运行（若e是当前环境）。具体操作包括：
 *   - 调用env_free释放e占用的所有资源（内存、ASID、页表等）
 *   - 若e是当前运行环境（curenv），将curenv置为NULL并触发调度
 *
 * Precondition：
 * - e必须指向有效的Env结构体，且e->env_status != ENV_FREE
 * - 全局变量curenv必须正确反映当前运行环境（可能为NULL）
 * - 全局变量env_free_list必须已初始化（空闲环境链表）
 * - 全局变量env_sched_list必须已初始化（调度队列）
 *
 * Postcondition：
 * - e的资源被完全释放，其状态变为ENV_FREE，并被插入env_free_list头部
 * - 若e是当前环境：
 *   - curenv被置为NULL
 *   - 调度器被触发选择新环境运行（可能发生上下文切换）
 * - e从env_sched_list中移除（若原先存在）
 *
 * 副作用：
 * - 修改全局变量env_free_list（插入释放的Env）
 * - 修改全局变量env_sched_list（移除e）
 * - 可能修改物理页管理状态（通过env_free调用page_decref）
 * - 可能修改ASID分配状态（通过env_free调用asid_free）
 * - 修改TLB状态（通过env_free调用tlb_invalidate）
 * - 若e是当前环境，修改curenv为NULL并触发调度流程
 */
void env_destroy(struct Env *e);

/*
 * 概述：
 *   将给定的环境ID（envid）转换为对应的Env结构体指针。
 *   如果envid为0，则将*penv设置为当前环境（curenv）；否则设置为envs[ENVX(envid)]。
 *   当checkperm非零时，要求目标环境必须是当前环境或其直接子环境。
 *
 * Precondition：
 * - penv必须指向有效的struct Env*指针
 * - 依赖全局变量：
 *   - curenv：当前运行环境控制块
 *   - envs：全局环境控制块数组
 *   - 要求envs已正确初始化且curenv有效（若checkperm非零）
 *
 * Postcondition：
 * - 成功时返回0，并将*penv设置为目标Env指针
 * - 失败时返回-E_BAD_ENV（当envid无效或权限检查失败）
 *
 * 副作用：
 * - 当checkperm非零但curenv为NULL时，触发panic
 */
int envid2env(u_int envid, struct Env **penv, int checkperm);

/* 概述:
 *   将 CPU 上下文切换到指定进程环境 'e'。
 *   这将改变全局变量`curenv`的值。
 *
 *   这将设置/重置时钟中断（在`env_pop_tf`中）。
 *
 * Precondition:
 *   - e->env_status 必须为 ENV_RUNNABLE (通过 assert 验证)
 *   - 如果当前存在运行环境 (curenv != NULL)，必须保证 KSTACKTOP - sizeof(struct
 * Trapframe) 处已保存当前环境的上下文
 *   - e->env_pgdir 必须指向有效的页目录
 *   - e->env_tf 必须包含有效的陷阱帧信息
 *   - 依赖全局变量 curenv 的当前状态（可能为 NULL）
 *
 * Postcondition:
 *   - e 将被设置为当前运行环境 (curenv = e)
 *   - 环境运行计数器 env_runs 自增
 *   - 通过 env_pop_tf 恢复 e->env_tf 保存的上下文
 *   - 控制转移到用户代码（函数不会返回）
 *
 * 副作用:
 *   - 修改全局变量 curenv 的值
 *   - 修改全局页目录指针 cur_pgdir
 *   - 增加 e->env_runs 的计数器值
 *   - 可能修改原 curenv 的 env_tf 字段（当 curenv != NULL 时）
 *   - 设置/重置时钟中断
 *
 * 实现步骤:
 *   1. 上下文保存: 若存在当前环境，将其内核栈顶的陷阱帧保存到 curenv->env_tf
 *   2. 环境切换: 更新 curenv 指针并增加运行计数
 *   3. 地址空间切换: 更新当前页目录指针
 *   4. 上下文恢复: 通过 env_pop_tf 加载新环境的陷阱帧
 *
 * 注意:
 *   - env_pop_tf 是 noreturn 函数：通过恢复 cp0_epc 寄存器、eret跳转到用户模式
 *   - 必须使用 curenv->env_asid 设置 TLB 的 ASID
 *   - KSTACKTOP 处的陷阱帧布局必须与 struct Trapframe 严格匹配
 */
void env_run(struct Env *e) __attribute__((noreturn));

void env_check(void);
void envid2env_check(void);

/*
 * 概述：
 *   通过指定的二进制镜像和优先级创建新环境。该宏利用外部链接符号获取ELF镜像信息，
 *   并调用env_create完成环境初始化及调度队列插入操作。
 *   适用于内核初始化阶段创建早期可运行环境。
 *
 * Precondition：
 * - 存在外部符号binary_x_start和binary_x_size，分别表示ELF镜像的起始地址和大小
 * - binary_x_start指向有效的ELF可执行镜像（通过elf_from校验）
 * - binary_x_size必须等于ELF文件的实际大小
 * - 参数y为合法的优先级值（符合调度策略定义）
 * - asid_bitmap中存在可分配的ASID（通过asid_alloc）
 * - env_free_list已正确初始化且包含至少一个空闲Env结构（env_init完成）
 * - 物理页管理器可分配足够页（用于页目录和ELF段映射）
 *
 * Postcondition：
 * - 成功时返回Env指针，其状态为ENV_RUNNABLE并插入env_sched_list头部
 * - 新Env特征：
 *   - env_id和ASID唯一
 *   - 页目录包含内核空间映射及ELF段映射
 *   - EPC寄存器指向ELF入口地址
 * - 失败返回NULL（可能因无空闲Env或内存不足）
 *
 * 副作用：
 * - 修改env_free_list：移出分配的Env结构
 * - 修改env_sched_list：插入新Env到队列头部
 * - 分配ASID（修改asid_bitmap状态）
 * - 分配物理页（用于页目录和ELF加载）
 * - ELF校验失败触发panic
 */
#define ENV_CREATE_PRIORITY(x, y)                                              \
    ({                                                                         \
        extern u_char binary_##x##_start[];                                    \
        extern u_int binary_##x##_size;                                        \
        env_create(binary_##x##_start, (u_int)binary_##x##_size, y);           \
    })

/*
 * 概述：
 *   通过指定的二进制镜像创建新环境，使用默认优先级1。功能与ENV_CREATE_PRIORITY相同，
 *   仅优先级参数固定为1。
 *
 * Precondition：
 * - 同ENV_CREATE_PRIORITY，但优先级参数固定为1（无需校验y的有效性）
 *
 * Postcondition：
 * - 同ENV_CREATE_PRIORITY
 *
 * 副作用：
 * - 同ENV_CREATE_PRIORITY
 */
#define ENV_CREATE(x)                                                          \
    ({                                                                         \
        extern u_char binary_##x##_start[];                                    \
        extern u_int binary_##x##_size;                                        \
        env_create(binary_##x##_start, (u_int)binary_##x##_size, 1);           \
    })

#endif // !_ENV_H_
