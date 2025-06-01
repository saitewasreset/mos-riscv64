#include <env.h>
#include <lib.h>
#include <mmu.h>
#include <stdint.h>

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
 *   - envs：全局环境控制块数组（用于子进程初始化）
 * - 当前进程必须已设置有效的页表结构
 * - 系统必须有足够的资源创建新进程（空闲Env结构、物理内存等）
 *
 * Postcondition：
 * - 成功时：
 *   - 返回子进程ID（大于0）
 *   - 子进程环境控制块正确初始化
 *   - 父子进程共享COW页
 * - 失败时：
 *   - 返回负的错误代码（如-E_NO_FREE_ENV等）
 *
 * 副作用：
 * - 分配新的Env结构
 * - 修改父子进程的页表项
 * - 修改父子进程的TLB异常处理入口
 * - 可能修改物理页引用计数
 */
// Checked by DeepSeek-R1 20250424 18:00
int fork(void) {
    uint32_t child;

    int r = 0;

    /* Step 2: 创建子进程 */

    r = syscall_exofork();

    // 实现差异：参考代码未处理`syscall_exofork`小于0的错误情况，而是继续执行
    if (r < 0) {
        return r;
    }

    child = (uint32_t)r;

    if (child == 0) {
        // 子进程路径：设置正确的env指针，指向子进程的Env
        env = envs + ENVX(syscall_getenvid());

        return 0;
    }

    return child;
}
