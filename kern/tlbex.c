#include <bitops.h>
#include <env.h>
#include <pmap.h>

/* 概述：
 *
 * 使 ASID 对应的虚拟地址空间中映射虚拟地址 `va` 的 TLB
 * 条目失效。
 *
 * 具体的，该页和相邻页的映射都将从 TLB 中移除。
 *
 * Preconditon:
 * - `va` 不一定是页对齐的。
 *
 * Postconditon:
 *
 * 如果 TLB 中存在特定条目，则通过将对应的 TLB 条目写为零来使其失效；
 *
 * 否则，不会发生任何操作。
 *
 * 由于 4Kc TLB 条目的结构，与该虚拟地址对应的页 **以及** 相邻页的映射将被移除。
 */
void tlb_invalidate(u_int asid, u_long va) {
    // GENMASK(PGSHIFT, 0) 生成 [0, 12] 位（**共 13 位**）为 1 的掩码
    // va & ~GENMASK(PGSHIFT, 0) 得到 VPN2
    tlb_out((va & ~GENMASK(PGSHIFT, 0)) | (asid & (NASID - 1)));
}

/*
 * 概述：
 *
 * （含 TLB 操作）在校验指定虚拟地址`va`位于允许的用户空间区域后，
 * 分配一个新的物理页面
 * 将该物理页面映射到该地址并增加该页面的引用计数。
 *
 * 若虚拟地址`va`已经存在有效映射，移除原映射，原映射的物理页的引用计数 -1。
 *
 * 页面权限根据虚拟地址所属区域进行配置：
 * 内核映射的用户区域（UVPT 至 ULIM）不设置 PTE_D 标志（只读访问）。
 *
 * Precondition:
 *
 * - `va` **无需**页面对齐，但必须满足以下条件：
 *   - `va >= UTEMP`（0x003f e000）
 *   - `va` 不在 [USTACKTOP, USTACKTOP + PAGE_SIZE) 区间内 [0x7f3f e000，0x7f3f
 * f000）
 *   - `va` 不在 [UENVS, UPAGES)、[UPAGES, UVPT) 区间内，且不 >= ULIM
 * - `pgdir` 必须指向有效的页目录
 * - `asid` 必须是有效的 ASID（用于 TLB 管理）
 *
 * Postcondition:
 *
 * - 分配物理页面并插入`pgdir`页目录的`va`映射项
 * - `va`对应的页表项权限设置为 PTE_C_CACHEABLE | PTE_V |
 *   （若 va < UVPT 则附加 PTE_D）
 * - 移除`va`地址原有的所有映射，原映射的物理页的引用计数将 -1
 * - 已分配页面的`pp_ref`引用计数增加
 */
static void passive_alloc(u_int va, Pde *pgdir, u_int asid) {
    struct Page *p = NULL;

    if (va < UTEMP) {
        panic("address too low");
    }

    if (va >= USTACKTOP && va < USTACKTOP + PAGE_SIZE) {
        panic("invalid memory");
    }

    if (va >= UENVS && va < UPAGES) {
        panic("envs zone");
    }

    if (va >= UPAGES && va < UVPT) {
        panic("pages zone");
    }

    if (va >= ULIM) {
        panic("kernel address");
    }

    panic_on(page_alloc(&p));

    // Postconditon for `page_alloc`: now, p points to the allocated Page

    panic_on(page_insert(pgdir, asid, p, PTE_ADDR(va),
                         (va >= UVPT && va < ULIM) ? 0 : PTE_D));
}

/*
 * 概述：
 *
 * （含 TLB 操作）处理 TLB refill
 *
 *  在任何情况下，TLB 中的原有映射（若存在）都将被移除
 *
 *  - 若虚拟地址`va`已经存在有效映射，按要求设置栈上的变量；
 *  - 若虚拟地址`va`不存在有效映射，分配新的物理页，
 *    引用计数 +1，按要求设置栈上的变量。
 *
 *
 * Precondition:
 *
 * - `va` **无需**页面对齐，但必须满足以下条件（是用户空间的合法地址）：
 *   - `va >= UTEMP`（0x003f e000）
 *   - `va` 不在 [USTACKTOP, USTACKTOP + PAGE_SIZE) 区间内 [0x7f3f e000，0x7f3f
 * f000）
 *   - `va` 不在 [UENVS, UPAGES)、[UPAGES, UVPT) 区间内，且不 >= ULIM
 * - `asid` 必须是有效的 ASID
 *
 * Postcondition:
 *
 * - pentrylo[0] 包含了 (VPN2 | 0) 页的硬件页表项
 * - pentrylo[1] 包含了 (VPN2 | 1) 页的硬件页表项
 */
void _do_tlb_refill(u_long *pentrylo, u_int va, u_int asid) {
    tlb_invalidate(asid, va);
    Pte *ppte;
    /* Hints:
     *  Invoke 'page_lookup' repeatedly in a loop to find the page table entry
     * '*ppte' associated with the virtual address 'va' in the current address
     * space 'cur_pgdir'.
     *
     *  **While** 'page_lookup' returns 'NULL', indicating that the '*ppte'
     * could not be found, allocate a new page using 'passive_alloc' until
     * 'page_lookup' succeeds.
     */

    /* Exercise 2.9: Your code here. */

    struct Page *page = NULL;

    // The `while` clause is needed because after call to `passive_alloc`,
    // `ppte` is still unset and another `page_lookup` is needed
    while ((page = page_lookup(cur_pgdir, va, &ppte)) == NULL) {
        passive_alloc(va, cur_pgdir, asid);
    }

    // 将页表项地址的低 3 位置 0，注意页表项本来就是 4 字节对齐的（低 2 位是 0）
    // 这样，ppte[0] 为 (VPN2 | 0) 对应的页表项，ppte[1] 为 (VPN2 | 1)
    // 对应的页表项
    ppte = (Pte *)((u_long)ppte & ~0x7);
    // 移除 6 位软件标志位，转化为硬件页表项
    pentrylo[0] = ppte[0] >> 6;
    pentrylo[1] = ppte[1] >> 6;
}

#if !defined(LAB) || LAB >= 4

/*
 * 概述：
 *   内核中的TLB Mod异常处理函数。我们的内核允许用户程序在用户模式下处理TLB
 * Mod异常，
 *   因此我们将异常上下文'tf'复制到用户异常栈(UXSTACK)中，并修改EPC寄存器指向注册的
 *   用户异常处理入口。
 *
 *   注意，处理函数将在用户态运行。
 *
 * 使用用户异常栈(UXSTACK)而非普通用户栈处理异常
 *
 * Precondition：
 * - tf必须指向有效的Trapframe结构
 * - 依赖全局状态：
 *   - curenv：当前运行环境，必须有效且已设置env_user_tlb_mod_entry
 *
 * Postcondition：
 * - 若用户处理函数已注册：
 *   - 将异常上下文保存到用户异常栈
 *   - 设置a0寄存器指向保存的上下文
 *   - 设置EPC指向用户处理函数
 * - 若未注册处理函数，触发panic
 *
 * 副作用：
 * - 修改用户异常栈内容
 * - 修改传入的Trapframe结构内容(寄存器值和EPC)
 */
void do_tlb_mod(struct Trapframe *tf) {
    struct Trapframe tmp_tf = *tf;

    // 若这是第一次发生异常，将用户栈设置为用户异常栈栈顶；
    // 否则，（说明发生嵌套TLB Mod异常），直接使用上次异常的sp
    // 这样可正确处理嵌套异常
    if (tf->regs[29] < USTACKTOP || tf->regs[29] >= UXSTACKTOP) {
        tf->regs[29] = UXSTACKTOP;
    }
    tf->regs[29] -= sizeof(struct Trapframe);
    *(struct Trapframe *)tf->regs[29] = tmp_tf;

    Pte *pte;
    page_lookup(cur_pgdir, tf->cp0_badvaddr, &pte);

    if (curenv->env_user_tlb_mod_entry) {
        // 4 -> a0
        tf->regs[4] = tf->regs[29];
        // 29 -> sp
        tf->regs[29] -= sizeof(tf->regs[4]);
        // Hint: Set 'cp0_epc' in the context 'tf' to
        // 'curenv->env_user_tlb_mod_entry'.
        /* Exercise 4.11: Your code here. */
        tf->cp0_epc = curenv->env_user_tlb_mod_entry;

    } else {
        panic("TLB Mod but no user handler registered");
    }
}
#endif
