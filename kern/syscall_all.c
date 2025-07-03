#include <device.h>
#include <env.h>
#include <env_interrupt.h>
#include <error.h>
#include <fork.h>
#include <kmalloc.h>
#include <mmu.h>
#include <plic.h>
#include <pmap.h>
#include <print.h>
#include <printk.h>
#include <queue.h>
#include <sbi.h>
#include <sched.h>
#include <syscall.h>
#include <trap.h>
#include <types.h>
#include <userspace.h>
#include <virt.h>

extern struct Env *curenv;
extern struct Env envs[NENV];

static struct Trapframe *syscall_current_tf;

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
void sys_putchar(int c) {
    outputk(NULL, (const char *)&c, 1);
    return;
}

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
int sys_print_cons(const void *s, size_t num) {
    if (((size_t)s + num) > UTOP || ((size_t)s) >= UTOP ||
        ((size_t)s > (size_t)s + num)) {
        return -E_INVAL;
    }

    if (num >= KERNEL_BUFFER_SIZE) {
        return -E_INVAL;
    }

    copy_user_space(s, KERNEL_BUFFER, num);

    outputk(NULL, KERNEL_BUFFER, num);

    return 0;
}

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
uint32_t sys_getenvid(void) {
    // 直接从全局变量curenv中获取当前进程的环境ID
    // 注意：这里假设调用时curenv一定非NULL，否则会导致空指针解引用
    return curenv->env_id;
}

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
void __attribute__((noreturn)) sys_yield(void) {
    // Hint: Just use 'schedule' with 'yield' set.
    /* Exercise 4.7: Your code here. */

    // `curenv != NULL`在`do_syscall`中检查
    curenv->env_in_syscall = 0;

    schedule(1);
}

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
int sys_env_destroy(uint32_t envid) {
    struct Env *e;
    try(envid2env(envid, &e, 1));

    printk("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
    env_destroy(e);
    return 0;
}

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
// Checked by DeepSeek-R1 20250422 15:49
int sys_set_tlb_mod_entry(uint32_t envid, uint64_t func) {
    struct Env *env;

    /* Step 1: Convert the envid to its corresponding 'struct Env *' using
     * 'envid2env'. */
    /* Exercise 4.12: Your code here. (1/2) */

    try(envid2env(envid, &env, 1));

    /* Step 2: Set its 'env_user_tlb_mod_entry' to 'func'. */
    /* Exercise 4.12: Your code here. (2/2) */

    env->env_user_tlb_mod_entry = func;

    return 0;
}

/* 概述:
 *   检查'va'是否是用户态虚拟地址（UTEMP <= va < UTOP）
 *
 *   注意：若是非法地址，函数返回1；若是合法地址，函数返回0
 */
static inline int is_illegal_va(u_reg_t va) { return va < UTEMP || va >= UTOP; }

/* 概述:
 *   检查['va', 'va' + len)是否是用户态虚拟地址范围（UTEMP <= x < UTOP）
 *
 *   注意：若是非法地址，函数返回1；若是合法地址，函数返回0
 *
 * 实现细节：
 *
 *  - 若`len = 0`，始终认为是合法范围
 *  - 若`va + len`溢出，认为是非法范围
 *
 */
static inline int is_illegal_va_range(u_reg_t va, u_reg_t len) {
    if (len == 0) {
        return 0;
    }
    return va + len < va || va < UTEMP || va + len > UTOP;
}

/*
 * 概述：
 *   （含 TLB 操作）为指定进程分配物理内存页并建立虚拟地址映射。
 *   若目标虚拟地址已存在映射，则静默解除原映射后建立新映射。
 *   权限检查要求目标进程必须是调用者或其子进程（通过envid2env的checkperm实现）。
 *
 *   不默认设置PTE_R，调用者需要手动设置！
 *
 * 实现差异：
 *   - 仅使用用户传入perm参数的低10位
 *   - 强制移除PTE_V标志（由page_insert内部添加）
 *   - 设置PTE_USER标志
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
// Checked by DeepSeek-R1 20250422 20:26
int sys_mem_alloc(uint32_t envid, u_reg_t va, uint32_t perm) {
    struct Env *env;
    struct Page *pp;

    /* Step 1: Check if 'va' is a legal user virtual address using
     * 'is_illegal_va'. */
    /* Exercise 4.4: Your code here. (1/3) */

    if (is_illegal_va(va) != 0) {
        return -E_INVAL;
    }

    /* Step 2: Convert the envid to its corresponding 'struct Env *' using
     * 'envid2env'. */
    /* Hint: **Always** validate the permission in syscalls! */
    /* Exercise 4.4: Your code here. (2/3) */
    /* 关键点：
     * - 必须设置checkperm=1以保证安全
     * - 依赖全局变量curenv进行权限校验 */
    if (envid2env(envid, &env, 1) != 0) {
        return -E_BAD_ENV;
    }

    /* Step 3: Allocate a physical page using 'page_alloc'. */
    /* Exercise 4.4: Your code here. (3/3) */

    /* 注意：page_alloc不增加pp_ref，由后续page_insert处理 */
    try(page_alloc(&pp));

    // 实现差异：只取用户传入的perm的低12位，并手动移除PTE_V
    // 以满足page_insert的Precondition
    perm &= GENMASK(9, 0);
    perm = perm & (~PTE_V);
    perm = perm | PTE_USER;

    /* Step 4: Map the allocated page at 'va' with permission 'perm' using
     * 'page_insert'. */
    /* 关键点：
     * - page_insert会处理引用计数
     * - 若va已映射，会自动解除原映射
     * - 会触发TLB失效 */
    return page_insert(env->env_pgdir, env->env_asid, pp, va, perm);
}

/*
 * 概述：
 *   （含 TLB 操作）将源进程(srcid)地址空间中'srcva'处的物理页
 *   映射到目标进程(dstid)地址空间中'dstva'处，
 *   并设置权限位'perm'。映射后两个进程共享同一物理页。
 *
 *   不默认设置PTE_R，调用者需要手动设置！
 *
 * 实现差异：
 * - 仅取用户传入的perm低10位，并手动移除PTE_V标志位，
 *   以满足page_insert函数的Precondition要求
 * - 设置PTE_USER标志
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
// Checked by DeepSeek-R1 20250422 20:58
int sys_mem_map(uint32_t srcid, u_reg_t srcva, uint32_t dstid, u_reg_t dstva,
                uint32_t perm) {
    struct Env *srcenv;
    struct Env *dstenv;
    struct Page *pp;

    /* Step 1: Check if 'srcva' and 'dstva' are legal user virtual addresses
     * using 'is_illegal_va'. */
    /* Exercise 4.5: Your code here. (1/4) */

    if ((is_illegal_va(srcva) != 0) || (is_illegal_va(dstva) != 0)) {
        return -E_INVAL;
    }

    /* Step 2: Convert the 'srcid' to its corresponding 'struct Env *' using
     * 'envid2env'. */
    /* Exercise 4.5: Your code here. (2/4) */

    if (envid2env(srcid, &srcenv, 1) != 0) {
        return -E_BAD_ENV;
    }

    /* Step 3: Convert the 'dstid' to its corresponding 'struct Env *' using
     * 'envid2env'. */
    /* Exercise 4.5: Your code here. (3/4) */

    if (envid2env(dstid, &dstenv, 1) != 0) {
        return -E_BAD_ENV;
    }

    /* Step 4: Find the physical page mapped at 'srcva' in the address space of
     * 'srcid'. */
    /* Return -E_INVAL if 'srcva' is not mapped. */
    /* Exercise 4.5: Your code here. (4/4) */

    pp = page_lookup(srcenv->env_pgdir, srcva, NULL);

    if (pp == NULL) {
        return -E_INVAL;
    }

    /* Step 5: Map the physical page at 'dstva' in the address space of
     * 'dstid'.
     */
    // 实现差异：只取用户传入的perm的低12位，并手动移除PTE_C_CACHEABLE、PTE_V
    // 以满足page_insert的Precondition
    perm &= GENMASK(9, 0);
    perm = perm & (~PTE_V);
    perm = perm | PTE_USER;

    return page_insert(dstenv->env_pgdir, dstenv->env_asid, pp, dstva, perm);
}

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
// Checked by DeepSeek-R1 20250422 21:03
int sys_mem_unmap(uint32_t envid, u_reg_t va) {
    struct Env *e;

    /* Step 1: Check if 'va' is a legal user virtual address using
     * 'is_illegal_va'. */
    /* Exercise 4.6: Your code here. (1/2) */

    if (is_illegal_va(va) != 0) {
        return -E_INVAL;
    }

    /* Step 2: Convert the envid to its corresponding 'struct Env *' using
     * 'envid2env'. */
    /* Exercise 4.6: Your code here. (2/2) */

    if (envid2env(envid, &e, 1) != 0) {
        return -E_BAD_ENV;
    }

    /* Step 3: Unmap the physical page at 'va' in the address space of 'envid'.
     */
    page_remove(e->env_pgdir, e->env_asid, va);
    return 0;
}

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
// Checked by DeepSeek-R1 and reference solution 20250422 14:08
int sys_exofork(void) {
    struct Env *e;

    assert(curenv != NULL);

    u_int parent_id = curenv->env_id;

    /* Step 1: Allocate a new env using 'env_alloc'. */
    /* Exercise 4.9: Your code here. (1/4) */

    try(env_alloc(&e, parent_id));

    /* Step 2: Copy the current Trapframe below 'KSTACKTOP' to the new env's
     * 'env_tf'. */
    /* Exercise 4.9: Your code here. (2/4) */

    struct Trapframe parent_tf = *(((struct Trapframe *)(KSTACKTOP)) - 1);
    e->env_tf = parent_tf;

    /* Step 3: Set the new env's 'env_tf.regs[10]' to 0 to indicate the return
     * value in child. */
    /* Exercise 4.9: Your code here. (3/4) */
    // 2 -> v0
    e->env_tf.regs[10] = 0;
    e->env_in_syscall = 0;

    dup_userspace(curenv->env_pgdir, e->env_pgdir, e->env_asid);

    /* Step 4: Set up the new env's 'env_status' and 'env_pri'.  */
    /* Exercise 4.9: Your code here. (4/4) */
    e->env_status = ENV_RUNNABLE;

    e->env_pri = curenv->env_pri;

    TAILQ_INSERT_HEAD(&env_sched_list, e, env_sched_link);

    return (int)e->env_id;
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
// Checked by DeepSeek-R1 20250424 17:23
int sys_set_env_status(uint32_t envid, uint32_t status) {
    struct Env *env;

    /* Step 1: Check if 'status' is valid. */
    /* Exercise 4.14: Your code here. (1/3) */

    if ((status != ENV_RUNNABLE) && (status != ENV_NOT_RUNNABLE)) {
        return -E_INVAL;
    }

    /* Step 2: Convert the envid to its corresponding 'struct Env *' using
     * 'envid2env'. */
    /* Exercise 4.14: Your code here. (2/3) */

    try(envid2env(envid, &env, 1));

    /* Step 3: Update 'env_sched_list' if the 'env_status' of 'env' is being
     * changed. */
    /* Exercise 4.14: Your code here. (3/3) */

    uint32_t pre_status = env->env_status;

    /* 根据状态变化情况更新调度队列：
     * 1. 原状态不可运行 -> 新状态可运行：加入队列尾部
     * 2. 原状态可运行 -> 新状态不可运行：从队列移除
     * 3. 其他情况（状态未变或同为不可运行）：无需操作
     */
    if ((pre_status == ENV_NOT_RUNNABLE) && (status == ENV_NOT_RUNNABLE)) {
        // No need to change
    } else if ((pre_status == ENV_NOT_RUNNABLE) && (status == ENV_RUNNABLE)) {
        TAILQ_INSERT_TAIL(&env_sched_list, env, env_sched_link);
    } else if ((pre_status == ENV_RUNNABLE) && (status == ENV_NOT_RUNNABLE)) {
        TAILQ_REMOVE(&env_sched_list, env, env_sched_link);
    } else {
        // No need to change
    }

    /* Step 4: Set the 'env_status' of 'env'. */
    env->env_status = status;

    /* Step 5: Use 'schedule' with 'yield' set if ths 'env' is 'curenv'. */
    if (env == curenv) {
        schedule(1);
    }
    return 0;
}

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
int sys_set_trapframe(uint32_t envid, struct Trapframe *tf) {

    struct Trapframe ktf;

    if (is_illegal_va_range((u_reg_t)tf, sizeof(struct Trapframe))) {
        return -E_INVAL;
    }

    copy_user_space(tf, &ktf, sizeof(struct Trapframe));

    struct Env *env;
    try(envid2env(envid, &env, 1));

    if (env == curenv) {
        *((struct Trapframe *)KSTACKTOP - 1) = ktf;
        // return `tf->regs[10]` instead of 0, because return value overrides
        // regs[10] on current trapframe.
        // 此函数返回后，返回值将被写入陷阱帧中的regs[10]
        // 此处必须返回`tf->regs[10]`，否则，此处返回的值将覆盖陷阱帧中的regs[10]
        // 导致目标进程的实际状态与`tf`中指定的不同
        return ktf.regs[10];
    } else {
        env->env_tf = ktf;
        return 0;
    }
}

/* Overview:
 * 	Kernel panic with message `msg`.
 *
 * Post-Condition:
 * 	This function will halt the system.
 */
void sys_panic(char *msg) {

    if ((size_t)msg >= ULIM) {
        panic("invalid message");
    }

    allow_access_user_space();

    size_t msg_len = strlen(msg);

    disallow_access_user_space();

    if ((size_t)(msg + msg_len) >= ULIM) {
        panic("invalid message");
    }

    if (msg_len >= KERNEL_BUFFER_SIZE) {
        panic("invalid message");
    }

    copy_user_space((void *)msg, KERNEL_BUFFER, msg_len);

    ((char *)KERNEL_BUFFER)[msg_len] = '\0';

    panic("%s", KERNEL_BUFFER);
}

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
// Checked by DeepSeek-R1 20250424 13:28
int sys_ipc_recv(uint32_t dstva, uint32_t from) {
    /* Step 1: Check if 'dstva' is either zero or a legal address. */
    if (dstva != 0 && is_illegal_va(dstva)) {
        return -E_INVAL;
    }

    /* Step 2: Set 'curenv->env_ipc_recving' to 1. */
    /* Exercise 4.8: Your code here. (1/8) */

    curenv->env_ipc_recving = 1;

    /* Step 3: Set the value of 'curenv->env_ipc_dstva'. */
    /* Exercise 4.8: Your code here. (2/8) */

    curenv->env_ipc_dstva = dstva;

    curenv->env_ipc_recv_from = from;

    /* Step 4: Set the status of 'curenv' to 'ENV_NOT_RUNNABLE' and remove it
     * from 'env_sched_list'. */
    /* Exercise 4.8: Your code here. (3/8) */

    curenv->env_status = ENV_NOT_RUNNABLE;
    TAILQ_REMOVE(&env_sched_list, curenv, env_sched_link);

    /* Step 5: Give up the CPU and block until a message is received. */
    // 10 -> a0
    // 这相当于设置了本次系统调用的返回值为0

    ((struct Trapframe *)KSTACKTOP - 1)->regs[10] = 0;
    schedule(1);
}

/*
 * 概述：
 *   尝试向目标环境'envid'发送一个'value'值（如果'srcva'不为0，则同时发送一个页面）。
 *   实现差异：
 *   - `perm`只取低10位，并清除了`PTE_V`
 *   - 将自动设置PTE_USER
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
// Checked by DeepSeek-R1 20250424 13:38
int sys_ipc_try_send(uint32_t envid, uint64_t value, u_reg_t srcva,
                     uint32_t perm) {
    struct Env *e;
    struct Page *p;

    /* Step 1: Check if 'srcva' is either zero or a legal address. */
    /* Exercise 4.8: Your code here. (4/8) */

    if ((srcva != 0) && (is_illegal_va(srcva) != 0)) {
        return -E_INVAL;
    }

    /* Step 2: Convert 'envid' to 'struct Env *e'. */
    /* 这是唯一一个调用'envid2env'时应设置'checkperm'为0的系统调用，
     * 因为目标环境不限于'curenv'的子环境 */
    /* Exercise 4.8: Your code here. (5/8) */

    if (envid2env(envid, &e, 0) != 0) {
        return -E_BAD_ENV;
    }

    /* Step 3: Check if the target is waiting for a message. */
    /* Exercise 4.8: Your code here. (6/8) */

    if (e->env_ipc_recving == 0) {
        return -E_IPC_NOT_RECV;
    }

    if (e->env_ipc_recv_from != 0) {
        if (e->env_ipc_recv_from != curenv->env_id) {
            return -E_IPC_NOT_RECV;
        }
    }

    // 此处清除了`perm`的高20位，以满足`page_insert`的Precondition

    perm &= GENMASK(9, 0);

    perm |= PTE_USER;

    /* Step 4: Set the target's ipc fields. */
    e->env_ipc_value = value;
    e->env_ipc_from = curenv->env_id;

    e->env_ipc_perm = perm;
    e->env_ipc_recving = 0;

    /* Step 5: Set the target's status to 'ENV_RUNNABLE' again and insert it to
     * the tail of 'env_sched_list'. */
    /* Exercise 4.8: Your code here. (7/8) */

    e->env_status = ENV_RUNNABLE;
    e->env_in_syscall = 0;

    TAILQ_INSERT_TAIL(&env_sched_list, e, env_sched_link);

    /* Step 6: If 'srcva' is not zero, map the page at 'srcva' in 'curenv' to
     * 'e->env_ipc_dstva' in 'e'. */
    /* Return -E_INVAL if 'srcva' is not zero and not mapped in 'curenv'. */
    if (srcva != 0) {
        /* Exercise 4.8: Your code here. (8/8) */

        p = page_lookup(curenv->env_pgdir, srcva, NULL);

        if (p == NULL) {
            return -E_INVAL;
        }

        try(page_insert(e->env_pgdir, e->env_asid, p, e->env_ipc_dstva, perm));
    }
    return 0;
}

// XXX: kernel does busy waiting here, blocking all envs
int sys_cgetc(void) {
    int ch;

    while (1) {
        struct sbiret ret = sbi_debug_console_read(1, (u_reg_t)&ch, 0);

        if (ret.error == SBI_SUCCESS) {
            break;
        }
    }
    return ch;
}

/*
 * 概述：
 *   验证物理地址pa是否在允许的设备地址范围内
 *
 *   具体地，对于写入长度`len`，需要`[pa, pa + len)`都在允许范围内
 *
 * Precondition：
 *   - 依赖静态数组valid_base_address和valid_len定义设备地址范围
 *
 * Postcondition：
 *   - 返回0表示地址合法
 *   - 返回1表示地址越界或地址溢出（pa + len < pa）
 *
 * 关键点：
 *   - 静态数组存储console和IDE控制器的基地址及长度
 *   - 地址溢出检查防止整数回绕（如0xFFFFFFFF + 4）
 */
// Checked by DeepSeek-R1 20250508 16:35
static int is_illegal_device_pa_range(u_reg_t pa, u_reg_t len,
                                      struct Device **out_device) {

    if (pa + len < pa) {
        return 1;
    }

    for (size_t i = 0; i < devices.len; i++) {
        struct Device *current_device = &devices.array[i];

        struct DeviceMMIORange *current_mmio_range =
            current_device->mmio_range_list;

        while (current_mmio_range != NULL) {
            if ((pa >= current_mmio_range->pa) &&
                (pa < current_mmio_range->pa + current_mmio_range->len)) {

                *out_device = current_device;

                return 0;
            }

            current_mmio_range = current_mmio_range->next;
        }
    }

    return 1;
}

/*
 * 概述：
 *   检查虚拟地址va是否按len字节对齐
 *
 * Precondition：
 *   - len必须为1、2、4、8
 *
 * Postcondition：
 *   - 返回0表示地址对齐合法
 *   - 返回1表示地址未按要求对齐
 */
// Checked by DeepSeek-R1 20250508 16:35
static int is_illegal_align(u_reg_t va, u_reg_t len) {
    if (len == 1) {
        return 0;
    } else if (len == 2) {
        if (va % 2 == 0) {
            return 0;
        } else {
            return 1;
        }
    } else if (len == 4) {
        if (va % 4 == 0) {
            return 0;
        } else {
            return 1;
        }
    } else if (len == 8) {
        if (va % 8 == 0) {
            return 0;
        } else {
            return 1;
        }
    } else {
        panic("unreachable code: len shoudle be 1 or 2 or 4");
        return 1;
    }
}

/*
 * 概述：
 *   系统调用函数，用于将用户空间从`va`开始长`len`的数据写入设备物理地址`pa`。
 *   根据数据长度选择对应的iowrite函数（8/16/32位/64位）。
 *
 * Precondition：
 *   - len必须为1、2、4、8字节，否则返回-E_INVAL
 *   - 用户虚拟地址[va, va+len)必须合法（UTEMP <= x < UTOP）
 *   - 虚拟地址va必须按len字节对齐（len=2时2字节对齐，len=4时4字节对齐）
 *   - 物理地址pa必须属于有效设备范围（MMIO）
 *
 * Postcondition：
 *   - 成功时：用户数据[va, va+len)被写入物理地址pa，返回0
 *   - 失败时：返回-E_INVAL（参数非法或地址越界）
 *
 * 副作用：
 *   - 通过iowrite函数直接修改设备寄存器状态
 *   - 可能触发硬件设备的I/O操作（如串口输出、磁盘写入）
 */
// Checked by DeepSeek-R1 20250508 16:35
int sys_write_dev(u_reg_t va, u_reg_t pa, u_reg_t len) {
    /* Exercise 5.1: Your code here. (1/2) */

    struct Device *target_device = NULL;

    if ((len != 1) && (len != 2) && (len != 4) && (len != 8)) {
        return -E_INVAL;
    }

    if (is_illegal_va_range(va, len) != 0) {
        return -E_INVAL;
    }

    if (is_illegal_device_pa_range(pa, len, &target_device) != 0) {
        return -E_INVAL;
    }

    if (len == 1) {
        uint8_t data = 0;
        copy_user_space((void *)va, &data, 1);

        iowrite8(target_device, data, pa);
    } else if (len == 2) {
        uint16_t data = 0;
        copy_user_space((void *)va, &data, 2);

        iowrite16(target_device, data, pa);
    } else if (len == 4) {
        uint32_t data = 0;
        copy_user_space((void *)va, &data, 4);

        iowrite32(target_device, data, pa);
    } else if (len == 4) {
        uint64_t data = 0;
        copy_user_space((void *)va, &data, 8);

        iowrite64(target_device, data, pa);
    } else {
        panic("unreachable code: len shoudle be 1 or 2 or 4 or 8");
    }

    return 0;
}

/*
 * 概述：
 *   系统调用函数，用于从设备物理地址读取数据到用户空间。根据数据长度选择对应的ioread函数（8/16/32位）。
 *
 * Precondition：
 *   - len必须为1、2、4、8字节，否则返回-E_INVAL
 *   - 用户虚拟地址[va, va+len)必须合法（UTEMP <= x < UTOP）
 *   - 虚拟地址va必须按len字节对齐（len=2时2字节对齐，len=4时4字节对齐）
 *   - 物理地址pa必须属于有效设备范围（MMIO）
 *
 * Postcondition：
 *   - 成功时：设备数据从pa读取到用户空间[va, va+len)，返回0
 *   - 失败时：返回-E_INVAL（参数非法或地址越界）
 *
 * 副作用：
 *   - 可能改变设备状态（如读取状态寄存器可能清除中断标志）
 *   - 通过直接内存访问操作设备寄存器
 */
// Checked by DeepSeek-R1 20250508 16:35
int sys_read_dev(u_reg_t va, u_reg_t pa, u_reg_t len) {
    /* Exercise 5.1: Your code here. (2/2) */

    struct Device *target_device = NULL;

    if ((len != 1) && (len != 2) && (len != 4) && (len != 8)) {
        return -E_INVAL;
    }

    if (is_illegal_va_range(va, len) != 0) {
        return -E_INVAL;
    }

    if (is_illegal_device_pa_range(pa, len, &target_device) != 0) {
        return -E_INVAL;
    }

    if (len == 1) {
        uint8_t data = ioread8(target_device, pa);

        copy_user_space(&data, (void *)va, 1);
    } else if (len == 2) {
        uint16_t data = ioread16(target_device, pa);

        copy_user_space(&data, (void *)va, 2);
    } else if (len == 4) {
        uint32_t data = ioread32(target_device, pa);

        copy_user_space(&data, (void *)va, 4);
    } else if (len == 8) {
        uint64_t data = ioread64(target_device, pa);

        copy_user_space(&data, (void *)va, 8);
    } else {
        panic("unreachable code: len shoudle be 1 or 2 or 4 or 8");
    }

    return 0;
}

void sys_map_user_vpt(void) {
    if (curenv == NULL) {
        panic("sys_map_user_vpt called while curenv is NULL");
    }

    map_user_vpt(curenv);
}

void sys_unmap_user_vpt(void) {
    if (curenv == NULL) {
        panic("sys_unmap_user_vpt called while curenv is NULL");
    }

    unmap_user_vpt(curenv);
}

void sys_sleep(void) {
    if (curenv == NULL) {
        panic("sys_sleep called while curenv is NULL");
    }

    curenv->env_status = ENV_NOT_RUNNABLE;
    TAILQ_REMOVE(&env_sched_list, curenv, env_sched_link);

    // 设置本次系统调用的返回值为0
    // 10 -> a0
    curenv->env_tf.regs[10] = 0;

    schedule(1);
}

int sys_set_interrupt_handler(uint32_t interrupt_code, u_reg_t handler_va) {
    if (curenv == NULL) {
        panic("sys_set_interrupt_handler called while curenv is NULL");
    }

    uint32_t interrupt_count = plic_get_interrupt_count();

    if (interrupt_code >= interrupt_count) {
        return -E_INVAL;
    }

    if ((handler_va < UTEMP) || (handler_va >= USTACKTOP)) {
        return -E_INVAL;
    }

    register_env_interrupt(interrupt_code, curenv, handler_va);

    plic_enable_interrupt(interrupt_code, 1, handle_env_interrupt);

    return 0;
}

u_reg_t sys_interrupt_return(void) {
    if (curenv == NULL) {
        panic("sys_interrupt_return called while curenv is NULL");
    }

    ret_env_interrupt(syscall_current_tf);
    // 10 -> a0
    // 在`do_syscall`中，a0将被本函数的返回值覆盖
    // 为了使得陷阱帧中的a0的值被保留，此处需要返回该值
    return syscall_current_tf->regs[10];
}

int sys_get_device_count(char *device_type) {
    if (curenv == NULL) {
        panic("sys_get_device_count called while curenv is NULL");
    }

    char device_type_buffer[DEVICE_TYPE_LEN] = {0};

    copy_user_space(device_type, device_type_buffer, DEVICE_TYPE_LEN);

    device_type_buffer[DEVICE_TYPE_LEN - 1] = '\0';

    return (int)get_device_count(device_type_buffer);
}

int sys_get_device(char *device_type, size_t idx, size_t max_data_len,
                   u_reg_t out_device, u_reg_t out_device_data) {
    if (curenv == NULL) {
        panic("sys_get_device called while curenv is NULL");
    }

    if ((is_illegal_va_range(out_device, sizeof(struct UserDevice)) == 1) ||
        (is_illegal_va_range(out_device_data, max_data_len) == 1)) {
        return -E_INVAL;
    }

    char device_type_buffer[DEVICE_TYPE_LEN] = {0};

    copy_user_space(device_type, device_type_buffer, DEVICE_TYPE_LEN);

    device_type_buffer[DEVICE_TYPE_LEN - 1] = '\0';

    return user_find_device_by_type(device_type_buffer, idx, max_data_len,
                                    (struct UserDevice *)out_device,
                                    (void *)out_device_data);
}

int sys_get_process_list(int max_len, u_reg_t out_process_list) {
    if (curenv == NULL) {
        panic("sys_get_process_list called while curenv is NULL");
    }

    if (max_len < 0) {
        return -E_INVAL;
    }

    if ((is_illegal_va_range(out_process_list,
                             max_len * sizeof(struct Process))) == 1) {
        return -E_INVAL;
    }

    size_t max_process_count =
        ((size_t)max_len) < NENV ? (((size_t)max_len)) : NENV;
    struct Process *buffer =
        (struct Process *)kmalloc(max_process_count * sizeof(struct Process));

    if (buffer == NULL) {
        panic("sys_get_process_list: cannot allocate buffer");
    }

    int count = 0;

    for (size_t i = 0; i < NENV; i++) {
        struct Env *cur = &envs[i];

        if (cur->env_status != ENV_FREE) {
            buffer[count].env_id = cur->env_id;
            buffer[count].env_parent_id = cur->env_parent_id;
            buffer[count].env_pri = cur->env_pri;
            buffer[count].env_status = cur->env_status;
            buffer[count].env_runs = cur->env_runs;
            strcpy(buffer[count].env_name, cur->env_name);
            count++;
        }
    }

    copy_user_space(buffer, (void *)out_process_list,
                    sizeof(struct Process) * (size_t)count);

    kfree(buffer);

    return count;
}

u_reg_t sys_get_physical_address(u_reg_t va) {
    if (curenv == NULL) {
        panic("sys_get_physical_address called while curenv is NULL");
    }

    u_reg_t pa = va2pa(curenv->env_pgdir, va);

    if (pa == ~0ULL) {
        return 0;
    } else {
        return pa;
    }
}

int sys_is_dirty(u_reg_t va) {
    if (curenv == NULL) {
        panic("sys_get_physical_address called while curenv is NULL");
    }

    Pte *pte = NULL;

    if (page_lookup(curenv->env_pgdir, va, &pte) != NULL) {
        return ((*pte) & PTE_DIRTY) != 0;
    }

    return 0;
}

int sys_pageref(u_reg_t va) {

    if (curenv == NULL) {
        panic("sys_get_physical_address called while curenv is NULL");
    }

    struct Page *page = page_lookup(curenv->env_pgdir, va, NULL);

    if (page != NULL) {

        return (int)(page->pp_ref);
    }

    return 0;
}

void *syscall_table[MAX_SYSNO] = {
    [SYS_putchar] = sys_putchar,
    [SYS_print_cons] = sys_print_cons,
    [SYS_getenvid] = sys_getenvid,
    [SYS_yield] = sys_yield,
    [SYS_env_destroy] = sys_env_destroy,
    [SYS_mem_alloc] = sys_mem_alloc,
    [SYS_mem_map] = sys_mem_map,
    [SYS_mem_unmap] = sys_mem_unmap,
    [SYS_exofork] = sys_exofork,
    [SYS_set_env_status] = sys_set_env_status,
    [SYS_set_trapframe] = sys_set_trapframe,
    [SYS_panic] = sys_panic,
    [SYS_ipc_try_send] = sys_ipc_try_send,
    [SYS_ipc_recv] = sys_ipc_recv,
    [SYS_cgetc] = sys_cgetc,
    [SYS_write_dev] = sys_write_dev,
    [SYS_read_dev] = sys_read_dev,
    [SYS_map_user_vpt] = sys_map_user_vpt,
    [SYS_unmap_user_vpt] = sys_unmap_user_vpt,
    [SYS_sleep] = sys_sleep,
    [SYS_set_interrupt_handler] = sys_set_interrupt_handler,
    [SYS_interrupt_return] = sys_interrupt_return,
    [SYS_get_device_count] = sys_get_device_count,
    [SYS_get_device] = sys_get_device,
    [SYS_get_process_list] = sys_get_process_list,
    [SYS_get_physical_address] = sys_get_physical_address,
    [SYS_is_dirty] = sys_is_dirty,
    [SYS_pageref] = sys_pageref};

/*
 * 概述：
 *   根据系统调用号'sysno'从'syscall_table'中获取对应的系统调用函数，
 *   并从用户上下文和栈中提取参数，调用该系统调用函数，最后将返回值存入用户上下文。
 *
 * Precondition：
 * - 'tf'必须指向有效的Trapframe结构体，包含完整的用户态寄存器状态
 * - 'syscall_table'必须已正确初始化，包含所有有效的系统调用处理函数
 * - 用户栈指针（$sp）必须指向有效的用户栈空间
 * - 系统调用号'sysno'必须小于MAX_SYSNO
 *
 * Postcondition：
 * - 系统调用函数被正确调用，返回值存入Trapframe的$a0寄存器
 * - 若系统调用号无效，$a0寄存器被设置为-E_NO_SYS
 * - 用户态的EPC寄存器值增加4，指向下一条指令
 *
 * 副作用：
 * - 修改了Trapframe结构体中的以下字段：
 *   - sepc（增加4）
 *   - regs[10]（存储返回值或错误码）
 * - 可能通过系统调用函数修改其他全局状态（具体取决于调用的系统调用）
 */
// Checked by DeepSeek-R1 20250422 20:01
void do_syscall(struct Trapframe *tf) {
    int (*func)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

    syscall_current_tf = tf;

    // 10 -> a0
    int sysno = tf->regs[10];
    if (sysno < 0 || sysno >= MAX_SYSNO) {
        // 10 -> a0
        tf->regs[10] = (u_reg_t)(-E_NO_SYS);
        return;
    }

    if (curenv == NULL) {
        panic("do_syscall called while curenv is NULL");
    }

    curenv->env_in_syscall = 1;

    /* Step 1: Add the EPC in 'tf' by a word (size of an instruction). */
    /* Exercise 4.2: Your code here. (1/4) */
    // 注意，执行syscall时，EPC中保存的是syscall指令的地址，为了正常返回，需要手动+4
    tf->sepc += 4;

    /* Step 2: Use 'sysno' to get 'func' from 'syscall_table'. */
    /* Exercise 4.2: Your code here. (2/4) */

    func = syscall_table[sysno];

    uint64_t arg1 = tf->regs[11];
    uint64_t arg2 = tf->regs[12];
    uint64_t arg3 = tf->regs[13];
    uint64_t arg4 = tf->regs[14];
    uint64_t arg5 = tf->regs[15];

    /* Step 5: Invoke 'func' with retrieved arguments and store its return value
     * to $v0 in 'tf'.
     */
    /* Exercise 4.2: Your code here. (4/4) */

    // env_in_syscall设置：
    // 对于阻塞调用`sys_ipc_recv`，该函数不会返回 ->
    // 保持`env_in_syscall = 1`，符合预期
    // 当`sys_ipc_try_send`唤醒目标进程时，设置`env_in_syscall = 0`
    // 对于`sys_yield`该函数不会返回 -> 函数中手动置0
    // 对于`sys_exofork`父进程从此处返回，子进程不从此处返回
    // 函数中手动置0
    u_reg_t ret = func(arg1, arg2, arg3, arg4, arg5);

    curenv->env_in_syscall = 0;

    // 10 -> a0
    tf->regs[10] = (u_reg_t)ret;
}
