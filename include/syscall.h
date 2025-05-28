#ifndef SYSCALL_H
#define SYSCALL_H

#ifndef __ASSEMBLER__

enum {
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
    SYS_putchar,
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
    SYS_print_cons,
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
    SYS_getenvid,
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
    SYS_yield,
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
    SYS_env_destroy,
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
    SYS_set_tlb_mod_entry,
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
    SYS_mem_alloc,
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
    SYS_mem_map,
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
    SYS_mem_unmap,
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
     * - 内核栈顶必须包含有效的Trapframe结构（位于KSTACKTOP -
     * sizeof(Trapframe)）
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
    SYS_exofork,
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
     *   - 若状态发生变化（ENV_NOT_RUNNABLE <->
     * ENV_RUNNABLE），调度队列被正确更新
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
    SYS_set_env_status,
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
    SYS_set_trapframe,
    /* Overview:
     * 	Kernel panic with message `msg`.
     *
     * Post-Condition:
     * 	This function will halt the system.
     */
    SYS_panic,

    /*
     * 概述：
     *   尝试向目标环境'envid'发送一个'value'值（如果'srcva'不为0，则同时发送一个页面）。
     *   实现差异：
     *   - `perm`只取低12位，并清除了`PTE_V`和`PTE_C_CACHEABLE`
     *   -
     * 与参考实现不同，这里为`e->env_ipc_perm`补充了缺失的`PTE_C_CACHEABLE`标志
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
     * - 若srcva != 0但合法，dstva = 0，将导致接收者的[0x00000000,
     * 0x00000FFF]被映射 这被认为是系统设计缺陷，但保留该效果
     */
    SYS_ipc_try_send,
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
    SYS_ipc_recv,
    SYS_cgetc,
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
    SYS_write_dev,
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
    SYS_read_dev,
    SYS_map_user_vpt,
    SYS_unmap_user_vpt,
    MAX_SYSNO,
};

#endif

#endif
