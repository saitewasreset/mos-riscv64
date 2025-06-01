#include "printk.h"
#include <fork.h>
#include <pmap.h>

static void duppage(Pte *entry, Pte *child_pgdir, uint16_t child_asid,
                    u_reg_t vpn) {
    int r;
    u_reg_t addr;
    uint32_t perm;

    perm = PTE_FLAGS(*entry);
    addr = (vpn << PAGE_SHIFT);

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
        // 处理可写页面（设置为 COW 并清除 W 标志）
    } else if ((perm & PTE_W) != 0) {
        new_perm = (perm & (~PTE_W)) | PTE_COW;
        // 其他情况（只读页等保持原权限）
    } else {
        new_perm = perm;
    }

    void *va = (void *)addr;

    /* 关键点：必须先映射子进程再重映射父进程，避免竞争条件 */

    if ((r = page_insert(child_pgdir, child_asid, pa2page(PTE_ADDR(*entry)),
                         (vpn << 12), new_perm)) < 0) {
        panic("duppage: failed to insert page for child vpn = 0x%016lx: %d\n",
              vpn, r);
    }

    // 清除原有标志位
    *entry &= ~GENMASK(9, 0);
    *entry |= new_perm;
}

void dup_userspace(Pte *parent_pgdir, Pte *child_pgdir, uint16_t child_asid) {
    uint32_t pte_count_per_page = (PAGE_SIZE / sizeof(Pte));

    u_reg_t current_vpn = 0;

    for (uint32_t p1x = 0; p1x <= P1X(USTACKTOP); p1x++) {
        Pte current_p1entry = parent_pgdir[p1x];

        if ((current_p1entry & PTE_V) != 0) {
            Pte *p2 = (Pte *)P2KADDR(PTE_ADDR(current_p1entry));

            for (uint32_t p2x = 0; p2x < pte_count_per_page; p2x++) {

                current_vpn = (p1x << 18) || (p2x << 9);

                if (current_vpn >= VPN(USTACKTOP)) {
                    break;
                }

                Pte current_p2entry = p2[p2x];

                if ((current_p2entry & PTE_V) != 0) {
                    Pte *p3 = (Pte *)P2KADDR(PTE_ADDR(current_p2entry));

                    for (uint32_t p3x = 0; p3x < pte_count_per_page; p3x++) {

                        current_vpn = (p1x << 18) | (p2x << 9) | p3x;

                        if (current_vpn >= VPN(USTACKTOP)) {
                            break;
                        }

                        Pte current_p3entry = p3[p3x];

                        if ((current_p3entry & PTE_V) != 0) {
                            duppage(&p3[p3x], child_pgdir, child_asid,
                                    current_vpn);
                        }
                    }
                }
            }
        }
    }
}