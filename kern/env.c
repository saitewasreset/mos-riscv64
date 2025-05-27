#include "asm/regdef.h"
#include <elf.h>
#include <env.h>
#include <error.h>
#include <mmu.h>
#include <pmap.h>
#include <printk.h>
#include <queue.h>
#include <sched.h>
#include <types.h>

// 所有Env组成的列表，静态分配(bss段)
struct Env envs[NENV] __attribute__((aligned(PAGE_SIZE))); // All environments

struct Env *curenv = NULL; // 当前正在运行的环境，由`env_run`修改
static struct Env_list
    env_free_list; // 空闲Env链表，只应含有`ENV_FREE`状态的Env，由`env_init`初始化

// Invariant: 'env' in 'env_sched_list' iff. 'env->env_status' is 'RUNNABLE'.
// 调度队列，只应含有`ENV_RUNNABLE`状态的Env，由`env_init`初始化
// 调度队列头部的进程将在下次进程上下文切换时（schedule函数）运行
struct Env_sched_list env_sched_list;

static Pte *
    base_pgdir; // 用户程序页目录模板，含有`pages`、`envs`的只读映射，由`env_init`初始化

static uint32_t asid_bitmap[NASID / 32] = {
    0}; // 每一位表示对应的ASID的占用情况，1表示占用，静态初始化为0

/* Overview:
 *  Allocate an unused ASID.
 *
 * Post-Condition:
 *   return 0 and set '*asid' to the allocated ASID on success.
 *   return -E_NO_FREE_ENV if no ASID is available.
 */
static int asid_alloc(uint16_t *asid) {
    for (uint16_t i = 0; i < NASID; ++i) {
        uint16_t index = (uint16_t)(i >> 5);
        uint16_t inner = (uint16_t)(i & 31);
        if ((asid_bitmap[index] & (1 << inner)) == 0) {
            asid_bitmap[index] |= 1 << inner;
            *asid = i;
            return 0;
        }
    }
    return -E_NO_FREE_ENV;
}

/* Overview:
 *  Free an ASID.
 *
 * Pre-Condition:
 *  The ASID is allocated by 'asid_alloc'.
 *
 * Post-Condition:
 *  The ASID is freed and may be allocated again later.
 */
static void asid_free(uint16_t asid) {
    uint16_t index = (uint16_t)(asid >> 5);
    uint16_t inner = (uint16_t)(asid & 31);
    asid_bitmap[index] &= (uint32_t)(~(1 << inner));
}

/*
 * 概述：
 *
 *   （含 TLB 操作）在`pgdir`对应的页表中，
 * 将虚拟地址空间[va, va+size)映射到物理地址空间[pa,pa+size)，
 * 使用权限位'perm | PTE_V'建立页表项。
 *
 * Precondition：
 * - 'pa'、'va'、'size'必须按页对齐（PAGE_SIZE）
 * - 'pgdir'必须指向有效的页目录结构
 * - 'perm'参数不得包含PTE_V（由函数自动添加）
 * - 'asid'必须是有效的地址空间标识符
 *
 * Postcondition：
 * - [va, va+size)范围内的每个虚拟页：
 *   - 映射到对应的物理页[pa, pa+size)
 *   - 页表项权限为'perm | PTE_V'
 *   - 若原存在映射，旧页引用计数-1，新页引用计数+1
 * - TLB中相关条目被无效化
 *
 * 副作用：
 * - 可能修改页目录和页表结构
 * - 可能分配新的物理页（通过page_insert内部调用）
 * - 修改物理页的引用计数（pp_ref字段）
 * - 调用tlb_invalidate使相关TLB条目失效
 */
static void map_segment(Pte *pgdir, uint16_t asid, u_reg_t pa, u_reg_t va,
                        u_reg_t size, uint32_t perm) {

    assert(pa % PAGE_SIZE == 0);
    assert(va % PAGE_SIZE == 0);
    assert(size % PAGE_SIZE == 0);

    /* Step 1: Map virtual address space to physical address space. */
    for (u_int i = 0; i < size; i += PAGE_SIZE) {
        /*
         * Hint:
         *  Map the virtual page 'va + i' to the physical page 'pa + i' using
         * 'page_insert'. Use 'pa2page' to get the 'struct Page *' of the
         * physical address.
         */
        /* Exercise 3.2: Your code here. */

        u_long current_pa = pa + i;
        u_long current_va = va + i;
        struct Page *current_pp = pa2page(current_pa);

        page_insert(pgdir, asid, current_pp, current_va, perm);
    }
}

/*
 * 概述：
 *
 *   为每个进程生成唯一的标识符(envid)，该标识符包含环境在全局数组中的索引和递增的生成计数
 *   对于不同进程，即使分配到同一个Env，其envid也不同
 *
 * Precondition：
 * - 参数'e'必须指向全局数组'envs'中的有效Env结构体
 * - LOG2NENV宏应正确表示envs数组大小的对数(确保2^LOG2NENV == NENV)
 *
 * Postcondition：
 * - 返回的envid格式为：[31 : 11]位 = 递增计数器i
 *                   [10 : 0]位    = 环境在envs数组中的索引
 * - 保证每次调用生成不同的envid(直到计数器i溢出)
 *
 * 副作用：
 * - 修改静态变量i的值(递增)
 */
u_int mkenvid(struct Env *e) {
    static u_int i = 0;
    return ((++i) << (1 + LOG2NENV)) | (e - envs);
}

/*
 * 概述：
 *   将给定的环境ID（envid）转换为对应的Env结构体指针。
 *   如果envid为0，则将*penv设置为当前环境（curenv）；否则设置为envs[ENVX(envid)]。
 *   当checkperm非零时，要求目标环境必须是当前环境或其**直接**子环境。
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
// Checked by DeepSeek-R1 20250422 20:26
int envid2env(u_int envid, struct Env **penv, int checkperm) {
    struct Env *e;

    /* Step 1: Assign value to 'e' using 'envid'. */
    /* Hint:
     *   If envid is zero, set 'penv' to 'curenv' and return 0.
     *   You may want to use 'ENVX'.
     */
    /* Exercise 4.3: Your code here. (1/2) */

    if (envid == 0) {
        *penv = curenv;
        return 0;
    } else {
        e = &envs[ENVX(envid)];
    }

    // 关键检查：需同时验证环境状态和env_id匹配
    // 因为ENVX可能产生冲突（不同env_id可能映射到同一数组位置）
    // 注意，每个**进程**获得的`env_id`是不同的，即使它们复用了同一个环境
    // 由于ENVX是env_id的低10位，即使传入非法的env_id（例如，从未分配过）
    // 通过ENVX取低10位也可能得到另一个环境，故需要通过`e->env_id != envid`
    // 检查
    if (e->env_status == ENV_FREE || e->env_id != envid) {
        return -E_BAD_ENV;
    }

    /* Step 2: Check when 'checkperm' is non-zero. */
    /* Hints:
     *   Check whether the calling env has sufficient permissions to manipulate
     * the specified env, i.e. 'e' is either 'curenv' or its **immediate
     * child**. If violated, return '-E_BAD_ENV'.
     */
    /* Exercise 4.3: Your code here. (2/2) */
    if (checkperm != 0) {
        if (curenv == NULL) {
            panic("envid2env called with checkperm when curenv == NULL");
        }

        if ((e != curenv) && (e->env_parent_id != curenv->env_id)) {
            return -E_BAD_ENV;
        }
    }

    /* Step 3: Assign 'e' to '*penv'. */
    *penv = e;
    return 0;
}

/*
 * 概述：
 *
 *   初始化所有Env结构体为空闲，将其加入空闲Env链表，并初始化调度队列。设置页目录模板，
 *   映射用户空间只读区域（UPAGES/UENVS）以及内核区域。
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
void env_init(void) {
    /* Step 1: Initialize 'env_free_list' with 'LIST_INIT' and 'env_sched_list'
     * with 'TAILQ_INIT'. */
    /* Exercise 3.1: Your code here. (1/2) */

    LIST_INIT(&env_free_list);
    TAILQ_INIT(&env_sched_list);

    /* Step 2: Traverse the elements of 'envs' array, set their status to
     * 'ENV_FREE' and insert them into the 'env_free_list'. Make sure, after the
     * insertion, the order of envs in the list should be the same as they are
     * in the 'envs' array. */

    /* Exercise 3.1: Your code here. (2/2) */

    for (size_t i = 0; i < NENV; i++) {
        size_t idx = NENV - i - 1;

        envs[idx].env_status = ENV_FREE;

        LIST_INSERT_HEAD(&env_free_list, &envs[idx], env_link);
    }

    /*
     * We want to map 'UPAGES' and 'UENVS' to *every* user space with PTE_G
     * permission (with PTE_RO), then user programs can read (but cannot
     * write) kernel data structures 'pages' and 'envs'.
     *
     * Here we first map them into the *template* page directory 'base_pgdir'.
     * Later in 'env_setup_vm', we will copy them into each 'env_pgdir'.
     */
    struct Page *p;
    panic_on(page_alloc(&p));
    p->pp_ref++;

    base_pgdir = (Pte *)page2kva(p);

    // 映射直接内存访问区域（内核态），一级巨页

    base_pgdir[P1X(HIGH_ADDR_IMM)] =
        ((LOW_ADDR_IMM >> PAGE_SHIFT) << 10) | PTE_RWX | PTE_GLOBAL | PTE_V;

    // 映射Pages区域
    map_segment(base_pgdir, 0, PADDR(pages), UPAGES,
                ROUND(npage * sizeof(struct Page), PAGE_SIZE),
                PTE_USER | PTE_RO | PTE_GLOBAL);
    // 映射Envs区域
    map_segment(base_pgdir, 0, PADDR(envs), UENVS,
                ROUND(NENV * sizeof(struct Env), PAGE_SIZE),
                PTE_USER | PTE_RO | PTE_GLOBAL);
}

/*
 * 概述：
 *
 *   初始化Env结构体的用户地址空间，建立基本页表映射。
 *
 * Precondition：
 * - 参数'e'必须指向全局数组'envs'中的有效Env结构体
 * - 全局模板页目录'base_pgdir'必须已正确初始化
 * - 物理页分配器'page_alloc'依赖的全局变量'page_free_list'必须有效
 *
 * Postcondition：
 * - 成功时返回0，并完成以下操作：
 *   a. 分配页目录，设置'e->env_pgdir'指向其内核虚拟地址
 *   b. 复制内核空间映射（[UTOP, ULIM)，包含envs、pages、User VPT）
 *   c. 建立UVPT自映射项（用户只读访问权限）
 *   d. 初始化用户空间页目录项为0
 * - 失败时返回错误码：
 *   -E_NO_MEM：物理页分配失败
 *
 * 副作用：
 * - 修改目标Env的'env_pgdir'字段
 * - 增加分配的物理页的引用计数'pp_ref'
 * - 修改全局空闲页链表'page_free_list'（通过page_alloc）
 */
static int env_setup_vm(struct Env *e) {
    /* Step 1:
     *   Allocate a page for the page directory with 'page_alloc'.
     *   Increase its 'pp_ref' and assign its kernel address to 'e->env_pgdir'.
     *
     * Hint:
     *   You can get the kernel address of a specified physical page using
     * 'page2kva'.
     */
    struct Page *p;
    try(page_alloc(&p));
    /* Exercise 3.3: Your code here. */

    p->pp_ref++;

    e->env_pgdir = (Pte *)page2kva(p);

    /* Step 2: Copy the template page directory 'base_pgdir' to 'e->env_pgdir'.
     */
    /* Hint:
     *   As a result, the address space of all envs is identical in [UTOP,
     * UVPT). See include/mmu.h for layout.
     */
    // 复制envs、pages区域的页目录映射
    memcpy(e->env_pgdir + P1X(UTOP), base_pgdir + P1X(UTOP),
           sizeof(Pte) * (P1X(UVPT) - P1X(UTOP)));

    // 复制直接映射区域的页目录映射

    e->env_pgdir[P1X(HIGH_ADDR_IMM)] = base_pgdir[P1X(HIGH_ADDR_IMM)];

    /* Step 3: Map its own page table at 'UVPT' with readonly permission.
     * As a result, user programs can read its page table through 'UVPT' */
    // 页目录中的一项映射4MB的内存空间，设置`env_pgdir`中的一项，相当于映射了4MB的内存空间
    // 此处，这4MB的内存空间刚好对应了进程自身的页表
    // 注意页表（env_pgdir）是4KB对齐的，故`PADDR(e->env_pgdir)`的低12位刚好为0
    // 这12位刚好对应硬件标志位、软件标志位
    e->env_pgdir[P1X(UVPT)] = PADDR(e->env_pgdir) | PTE_V;
    return 0;
}

/*
 * 概述：
 *
 *   分配并初始化一个新的Env结构体。
 *   成功时，新Env将存储在'*new'中。
 *
 *   本函数将设置Env的栈指针。
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
int env_alloc(struct Env **new, u_int parent_id) {
    int r;
    struct Env *e;

    /* Step 1: Get a free Env from 'env_free_list' */
    /* Exercise 3.4: Your code here. (1/4) */

    e = LIST_FIRST(&env_free_list);

    if (e == NULL) {
        return -E_NO_FREE_ENV;
    }

    /* Step 2: Call a 'env_setup_vm' to initialize the user address space for
     * this new Env. */
    /* Exercise 3.4: Your code here. (2/4) */

    r = env_setup_vm(e);

    if (r != 0) {
        return r;
    }

    /* Step 3: Initialize these fields for the new Env with appropriate values:
     *   'env_user_tlb_mod_entry' (lab4), 'env_runs' (lab6), 'env_id' (lab3),
     * 'env_asid' (lab3), 'env_parent_id' (lab3)
     *
     * Hint:
     *   Use 'asid_alloc' to allocate a free asid.
     *   Use 'mkenvid' to allocate a free envid.
     */
    e->env_user_tlb_mod_entry = 0; // for lab4
    e->env_runs = 0;               // for lab6
    /* Exercise 3.4: Your code here. (3/4) */

    e->env_id = mkenvid(e);

    r = asid_alloc(&e->env_asid);

    if (r < 0) {
        return r;
    }

    e->env_parent_id = parent_id;

    /* Step 4: Initialize the sp and 'cp0_status' in 'e->env_tf'.
     *   Set the EXL bit to ensure that the processor remains in kernel mode
     * during context recovery. Additionally, set UM to 1 so that when ERET
     * unsets EXL, the processor transitions to user mode.
     */
    // STATUS_IE：开启中断
    // STATUS_IM7：允许响应时钟中断
    // STATUS_EXL：处理器正在执行异常处理程序，强制处理器处于内核态
    // STATUS_UM：处理器处于用户态（只有STATUS_EXL = 0时才生效）

    // 只有当STATUS_EXL = 0 并且 STATUS_UM = 1时，处理器处于用户态
    // 在每次进程上下文切换时，`env_tf`中的寄存器都会被恢复，之后执行eret指令
    // 因此，此处要设置STATUS_EXL = 1，这样，当`env_tf`中的寄存器被恢复后，
    // 处理器仍处于内核态，可以继续执行eret指令
    // 当执行eret后，STATUS_EXL自动设置为0，且此时STATUS_UM = 1
    // 处理器以用户态继续执行用户进程

    // SIE = 0，当前在内核态，关闭中断
    // SPIE = 1，返回到用户态，开启中断
    // UBE = 0，小端
    // SPP = 0，之前的模式，0表示用户态
    e->env_tf.sstatus = SSTATUS_SPIE;

    // STIE = 1，启用时钟中断
    e->env_tf.sie = SIE_STIE;

    // 初始化sp寄存器（regs[2]），在栈顶为'argc'、'argv'参数预留空间
    e->env_tf.regs[2] = USTACKTOP - sizeof(int) - sizeof(char **);

    /* Step 5: Remove the new Env from env_free_list. */
    /* Exercise 3.4: Your code here. (4/4) */

    LIST_REMOVE(e, env_link);

    *new = e;
    return 0;
}

/* 概述:
 *   （含 TLB 操作）映射`env`对应的用户地址空间中的一页。
 *    将页权限设置为`perm | PTE_V`。
 *    若`src`不为空，将`src`起始，长度为`len`的数据复制到页中偏移量为`offset`的位置。
 *
 * Precondition:
 * - 'offset + len'不能大于'PAGE_SIZE' (由调用者保证)
 * - 每次调用必须传入对应**不同的的页**虚拟地址`va` (由调用者保证)
 * - `data` 必须指向有效的`struct Env`实例（即env结构体）
 * - `perm` 参数不得包含 PTE_V（由函数显式添加）
 * - 全局变量`page_free_list`必须为合法的空闲物理页链表
 *
 * Postcondition:
 * - 成功时返回0：
 *   - 物理页被插入到`env->env_pgdir`的`va`处
 *   - 若`src`不为NULL，数据被复制到物理页的`offset`位置
 *   - 页表项权限为`perm | PTE_V`
 * - 失败时返回错误码：
 *   - E_NO_MEM: 物理页分配失败、页表项分配失败
 *
 * 副作用：
 * - 可能修改全局变量`page_free_list`（通过page_alloc）
 * - 可能修改页表`env->env_pgdir`的内容
 * - 可能修改物理页`pp`的引用计数（通过page_insert）
 * - 可能使TLB条目失效（通过page_insert）
 */
static int load_icode_mapper(void *data, u_long va, size_t offset, u_int perm,
                             const void *src, size_t len) {
    struct Env *env = (struct Env *)data;
    struct Page *p;
    int r;

    /* Step 1: Allocate a page with 'page_alloc'. */
    /* Exercise 3.5: Your code here. (1/2) */

    r = page_alloc(&p);

    if (r < 0) {
        return r;
    }

    /* Step 2: If 'src' is not NULL, copy the 'len' bytes started at 'src' into
     * 'offset' at this page. */
    // Hint: You may want to use 'memcpy'.
    if (src != NULL) {
        /* Exercise 3.5: Your code here. (2/2) */

        memcpy((void *)(page2kva(p) + offset), src, len);
    }

    /* Step 3: Insert 'p' into 'env->env_pgdir' at 'va' with 'perm'. */
    return page_insert(env->env_pgdir, env->env_asid, p, va, perm);
}

/*
 * 概述：
 *   将 ELF 可执行镜像加载到用户环境 'e' 的地址空间中。
 *   解析 ELF 文件头，加载所有可加载段（PT_LOAD）到用户空间，并设置入口地址。
 *   具体步骤包括：
 *     1. 验证并解析 ELF 头。
 *     2. 遍历程序头表，加载所有类型为 PT_LOAD 的段。
 *     3. 设置用户环境的入口地址：保存到进程上下文的EPC寄存器中，以便`sret`后
 *        从此处开始执行。
 *
 * Precondition：
 * - `binary` 必须指向有效的 ELF 可执行文件（e_type 为 ET_EXEC），且通过
 * `elf_from` 的校验。
 * - `size` 为ELF 可执行文件的大小。
 * - 环境 `e` 必须已通过 `env_alloc` 分配。
 * - 全局物理页管理器（如 `page_free_list`）处于有效状态，能够分配所需物理页。
 *
 * Postcondition：
 * - ELF 文件中的所有 PT_LOAD 段被加载到用户空间的对应虚拟地址。
 * - 用户环境 `e` 的寄存器上下文中EPC（`e->env_tf.cp0_epc`） 被设置为 ELF
 * 头的入口地址 `ehdr->e_entry`。
 * - 用户环境的页目录 `e->env_pgdir` 包含新加载段的映射关系。
 *
 * 副作用：
 * - 修改用户环境 `e`
 * 的页目录和页表、寄存器上下文（EPC），可能分配新的物理页（通过
 * `load_icode_mapper`）。
 * - 修改全局物理页管理结构（如 `page_free_list` 的物理页被分配）。
 * - 若 ELF 校验失败（如魔数错误或非可执行类型），触发 panic。
 */
static void load_icode(struct Env *e, const void *binary, size_t size) {
    /* Step 1: Use 'elf_from' to parse an ELF header from 'binary'. */
    const Elf64_Ehdr *ehdr = elf_from(binary, size);
    if (!ehdr) {
        panic("bad elf at %x", binary);
    }

    /* Step 2: Load the segments using 'ELF_FOREACH_PHDR_OFF' and
     * 'elf_load_seg'. As a loader, we just care about loadable segments, so
     * parse only program headers here.
     */
    size_t ph_off;
    ELF_FOREACH_PHDR_OFF(ph_off, ehdr) {
        Elf64_Phdr *ph = (Elf64_Phdr *)((size_t)binary + ph_off);
        if (ph->p_type == PT_LOAD) {
            // 'elf_load_seg' is defined in lib/elfloader.c
            // 'load_icode_mapper' defines the way in which a page in this
            // segment should be mapped.
            panic_on(elf_load_seg(ph, (void *)((size_t)binary + ph->p_offset),
                                  load_icode_mapper, e));
        }
    }

    /* Step 3: Set 'e->env_tf.cp0_epc' to 'ehdr->e_entry'. */
    /* Exercise 3.6: Your code here. */
    e->env_tf.sepc = ehdr->e_entry;
}

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
struct Env *env_create(const void *binary, size_t size, uint32_t priority) {
    struct Env *e;
    /* Step 1: Use 'env_alloc' to alloc a new env, with 0 as 'parent_id'. */
    /* Exercise 3.7: Your code here. (1/3) */

    if (env_alloc(&e, 0) < 0) {
        return NULL;
    }

    /* Step 2: Assign the 'priority' to 'e' and mark its 'env_status' as
     * runnable.
     */
    /* Exercise 3.7: Your code here. (2/3) */

    e->env_pri = priority;
    e->env_status = ENV_RUNNABLE;

    /* Step 3: Use 'load_icode' to load the image from 'binary', and insert 'e'
     * into 'env_sched_list' using 'TAILQ_INSERT_HEAD'. */
    /* Exercise 3.7: Your code here. (3/3) */

    load_icode(e, binary, size);
    TAILQ_INSERT_HEAD(&env_sched_list, e, env_sched_link);

    return e;
}

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
void env_free(struct Env *e) {
    Pte *p2;
    Pte *p3;
    u_reg_t p1no, p2no, p3no, p1pa, p2pa;

    /* Hint: Note the environment's demise.*/
    printk("[%08x] free env %08x\n", curenv ? curenv->env_id : 0, e->env_id);

    /* Hint: Flush all mapped pages in the user portion of the address space */
    // 释放该进程中用户空间映射的所有页，注意，这只含到UTOP的页
    // 不包括映射的pages、envs、User VPT
    // 具体的，先检查页目录，对于页目录中有效的目录项
    // 再检查对应的二级页表，对于二级页表中的有效项
    // 再检查对应的三级页表
    for (p1no = 0; p1no < P1X(UTOP); p1no++) {
        Pte *p1_entry = &e->env_pgdir[p1no];

        /* Hint: only look at mapped page tables. */
        if (!(*p1_entry & PTE_V)) {
            continue;
        }

        // 一级巨页
        if (PTE_IS_NON_LEAF(*p1_entry) == 0) {
            panic("Huge page is not supported, level = %d env = %08x va = "
                  "0x%016lx\n",
                  1, e->env_id, p1no << P1SHIFT);
        }

        /* Hint: find the pa and va of the page table. */
        // 对应二级页表的物理地址
        p1pa = PTE_ADDR(*p1_entry);
        p2 = (Pte *)P2KADDR(p1pa);

        /* Hint: Unmap all PTEs in this page table. */
        for (p2no = 0; p2no <= P2X(~0ULL); p2no++) {
            Pte *p2_entry = &p2[p2no];

            if (!(*p2_entry & PTE_V)) {
                continue;
            }

            if (PTE_IS_NON_LEAF(*p2_entry) == 0) {
                panic("Huge page is not supported, level = %d env = %08x va = "
                      "0x%016lx\n",
                      2, e->env_id, (p1no << P1SHIFT) | (p2no << P2SHIFT));
            }

            p2pa = PTE_ADDR(*p2_entry);
            p3 = (Pte *)P2KADDR(p2pa);

            for (p3no = 0; p3no <= P3X(~0ULL); p3no++) {
                Pte *p3_entry = &p3[p3no];

                if (!(*p3_entry & PTE_V)) {
                    continue;
                }

                page_remove(e->env_pgdir, e->env_asid,
                            (p1no << P1SHIFT) | (p2no << P2SHIFT) |
                                (p3no << P3SHIFT));
            }

            // 将二级页表项设置为无效
            *p2_entry = 0;

            // 将三级页表自身的物理页取消映射
            page_decref(pa2page(p2pa));
        }

        /* Hint: free the page table itself. */
        // 将目录项设置为无效
        *p1_entry = 0;
        // 将二级页表自身的物理页取消映射（引用计数-1）
        page_decref(pa2page(p1pa));
    }
    /* Hint: free the page directory. */
    page_decref(pa2page(PADDR(e->env_pgdir)));
    /* Hint: free the ASID */
    asid_free(e->env_asid);
    /* Hint: invalidate page directory in TLB */

    tlb_flush_asid(e->env_asid);

    /* Hint: return the environment to the free list. */
    e->env_status = ENV_FREE;
    LIST_INSERT_HEAD((&env_free_list), (e), env_link);
    TAILQ_REMOVE(&env_sched_list, (e), env_sched_link);
}

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
void env_destroy(struct Env *e) {
    /* Hint: free e. */
    env_free(e);

    /* Hint: schedule to run a new environment. */
    if (curenv == e) {
        curenv = NULL;
        printk("i am killed ... \n");
        schedule(1);
    }
}

// WARNING BEGIN: DO NOT MODIFY FOLLOWING LINES!
#ifdef MOS_PRE_ENV_RUN
#include <generated/pre_env_run.h>
#endif
// WARNING END

/*
 * env_pop_tf:
 *
 * 功能：从指定的 Trapframe 恢复上下文，重置时钟，设置ASID，并返回到用户态
 *
 * Precondition:
 * - tf 必须指向一个有效的 Trapframe 结构，该结构包含完整的寄存器状态
 * - asid 必须是一个有效的地址空间标识符，与当前进程对应
 * - p1_ppn 必须对应用户进程一级页表的物理页号
 *
 * Postcondition:
 * - 恢复 Trapframe 中的所有寄存器状态，重置时钟计数器
 * - 通过 sret 指令返回到用户态，完成上下文切换
 *
 * 副作用：
 * - 修改 CP0_ENTRYHI 寄存器以设置 ASID
 * - 通过 RESET_KCLOCK 重置时钟相关寄存器
 */
extern void env_pop_tf(struct Trapframe *tf, uint16_t asid, u_reg_t p1_ppn)
    __attribute__((noreturn));

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
void env_run(struct Env *e) {
    assert(e->env_status == ENV_RUNNABLE);
    // WARNING BEGIN: DO NOT MODIFY FOLLOWING LINES!
#ifdef MOS_PRE_ENV_RUN
    MOS_PRE_ENV_RUN_STMT
#endif
    // WARNING END

    /* Step 1:
     *   If 'curenv' is NULL, this is the first time through.
     *   If not, we may be switching from a previous env, so save its context
     * into 'curenv->env_tf' first.
     */
    if (curenv) {
        curenv->env_tf = *(((struct Trapframe *)KSTACKTOP) - 1);
    }

    /* Step 2: Change 'curenv' to 'e'. */
    curenv = e;
    curenv->env_runs++; // lab6

    /* Step 3: Change 'cur_pgdir' to 'curenv->env_pgdir', switching to its
     * address space. */
    /* Exercise 3.8: Your code here. (1/2) */

    cur_pgdir = curenv->env_pgdir;

    /* Step 4: Use 'env_pop_tf' to restore the curenv's saved context
     * (registers) and return/go to user mode.
     *
     * Hint:
     *  - You should use 'curenv->env_asid' here.
     *  - 'env_pop_tf' is a 'noreturn' function: it restores PC from 'cp0_epc'
     * thus not returning to the kernel caller, making 'env_run' a 'noreturn'
     * function as well.
     */
    /* Exercise 3.8: Your code here. (2/2) */

    env_pop_tf(&curenv->env_tf, curenv->env_asid,
               PADDR(cur_pgdir) >> PAGE_SHIFT);
}

void env_check() {
    struct Env *pe, *pe0, *pe1, *pe2;
    struct Env_list fl;
    u_long page_addr;
    /* should be able to allocate three envs */
    pe0 = 0;
    pe1 = 0;
    pe2 = 0;
    assert(env_alloc(&pe0, 0) == 0);
    assert(env_alloc(&pe1, 0) == 0);
    assert(env_alloc(&pe2, 0) == 0);

    assert(pe0);
    assert(pe1 && pe1 != pe0);
    assert(pe2 && pe2 != pe1 && pe2 != pe0);

    /* temporarily steal the rest of the free envs */
    fl = env_free_list;
    /* now this env_free list must be empty! */
    LIST_INIT(&env_free_list);

    /* should be no free env */
    assert(env_alloc(&pe, 0) == -E_NO_FREE_ENV);

    /* recover env_free_list */
    env_free_list = fl;

    printk("pe0->env_id %d\n", pe0->env_id);
    printk("pe1->env_id %d\n", pe1->env_id);
    printk("pe2->env_id %d\n", pe2->env_id);

    assert(pe0->env_id == 2048);
    assert(pe1->env_id == 4097);
    assert(pe2->env_id == 6146);
    printk("env_init() work well!\n");

    /* 'UENVS' and 'UPAGES' should have been correctly mapped in *template* page
     * directory 'base_pgdir'. */
    for (page_addr = 0; page_addr < npage * sizeof(struct Page);
         page_addr += PAGE_SIZE) {
        assert(va2pa(base_pgdir, UPAGES + page_addr) ==
               PADDR(pages) + page_addr);
    }
    for (page_addr = 0; page_addr < NENV * sizeof(struct Env);
         page_addr += PAGE_SIZE) {
        assert(va2pa(base_pgdir, UENVS + page_addr) == PADDR(envs) + page_addr);
    }
    /* check env_setup_vm() work well */
    printk("pe1->env_pgdir 0x%016lx\n", pe1->env_pgdir);

    assert(pe2->env_pgdir[P1X(UTOP)] == base_pgdir[P1X(UTOP)]);
    assert(pe2->env_pgdir[P1X(UTOP) - 1] == 0);
    printk("env_setup_vm passed!\n");

    printk("pe2`s sp register 0x%016lx\n", pe2->env_tf.regs[2]);

    /* free all env allocated in this function */
    TAILQ_INSERT_TAIL(&env_sched_list, pe0, env_sched_link);
    TAILQ_INSERT_TAIL(&env_sched_list, pe1, env_sched_link);
    TAILQ_INSERT_TAIL(&env_sched_list, pe2, env_sched_link);

    env_free(pe2);
    env_free(pe1);
    env_free(pe0);

    printk("env_check() succeeded!\n");
}

void envid2env_check() {
    struct Env *pe, *pe0, *pe2;
    assert(env_alloc(&pe0, 0) == 0);
    assert(env_alloc(&pe2, 0) == 0);
    int re;
    pe2->env_status = ENV_FREE;
    re = envid2env(pe2->env_id, &pe, 0);

    assert(re == -E_BAD_ENV);

    pe2->env_status = ENV_RUNNABLE;
    re = envid2env(pe2->env_id, &pe, 0);

    assert(pe->env_id == pe2->env_id && re == 0);

    curenv = pe0;
    re = envid2env(pe2->env_id, &pe, 1);
    assert(re == -E_BAD_ENV);
    printk("envid2env() work well!\n");
}
