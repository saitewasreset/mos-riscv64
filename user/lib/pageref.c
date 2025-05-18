#include <lib.h>

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
int pageref(void *v) {
    u_int pte;

    /* Step 1: Check the page directory. */
    if (!(vpd[PDX(v)] & PTE_V)) {
        return 0;
    }

    /* Step 2: Check the page table. */
    pte = vpt[VPN(v)];

    if (!(pte & PTE_V)) {
        return 0;
    }
    /* Step 3: Return the result. */
    return pages[PPN(pte)].pp_ref;
}
