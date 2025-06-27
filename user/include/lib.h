#ifndef LIB_H
#define LIB_H
#include <args.h>
#include <env.h>
#include <fd.h>
#include <mmu.h>
#include <pmap.h>
#include <syscall.h>
#include <trap.h>

// 用户空间对进程自身三级页表的只读访问，可直接由VPN索引
#define vp3 ((const volatile Pte *)UVPT)
// 用户空间对进程自身二级页表的只读访问，可由(P1X(va) << 9) | P2X(va)索引
#define vp2 ((const volatile Pte *)(UVPT + (UVPT >> 9)))
// 用户空间对进程自身一级页表的只读访问，可由P1X(va)索引
#define vp1 ((const volatile Pte *)(UVPT + (UVPT >> 9) + (UVPT >> 18)))

// 用户空间对**所有**Env结构的只读访问
#define envs ((const volatile struct Env *)UENVS)
// 用户空间对**所有**物理页**结构**的只读访问
#define pages ((const volatile struct Page *)UPAGES)

/*
 * 用户空间系统调用过程：
 *
 * syscall_* -> msyscall(调用号，参数...) -> syscall指令
 * -> handle_sys -> do_syscall -> sys_*
 */

// libos
void exit(void) __attribute__((noreturn));

extern const volatile struct Env *env;

#define USED(x) (void)(x)

// debugf
void debugf(const char *fmt, ...);

void _user_panic(const char *, int, const char *, ...)
    __attribute__((noreturn));
void _user_halt(const char *, int, const char *, ...) __attribute__((noreturn));

#define user_panic(...) _user_panic(__FILE__, __LINE__, __VA_ARGS__)
#define user_halt(...) _user_halt(__FILE__, __LINE__, __VA_ARGS__)

#undef panic_on
#define panic_on(expr)                                                         \
    do {                                                                       \
        int r = (expr);                                                        \
        if (r != 0) {                                                          \
            user_panic("'" #expr "' returned %d", r);                          \
        }                                                                      \
    } while (0)

/// fork, spawn
int spawn(char *prog, char **argv);
int spawnl(char *prot, char *args, ...);
/*
 * 概述：
 *   用户级fork实现，创建子进程并复制父进程地址空间。
 *   设置父、子进程的TLB修改异常处理入口为cow_entry以实现写时复制。
 *   通过复制父进程页表项实现地址空间共享，并标记COW页。
 *
 * 实现差异：
 * - 相比参考实现，增加了对syscall_exofork错误的处理
 *
 * Precondition：
 * - 依赖全局状态：
 *   - env：当前进程控制块（必须有效）
 *   - vpt/vpd：当前进程页表/页目录的用户空间只读视图（必须有效）
 *   - envs：全局环境控制块数组（用于子进程初始化）
 * - 当前进程必须已设置有效的页表结构
 * - 系统必须有足够的资源创建新进程（空闲Env结构、物理内存等）
 *
 * Postcondition：
 * - 成功时：
 *   - 返回子进程ID（大于0）
 *   - 子进程环境控制块正确初始化
 *   - 父子进程共享COW页，TLB异常处理设置为cow_entry
 * - 失败时：
 *   - 返回负的错误代码（如-E_NO_FREE_ENV等）
 *
 * 副作用：
 * - 分配新的Env结构（通过syscall_exofork）
 * - 修改父子进程的页表项（通过duppage）
 * - 修改父子进程的TLB异常处理入口（通过syscall_set_tlb_mod_entry）
 * - 可能修改物理页引用计数（通过duppage内部处理）
 */
int fork(void);

/// syscalls
extern int msyscall(int, ...);

/*
 * 概述：
 *   该函数用于在屏幕上打印一个字符。它是通过调用底层字符打印函数printcharc实现的简单封装。
 *
 * Precondition：
 *   - 参数'c'应为有效的ASCII字符或转义字符（如'\n'）
 *   - 依赖全局硬件状态：MALTA串口设备（通过printcharc间接访问）
 *
 * Postcondition：
 *   - 字符会被输出到系统控制台
 *   - 若字符是换行符('\n')，会自动先输出回车符('\r')
 *
 * 副作用：
 *   - 会修改MALTA串口设备的硬件状态（通过printcharc）
 */
void syscall_putchar(int ch);
/*
 * 概述：
 *   该函数用于在控制台上打印指定长度的字节字符串。
 *   函数会逐个字符调用printcharc()输出到控制台，
 *   并在执行前对输入参数进行有效性检查。
 *
 * Precondition：
 *   - 参数`s`必须指向有效的内存地址，是待打印字符串的基地址
 *   - 参数`num`必须是字符串的有效长度（字节数）
 *   - 字符串范围[s, s+num)必须在用户地址空间内（低于UTOP）
 *   - 不依赖全局变量
 *
 * Postcondition：
 *   - 若参数检查通过，字符串内容将被输出到控制台
 *   - 返回值为0表示成功，-E_INVAL表示参数无效
 *
 * 副作用：
 *   - 通过printcharc()向控制台输出字符
 *   - 可能修改控制台设备状态（通过printcharc的串口操作）
 *
 * 关键点注释：
 *   - 地址检查条件包含三种无效情况：
 *     1. 字符串跨越UTOP边界
 *     2. 起始地址本身超过UTOP
 *     3. 起始地址大于结束地址（整数溢出情况）
 *   - printcharc()会自动处理换行符转换（\n -> \r\n）
 */
int syscall_print_cons(const void *str, size_t num);
/*
 * 概述：
 *   获取当前进程的环境ID（env_id）。
 *   环境ID是进程的唯一标识符，即使不同进程复用了同一个Env结构体，其env_id也不同。
 *
 * Precondition：
 * - 全局变量curenv必须已正确初始化，且指向当前运行进程的Env结构体
 * - curenv不能为NULL（即必须有正在运行的进程）
 *
 * Postcondition：
 * - 返回当前进程的环境ID（curenv->env_id）
 *
 * 副作用：
 * - 无副作用（不修改任何全局状态）
 */
uint32_t syscall_getenvid(void);
/*
 * 概述：
 *   主动放弃剩余的CPU时间片，触发进程调度。
 *   该函数通过调用schedule(1)实现主动让出CPU，并确保不会返回。
 *
 * Precondition：
 * - 全局变量curenv必须指向当前运行的环境结构体
 *
 * Postcondition：
 * - 系统将调度另一个可运行环境执行
 * - 当前环境会被移动到调度队列尾部
 *
 * 副作用：
 * - 通过schedule函数间接修改全局变量curenv
 * - 可能调整env_sched_list队列结构
 *
 */
void syscall_yield(void);
/*
 * 概述：
 *   销毁指定进程环境（envid对应的Env结构体），释放其占用的所有资源。
 *   该函数会终止目标进程的运行，并回收其内存、页表、ASID等资源。
 *
 * Precondition：
 * - 参数`envid`必须是有效的进程ID，且满足以下条件之一：
 *   - 是当前进程的ID（即允许进程自我销毁）
 *   - 是当前进程的直接子进程ID
 * - 依赖全局状态：
 *   - curenv：当前运行环境控制块（用于权限检查）
 *   - envs：全局环境控制块数组（用于envid2env查找）
 *
 * Postcondition：
 * - 成功时返回0，并完成以下操作：
 *   - 目标进程的所有资源被释放（内存、页表、ASID等）
 *   - 目标进程从调度队列中移除（如果存在）
 *   - 目标进程状态被设为ENV_FREE并加入空闲链表
 * - 失败时返回原始错误码（来自底层函数调用）
 *
 * 副作用：
 * - 修改全局变量env_free_list（将释放的Env加入空闲链表）
 * - 修改全局变量env_sched_list（从调度队列移除目标Env）
 * - 可能修改物理页管理状态（通过env_destroy内部调用page_decref）
 * - 可能修改ASID分配状态（通过env_destroy内部调用asid_free）
 * - 修改TLB状态（通过env_destroy内部调用tlb_invalidate）
 * - 若目标进程是当前进程，会触发调度流程选择新进程运行
 */
int syscall_env_destroy(uint32_t envid);
/*
 * 概述：
 *   注册用户空间TLB Mod异常处理函数的入口地址。
 *   该函数用于设置指定进程的用户空间页写入异常(TLB Mod)处理函数的入口地址，
 *   当该进程触发页写入异常时，内核会跳转到该地址执行用户自定义处理逻辑。
 *
 *   注意，处理函数将在用户态运行。
 *   与常规系统调用不同，该函数不直接处理异常，而是设置用户态处理入口
 *   用户态处理函数需自行完成写时复制等操作
 *
 * Precondition：
 * - 'envid'必须是有效的进程ID（0表示当前进程），且为当前进程或其直接子进程
 * - 'func'必须是合法的用户空间虚拟地址
 * - 依赖全局状态：
 *   - envs：全局环境控制块数组（用于envid2env查找）
 *   - curenv：当前运行环境（当checkperm非零时用于权限检查）
 *
 * Postcondition：
 * - 成功时返回0，并完成以下操作：
 *   - 目标进程的'env_user_tlb_mod_entry'字段被设置为'func'
 * - 失败时返回相应错误代码：
 *   - -E_BAD_ENV：进程ID无效或权限检查失败
 *
 * 副作用：
 * - 修改目标进程控制块的env_user_tlb_mod_entry字段
 */
int syscall_set_tlb_mod_entry(uint32_t envid, void (*func)(struct Trapframe *));
/*
 * 概述：
 *   （含 TLB 操作）为指定进程分配物理内存页并建立虚拟地址映射。
 *   若目标虚拟地址已存在映射，则静默解除原映射后建立新映射。
 *   权限检查要求目标进程必须是调用者或其子进程（通过envid2env的checkperm实现）。
 *
 * 实现差异：
 *   - 仅使用用户传入perm参数的低12位
 *   - 强制移除PTE_C_CACHEABLE和PTE_V标志（由page_insert内部添加）
 *
 * Precondition：
 * - envid必须是有效的进程ID（0表示当前进程），且为当前进程或其直接子进程
 * - 'va'必须是用户态虚拟地址（UTEMP <= va < UTOP）
 * - 依赖全局状态：
 *   - curenv：用于权限校验（当checkperm=1时）
 *   - envs：全局环境控制块数组
 *   - page_free_list：空闲物理页链表
 *
 * Postcondition：
 * - 成功时：
 *   - 返回0
 *   - 为envid对应进程建立va到新物理页的映射
 *   - 新物理页内容清零
 * - 失败时：
 *   - 返回-E_BAD_ENV：envid无效或权限不足
 *   - 返回-E_INVAL：va非法
 *   - 返回-E_NO_MEM：物理内存不足
 *
 * 副作用：
 * - 可能修改目标进程的页表结构
 * - 可能使目标进程TLB条目失效
 * - 可能减少原映射页面的引用计数（若va已映射）
 * - 增加新分配页面的引用计数
 */
int syscall_mem_alloc(uint32_t envid, void *va, uint32_t perm);
/*
 * 概述：
 *   （含 TLB 操作）将源进程(srcid)地址空间中'srcva'处的物理页
 *   映射到目标进程(dstid)地址空间中'dstva'处，
 *   并设置权限位'perm'。映射后两个进程共享同一物理页。
 *
 * 实现差异：
 * - 仅取用户传入的perm低12位，并手动移除PTE_C_CACHEABLE和PTE_V标志位，
 *   以满足page_insert函数的Precondition要求
 *
 * Precondition：
 * -
 * `srcid`、`dstid`必须是有效的进程ID（0表示当前进程），且为当前进程或其直接子进程
 * - 'srcva'和'dstva'必须是合法的用户虚拟地址（UTEMP <= va < UTOP）
 * - 依赖全局状态：
 *   - envs：全局环境控制块数组（用于envid2env查找）
 *   - curenv：当前运行环境（当checkperm非零时用于权限检查）
 *
 * Postcondition：
 * - 成功时返回0，并完成以下操作：
 *   - 目标进程的'dstva'被映射到源进程'srcva'对应的物理页
 *   - 若'dstva'原有映射，则原映射被清除且引用计数正确修改
 * - 失败时返回相应错误代码：
 *   - -E_BAD_ENV：源或目标进程ID无效或权限检查失败
 *   - -E_INVAL：虚拟地址非法或源地址未映射
 *   - 其他：底层函数调用失败时返回原始错误码
 *
 * 副作用：
 * - 可能修改目标进程的页表结构
 * - 可能修改物理页的引用计数（通过page_insert内部处理）
 * - 若目标地址原有映射，会清除该映射并减少原物理页的引用计数
 */
int syscall_mem_map(uint32_t srcid, void *srcva, uint32_t dstid, void *dstva,
                    uint32_t perm);
/*
 * 概述：
 *   （含 TLB 操作）在进程envid的地址空间中解除虚拟地址va的物理页映射。
 *   如果该地址没有映射物理页，函数静默成功。
 *
 *   该函数通过调用page_remove实现，会执行TLB无效化操作。
 *
 * Precondition：
 * - envid必须是有效的进程ID（0表示当前进程），且为当前进程或其直接子进程
 * - va必须是合法的用户虚拟地址（UTEMP <= va < UTOP）
 * - va可以已被映射或未被映射
 * - 依赖全局变量：
 *   - curenv：用于envid为0时的当前进程判断
 *   - envs：全局进程控制块数组
 *
 * Postcondition：
 * - 成功时返回0
 * - 若`va`合法但未被映射，也返回0
 * - 失败时返回：
 *   - -E_BAD_ENV：envid无效或权限检查失败（通过envid2env检查）
 *   - -E_INVAL：va是非法用户虚拟地址
 *   - 其他底层调用返回的错误码
 *
 * 副作用：
 * - 若va存在有效映射：
 *   - 解除页表映射
 *   - 减少物理页的引用计数(pp_ref)
 *   - 无效化TLB中对应条目
 * - 修改进程envid的页表结构（通过page_remove）
 */
int syscall_mem_unmap(uint32_t envid, void *va);
/*
 * 概述：
 *   分配一个新的Env作为当前环境(curenv)的子进程，并初始化其运行上下文。
 *   该函数是用户空间fork和spawn实现的关键步骤，用于创建子进程的初始状态。
 *
 *   该函数只实现如下操作，其它操作需要由用户态进行：
 *   - 分配新的Env：基本参数，复制页目录模板
 *   - 复制父进程的上下文
 *   - 设置新进程的优先级与父进程相同
 *   - 阻塞新进程的执行
 *
 *   例如，以下操作需由用户态处理：
 *   - 对于子进程，更新其用户空间中的指向Env的指针
 *   - 复制父进程的地址空间（映射到同一物理页）
 *   - 在父进程、子进程页中设置CoW软件标志
 *   - 设置CoW处理入口（TLB Mod），处理写时复制
 *   - 恢复子进程的执行
 *
 * Precondition：
 * - 全局变量curenv必须非NULL（表示存在当前运行环境）
 * - env_alloc依赖的全局状态必须已初始化（env_free_list、envs数组等）
 * - 内核栈顶必须包含有效的Trapframe结构（位于KSTACKTOP - sizeof(Trapframe)）
 *
 * Postcondition：
 * - 成功时返回子进程的envid，并满足：
 *   - 子进程的env_tf复制自父进程内核栈的Trapframe（除$v0设为0）
 *   - 子进程状态为ENV_NOT_RUNNABLE
 *   - 子进程优先级与父进程相同
 * - 失败时返回底层调用产生的错误码
 *
 * 副作用：
 * - 修改全局空闲Env链表（通过env_alloc）
 * - 可能修改ASID分配状态（通过env_alloc内部调用asid_alloc）
 * - 分配新页目录（通过env_alloc内部调用env_setup_vm，复制页目录模板）
 */
__attribute__((always_inline)) inline static int syscall_exofork(void) {
    return msyscall(SYS_exofork, 0, 0, 0, 0, 0);
}
/*
 * 概述：
 *   设置指定进程（envid）的状态（status），并根据状态变化更新调度队列（env_sched_list）。
 *   该函数需维护调度队列的一致性：该队列仅包含且必须包含所有 ENV_RUNNABLE
 * 状态的进程。
 *
 * Precondition：
 * - envid 必须指向有效的进程控制块（通过 envid2env
 * 校验），且为当前进程或其直接子进程
 * - status 必须是 ENV_RUNNABLE 或 ENV_NOT_RUNNABLE
 * - 依赖全局状态：
 *   - env_sched_list：调度队列（必须仅包含 ENV_RUNNABLE 状态的进程）
 *   - curenv：当前运行进程（用于检查是否需要触发调度）
 *
 * Postcondition：
 * - 成功时返回 0，并完成以下操作：
 *   - 目标进程的 env_status 被设置为 status
 *   - 若状态发生变化（ENV_NOT_RUNNABLE <-> ENV_RUNNABLE），调度队列被正确更新
 *   - 若目标进程是当前进程，触发调度
 * - 失败时返回错误码：
 *   - -E_INVAL：status 参数非法（非 ENV_RUNNABLE/ENV_NOT_RUNNABLE）
 *   - 其他：envid2env 等底层调用失败时返回原始错误码
 *
 * 副作用：
 * - 可能修改目标进程的 env_status 字段
 * - 可能修改全局调度队列 env_sched_list 的结构
 * - 若目标进程是当前进程，会触发调度流程（通过 schedule）
 */
int syscall_set_env_status(uint32_t envid, uint32_t status);
/*
 * 概述：
 *   设置指定进程(envid)的陷阱帧(trapframe)为给定的tf值。
 *   该函数用于修改目标进程的执行上下文，包括寄存器状态和异常处理相关信息。
 *   当目标进程是当前进程时，特殊处理返回值以避免覆盖寄存器状态
 *
 *   注意：若目标进程是当前进程，本系统调用将立即将本进程恢复到`tf`中的状态执行。
 *
 * Precondition：
 * - `envid`对应的进程必须是当前进程或其直接子进程
 * - tf必须指向合法的用户空间虚拟地址(UTEMP <= tf < UTOP)
 * - tf指向的内存区域必须可读且包含完整的Trapframe结构
 * - 依赖全局状态：
 *   - curenv：当前运行环境（用于判断目标进程是否为当前进程）
 *
 * Postcondition：
 * - 成功时：
 *   - 目标进程的env_tf被设置为*tf
 *   - 若目标进程是当前进程，返回tf->regs[2]（避免覆盖返回值寄存器）
 *   - 否则返回0
 * - 失败时返回错误码：
 *   - -E_INVAL：tf地址非法或envid无效
 *   - 其他：envid2env等底层调用失败时返回原始错误码
 *
 * 副作用：
 * - 修改目标进程的env_tf字段
 * - 若目标进程是当前进程，修改内核栈顶的陷阱帧内容
 */
int syscall_set_trapframe(uint32_t envid, struct Trapframe *tf);
/* Overview:
 * 	Kernel panic with message `msg`.
 *
 * Post-Condition:
 * 	This function will halt the system.
 */
void syscall_panic(const char *msg) __attribute__((noreturn));
/*
 * 概述：
 *   尝试向目标环境'envid'发送一个'value'值（如果'srcva'不为0，则同时发送一个页面）。
 *   实现差异：
 *   - `perm`只取低12位，并清除了`PTE_V`和`PTE_C_CACHEABLE`
 *   - 与参考实现不同，这里为`e->env_ipc_perm`补充了缺失的`PTE_C_CACHEABLE`标志
 *
 * Precondition：
 * - `envid`对应的进程可以是任意合法进程，无需是当前进程的直接子进程
 * - 全局变量`curenv`必须指向当前运行的环境
 * - 全局变量`env_sched_list`必须已正确初始化
 * - 如果'srcva'不为0，则必须指向当前环境的合法虚拟地址
 * - 'perm'参数的高20位将被忽略
 *
 * Postcondition：
 * - 成功时返回0，目标环境更新如下：
 *   - 'env_ipc_recving'设为0以阻止后续发送
 *   - 'env_ipc_from'设为发送者的envid
 *   - 'env_ipc_value'设为'value'
 *   - 'env_status'设为'ENV_RUNNABLE'以从'sys_ipc_recv'恢复
 *   -
 * 如果'srcva'不为NULL，将'env_ipc_dstva'映射到'curenv'中'srcva'对应的页面，权限为'perm'
 * - 返回-E_IPC_NOT_RECV如果目标环境未通过'sys_ipc_recv'等待IPC消息
 * - 返回底层调用失败时的原始错误码
 *
 * 副作用：
 * - 修改目标环境的以下字段：
 *   - env_ipc_value
 *   - env_ipc_from
 *   - env_ipc_perm
 *   - env_ipc_recving
 *   - env_status
 * - 可能修改全局调度队列`env_sched_list`（通过TAILQ_INSERT_TAIL）
 * - 如果'srcva'不为0，可能修改目标环境的页表（通过page_insert）
 *
 * 注意事项：
 * - 若srcva != 0但非法，将返回`-E_INVAL`，但此时接收者已被唤醒并继续执行
 * - 若srcva != 0但合法，dstva = 0，将导致接收者的[0x00000000, 0x00000FFF]被映射
 *   这被认为是系统设计缺陷，但保留该效果
 */
int syscall_ipc_try_send(uint32_t envid, uint64_t value, const void *srcva,
                         uint32_t perm);
/*
 * 概述：
 *   接收来自其他进程的消息（包含一个值和可选页面）。当前进程(curenv)将被阻塞，直到收到消息。
 *   若'dstva'不为0，表示同时接收一个页面并与该虚拟地址完成映射。
 *   该函数实现进程间通信(IPC)的接收端逻辑。
 *
 * Precondition：
 * - 全局变量'curenv'必须指向当前运行的有效进程控制块
 * - 全局变量'env_sched_list'必须已正确初始化
 * - 若'dstva'必须位于合法的用户空间，但无需按页对齐
 *
 * Postcondition：
 * - 成功时返回0，当前进程状态变为ENV_NOT_RUNNABLE并移出调度队列
 * - 失败时返回-E_INVAL，表示'dstva'既不是0也不是合法地址
 * - 设置当前进程的IPC相关字段：
 *   - env_ipc_recving = 1（标记为正在接收）
 *   - env_ipc_dstva = dstva（设置接收页面映射目标）
 *
 * 副作用：
 * - 修改当前进程状态为ENV_NOT_RUNNABLE
 * - 从调度队列'env_sched_list'中移除当前进程
 * - 修改当前进程的IPC相关字段
 * - 通过系统调用返回值寄存器(v0)设置返回值为0
 */
int syscall_ipc_recv(void *dstva);
int syscall_cgetc(void);
/*
 * 概述：
 *   系统调用函数，用于将用户空间从`va`开始长`len`的数据写入设备物理地址`pa`。
 *   根据数据长度选择对应的iowrite函数（8/16/32位）。
 *
 * Precondition：
 *   - len必须为1、2或4字节，否则返回-E_INVAL
 *   - 用户虚拟地址[va, va+len)必须合法（UTEMP <= x < UTOP）
 *   - 虚拟地址va必须按len字节对齐（len=2时2字节对齐，len=4时4字节对齐）
 *   - 物理地址pa必须属于有效设备范围（console或IDE控制器地址区间）
 *
 * Postcondition：
 *   - 成功时：用户数据[va, va+len)被写入物理地址pa，返回0
 *   - 失败时：返回-E_INVAL（参数非法或地址越界）
 *
 * 副作用：
 *   - 通过iowrite函数直接修改设备寄存器状态
 *   - 可能触发硬件设备的I/O操作（如串口输出、磁盘写入）
 *
 *  All valid device and their physical address ranges:
 *	* ---------------------------------*
 *	|   device   | start addr | length |
 *	* -----------+------------+--------*
 *	|  console   | 0x180003f8 | 0x20   |
 *	|  IDE disk  | 0x180001f0 | 0x8    |
 *	* ---------------------------------*
 */
int syscall_write_dev(u_reg_t va, u_reg_t pa, u_reg_t len);
/*
 * 概述：
 *   系统调用函数，用于从设备物理地址读取数据到用户空间。根据数据长度选择对应的ioread函数（8/16/32位）。
 *
 * Precondition：
 *   - len必须为1、2或4字节，否则返回-E_INVAL
 *   - 用户虚拟地址[va, va+len)必须合法（UTEMP <= x < UTOP）
 *   - 虚拟地址va必须按len字节对齐（len=2时2字节对齐，len=4时4字节对齐）
 *   - 物理地址pa必须属于有效设备范围（console或IDE控制器地址区间）
 *
 * Postcondition：
 *   - 成功时：设备数据从pa读取到用户空间[va, va+len)，返回0
 *   - 失败时：返回-E_INVAL（参数非法或地址越界）
 *
 * 副作用：
 *   - 可能改变设备状态（如读取状态寄存器可能清除中断标志）
 *   - 通过直接内存访问操作设备寄存器
 *
 * 关键点：
 *   - 通过KSEG1地址空间进行非缓存访问
 *
 *  All valid device and their physical address ranges:
 *	* ---------------------------------*
 *	|   device   | start addr | length |
 *	* -----------+------------+--------*
 *	|  console   | 0x180003f8 | 0x20   |
 *	|  IDE disk  | 0x180001f0 | 0x8    |
 *	* ---------------------------------*
 */
int syscall_read_dev(u_reg_t va, u_reg_t pa, u_reg_t len);

void syscall_map_user_vpt(void);
void syscall_unmap_user_vpt(void);

void syscall_sleep(void);
int syscall_set_interrupt_handler(uint32_t interrupt_code, u_reg_t handler_va);

int syscall_get_device_count(char *device_type);

int syscall_get_device(char *device_type, size_t idx, size_t max_data_len,
                       u_reg_t out_device, u_reg_t out_device_data);

void syscall_interrupt_return(void);

int syscall_get_process_list(int max_len, u_reg_t out_process_list);

// ipc.c
int ipc_send(uint32_t whom, uint64_t val, const void *srcva, uint32_t perm);
int ipc_recv(uint32_t *whom, uint64_t *out_val, void *dstva, uint32_t *perm);

// wait.c
void wait(uint32_t envid);

// console.c
int opencons(void);
int iscons(int fdnum);

// pipe.c
int pipe(int pfd[2]);
int pipe_is_closed(int fdnum);

// pageref.c
/*
 * 概述：
 *   获取指定虚拟地址映射的物理页的全局引用计数。通过两级页表结构（页目录+页表）
 *   验证地址有效性后，查询物理页结构中的引用计数字段。
 *
 * Precondition：
 *   - 全局页目录vpd和页表vpt必须正确映射，反映当前地址空间状态
 *   - 全局物理页结构数组pages需可访问且与内存管理系统同步
 *
 * Postcondition：
 *   - 地址有效：返回该物理页在所有进程中的虚拟映射总数（pp_ref）
 *   - 地址无效（页目录/页表项无效）：返回0
 *
 * 副作用：
 *   无副作用，仅进行状态查询
 *
 * 关键点：
 *   - 两级验证机制：先检查页目录项PTE_V，再检查页表项PTE_V，避免无效访问
 *   - PPN宏从页表项中提取物理页号，依赖硬件页表格式
 *   - pages数组通过物理页号索引，直接访问全局物理页元数据
 *   - 返回值为全局引用计数，包含所有进程的映射情况
 */
int pageref(void *);

// fprintf.c
int fprintf(int fd, const char *fmt, ...);
int printf(const char *fmt, ...);

// fsipc.c
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
int fsipc_open(const char *path, u_int omode, struct Fd *fd);
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
int fsipc_map(u_int fileid, u_int offset, void *dstva);
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
 *   - 失败：通过IPC返回错误码（E_INVAL等）
 *
 * 副作用：
 *   - 可能触发文件截断操作，释放磁盘空间
 *   - 若文件包含父目录，会刷新文件元数据到磁盘
 *
 * 关键点：
 *   - 文件尺寸扩展时不预分配存储，依赖后续获取/写入操作
 */
int fsipc_set_size(u_int fileid, u_int size);
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
int fsipc_close(u_int fileid);
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
int fsipc_dirty(u_int fileid, u_int offset);
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
int fsipc_remove(const char *path);
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
 *   - 无显式失败处理，依赖底层fs_sync实现
 *
 * 副作用：
 *   - 触发全量磁盘I/O，可能造成系统短暂卡顿
 *   - 修改磁盘物理存储内容
 *
 * 关键点：
 *   - 响应延迟：需等待所有I/O完成才发送IPC确认
 */
int fsipc_sync(void);
int fsipc_incref(u_int);

// fd.c
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
int close(int fd);
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
int read(int fd, void *buf, u_int nbytes);
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
int write(int fd, const void *buf, u_int nbytes);
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
int seek(int fd, u_int offset);
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
void close_all(void);
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
int readn(int fd, void *buf, u_int nbytes);
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
 */
int dup(int oldfd, int newfd);
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
int fstat(int fdnum, struct Stat *stat);
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
int stat(const char *path, struct Stat *);

// file.c
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
int open(const char *path, int mode);
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
int read_map(int fd, u_int offset, void **blk);
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
int remove(const char *path);
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
int ftruncate(int fd, u_int size);
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
int sync(void);

#define user_assert(x)                                                         \
    do {                                                                       \
        if (!(x))                                                              \
            user_panic("assertion failed: %s", #x);                            \
    } while (0)

// File open modes
#define O_RDONLY 0x0000  /* open for reading only */
#define O_WRONLY 0x0001  /* open for writing only */
#define O_RDWR 0x0002    /* open for reading and writing */
#define O_ACCMODE 0x0003 /* mask for above modes */
#define O_CREAT 0x0100   /* create if nonexistent */
#define O_TRUNC 0x0200   /* truncate to zero length */

// Unimplemented open modes
#define O_EXCL 0x0400  /* error if already exists */
#define O_MKDIR 0x0800 /* create directory, not regular file */

#endif
