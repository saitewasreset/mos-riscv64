#include "types.h"
#include <env.h>
#include <lib.h>
#include <mmu.h>

/*
 * 概述：
 *   处理TLB修改异常，将触发异常的页映射为私有可写副本（写时复制机制）。
 *   函数会检查虚拟地址是否为合法的COW页，若不是则触发panic；
 *   若是则分配新物理页并复制内容，建立新的可写映射。
 *
 *   使用临时地址UCOW作为中转，分配新物理页并复制内容后立即解除映射
 *
 * Precondition：
 * - 全局变量'tf'必须包含有效的陷阱帧信息（包含异常发生时状态）
 * - 依赖全局状态：
 *   - vpt：当前进程页表的用户空间只读视图（用于获取页表项）
 *   - syscall_mem_*函数依赖的环境控制块（如curenv）必须有效
 *  - 'tf->cp0_badvaddr'必须是触发TLB Mod异常的虚拟地址
 *
 * Postcondition：
 * - 成功时：
 *   - 异常地址va被重新映射到新的可写私有副本
 *   - 原页内容被完整复制到新页
 *   - 页表项权限去除PTE_COW标志，保留其他权限并添加PTE_D标志
 * - 失败时：
 *   - 若va不是COW页（无PTE_COW标志），触发user_panic
 *   - 若内存分配/映射操作失败，触发user_panic
 *
 * 副作用：
 * - 可能修改当前进程的页表结构（通过syscall_mem_*函数）
 * - 建立临时映射到UCOW地址并随后解除
 * - 修改系统调用计数器（通过syscall_mem_*内部处理）
 */
// Checked by DeepSeek-R1 20250424 16:01
static void __attribute__((noreturn)) cow_entry(struct Trapframe *tf) {
    u_long va = tf->cp0_badvaddr;
    u_long perm;

    int r = 0;

    /* Step 1: Find the 'perm' in which the faulting address 'va' is mapped. */
    /* Hint: Use 'vpt' and 'VPN' to find the page table entry. If the 'perm'
     * doesn't have 'PTE_COW', launch a 'user_panic'. */
    /* Exercise 4.13: Your code here. (1/6) */

    perm = PTE_FLAGS(vpt[VPN(va)]);

    if ((perm & PTE_COW) == 0) {
        user_panic(
            "cow_entry called with va %08x, but it doesn't have PTE_COW flag",
            va);
    }

    /* Step 2: Remove 'PTE_COW' from the 'perm', and add 'PTE_D' to it. */
    /* Exercise 4.13: Your code here. (2/6) */

    u_long new_perm = (perm & ~PTE_COW) | PTE_D;

    /* Step 3: Allocate a new page at 'UCOW'. */
    /* Exercise 4.13: Your code here. (3/6) */

    if ((r = syscall_mem_alloc(0, (void *)UCOW, new_perm)) != 0) {
        user_panic("cow_entry failed allocate page for va %08x, ret: %d",
                   (void *)UCOW, r);
    }

    /* Step 4: Copy the content of the faulting page at 'va' to 'UCOW'. */
    /* 注意：va可能未按页对齐，需用ROUNDDOWN获取页起始地址 */
    /* Exercise 4.13: Your code here. (4/6) */
    u_long page_begin_va = ROUNDDOWN(va, PAGE_SIZE);
    memcpy((void *)UCOW, (void *)page_begin_va, PAGE_SIZE);

    // Step 5: Map the page at 'UCOW' to 'va' with the new 'perm'.
    /* Exercise 4.13: Your code here. (5/6) */
    if ((r = syscall_mem_map(0, (void *)UCOW, 0, (void *)page_begin_va,
                             new_perm)) != 0) {
        user_panic("cow_entry failed map page from %08x to %08x, ret: %d",
                   (void *)UCOW, (void *)page_begin_va, r);
    }

    // Step 6: Unmap the page at 'UCOW'.
    /* Exercise 4.13: Your code here. (6/6) */
    if ((r = syscall_mem_unmap(0, (void *)UCOW)) != 0) {
        user_panic("cow_entry failed unmap page at %08x, ret: %d",
                   (void *)UCOW);
    }

    // Step 7: Return to the faulting routine.
    // 对于syscall_set_trapframe，若目标进程是当前进程，该系统调用将立即将本进程恢复到`tf`中的状态执行。
    // 并不会返回
    r = syscall_set_trapframe(0, tf);
    user_panic("syscall_set_trapframe returned %d", r);
}

/*
 * 概述：
 *   将当前进程（父进程）的虚拟页 vpn 映射到子进程 envid 的地址空间中，
 *   并根据页面属性，设置适当的权限标志（特别是处理写时复制场景）。
 *   该函数主要用于 fork 时的页面共享处理。
 *
 *   - 对于 PTE_D 置位且 PTE_LIBRARY 未置位的页面，父子进程的映射都会改为
 * PTE_COW 且清除 PTE_D
 *   - 其他情况（只读页或共享页）保持原权限直接映射
 *
 * Precondition：
 * - envid 必须是有效的子进程 ID（0 表示当前进程）
 * - vpn 必须是当前进程地址空间中的合法虚拟页号
 * - 依赖全局状态：
 *   - vpt：当前进程的页表只读视图（UVPT 区域映射）
 *   - 当前进程的页目录必须有效且包含 vpn 对应的映射
 *
 * Postcondition：
 * - 成功时：
 *   - 子进程的 vpn 页被映射到与父进程相同的物理页
 *   - 若原页可写且非共享，父子进程的页表项都会更新为 PTE_COW 且清除 PTE_D
 *   - 其他情况保持原权限映射
 * - 失败时触发用户态 panic（通过 user_panic）
 *
 * 副作用：
 * - 修改子进程的页表结构（通过 syscall_mem_map）
 * - 可能修改父进程的页表项（对于 COW 情况）
 * - 可能增加物理页的引用计数（通过底层 sys_mem_map 实现）
 * - 注意，当映射父进程的栈时，在本函数执行过程中就会触发TLB Mod异常
 */
// Checked by DeepSeek-R1 and reference solution 20250424 15:17
static void duppage(u_int envid, u_int vpn) {
    int r;
    u_int addr;
    u_int perm;

    /* Step 1: Get the permission of the page. */
    /* Hint: Use 'vpt' to find the page table entry. */
    /* Exercise 4.10: Your code here. (1/2) */

    // vpt is `const volatile Pte *`!
    const volatile Pte *pte = &vpt[vpn];

    perm = PTE_FLAGS(*pte);
    addr = (vpn << PGSHIFT);

    /* Step 2: If the page is writable, and not shared with children, and not
     * marked as COW yet, then map it as copy-on-write, both in the parent (0)
     * and the child (envid). */
    /* Hint: The page should be first mapped to the child before remapped in the
     * parent. (Why?)
     */
    /*
     * 若先在父进程中进行重映射，当父进程调用syscall_mem_map映射父进程的用户栈时，会发生如下情况：
     * `duppage`尝试将返回的值写到栈上，但栈已经由syscall_mem_map标记为CoW，不可写
     * 触发TLB
     * Mod异常，（此时父进程的异常处理函数已经设置），在异常处理函数中，分配新的物理页
     * 并修改父进程的页表，**指向新的物理页**并**取消CoW标志**
     * 之后，调用syscall_mem_map将该物理页映射到子进程，但注意，现在映射的已经是父进程**新指向的物理页**
     * 该页面对于父进程的可写的，使得父进程fork后的更改被子进程共享，导致错误
     */
    /* Exercise 4.10: Your code here. (2/2) */

    u_int new_perm = 0;

    // 注意，应当使用`!= 0`而非`==
    // 1`检查，因为运算的结果位于`PTE_LIBRARY`对应的位上，而非最低位
    // 处理共享库页面（保持原权限）
    if ((perm & PTE_LIBRARY) != 0) {
        new_perm = perm;
        // 处理可写页面（设置为 COW 并清除 D 标志）
    } else if ((perm & PTE_D) != 0) {
        new_perm = (perm & (~PTE_D)) | PTE_COW;
        // 其他情况（只读页等保持原权限）
    } else {
        new_perm = perm;
    }

    void *va = (void *)(unsigned long)addr;

    /* 关键点：必须先映射子进程再重映射父进程，避免竞争条件 */

    if ((r = syscall_mem_map(0, va, envid, va, new_perm)) != 0) {
        user_panic("syscall_mem_map failed for va 0x%08x with %d\n", va, r);
    }

    if ((r = syscall_mem_map(0, va, 0, va, new_perm)) != 0) {
        user_panic("syscall_mem_map failed for va 0x%08x with %d\n", va, r);
    }
}

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
// Checked by DeepSeek-R1 20250424 18:00
int fork(void) {
    u_int child;

    int r = 0;

    /* Step 1: Set our TLB Mod user exception entry to 'cow_entry' if not done
     * yet. */
    if (env->env_user_tlb_mod_entry != (u_int)cow_entry) {
        try(syscall_set_tlb_mod_entry(0, cow_entry));
    }

    /* Step 2: 创建子进程（此时尚未准备好调度） */
    /* 关键点：
     * - syscall_exofork必须是内联函数，否则子进程的栈帧可能被覆盖
     * - 此时父子进程内存还未共享，子进程仅由基本模板页目录
     * - 子进程暂时不能运行，直到完成地址空间复制
     */
    // 注意：syscall_exofork必须是内联函数
    // 此时还未复制页面，父子进程共享相同的物理页
    // 此时子进程无法运行
    // 而父进程继续执行下面的代码，这将导致栈空间被修改
    // 具体的，（若syscall_exofork不是内联函数）
    // 则返回时，其栈帧被释放，且在之后调用`syscall_getenvid`
    // 等函数时，将被覆盖
    // 当子进程最终恢复执行时，从被覆盖的栈上读取返回地址
    // 将导致错误
    r = syscall_exofork();

    // 实现差异：参考代码未处理`syscall_exofork`小于0的错误情况，而是继续执行
    if (r < 0) {
        return r;
    }

    child = (u_int)r;

    if (child == 0) {
        // 子进程路径：设置正确的env指针，指向子进程的Env
        env = envs + ENVX(syscall_getenvid());

        return 0;
    }

    /* Step 3: 将USTACKTOP以下所有已映射页复制到子进程地址空间 */
    /* 关键点：
     * - 使用vpd/vpt遍历页表结构
     * - 每个页目录项对应4MB地址空间
     * - 每个页表项对应4KB地址空间
     * - 跳过超过USTACKTOP的虚拟页
     */
    u_int pte_count_per_page = (PAGE_SIZE / sizeof(Pte));

    for (u_int pdx = 0; pdx <= PDX(USTACKTOP); pdx++) {
        Pde current_pde = vpd[pdx];

        if ((current_pde & PTE_V) != 0) {
            for (u_int ptx = 0; ptx < pte_count_per_page; ptx++) {
                u_int pte_idx = pdx * pte_count_per_page + ptx;

                Pte current_pte = vpt[pte_idx];

                if ((current_pte & PTE_V) != 0) {
                    u_int vpn = (pdx << (PDSHIFT - PGSHIFT)) | ptx;

                    // 跳过超过USTACKTOP的虚拟页
                    if (vpn >= VPN(USTACKTOP)) {
                        break;
                    }

                    duppage(child, vpn);
                }
            }
        }
    }

    /* Step 4: Set up the child's tlb mod handler and set child's 'env_status'
     * to 'ENV_RUNNABLE'. */
    /* Hint:
     *   You may use 'syscall_set_tlb_mod_entry' and 'syscall_set_env_status'
     *   Child's TLB Mod user exception entry should handle COW, so set it to
     * 'cow_entry'
     */
    /* Exercise 4.15: Your code here. (2/2) */
    /* 关键点：
     * - 子进程也需要设置cow_entry处理COW页
     * - 必须显式设置状态为ENV_RUNNABLE才能被调度
     */
    if ((r = syscall_set_tlb_mod_entry(child, cow_entry)) != 0) {
        return r;
    }

    if ((r = syscall_set_env_status(child, ENV_RUNNABLE)) != 0) {
        return r;
    }

    return child;
}
