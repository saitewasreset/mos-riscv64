#include "queue.h"
#include <env.h>
#include <pmap.h>
#include <printk.h>

void dump_schedule_list(void) {
    struct Env *cur;

    printk("Dumping schedule list: ");

    uint32_t count = 0;

    TAILQ_FOREACH(cur, &env_sched_list, env_sched_link) {
        printk("%s ", cur->env_name);

        count++;

        if (count >= 10) {
            printk("limit_exceed");
            break;
        }
    }

    printk("\n");
}

/*
 * 概述：
 *   实现时间片轮转调度算法，从可运行环境列表中选择一个环境并使用'env_run'调度运行。
 *
 *   当需要切换进程时（如主动让出、时间片耗尽、当前进程不可运行等），从调度队列头部
 * 选取新进程，设置其优先级对应的时间片。若原进程仍可运行，将其移至队列尾部以保证
 * 公平性。非可运行进程的移除由其他函数保证。
 *
 *   当无需切换进程时（时间片未用完、未让出、仍可运行），继续运行当前进程，并
 * 减少其可运行的时间片。
 *
 *   **不要在本函数中修改`curenv`的值，其值应当通过`env_run`函数修改**
 *
 * Precondition：
 * - 全局变量'env_sched_list'仅包含且必须包含所有ENV_RUNNABLE状态的进程
 * - 全局变量'curenv'在首次调度前应为NULL
 * - 非可运行进程的移除由其他函数维护（如env_destroy/env_block等）
 *
 * Postcondition：
 * - 若yield非零，当前进程不会（在当前轮次）被再次调度（除非是唯一可运行进程）
 * - 调度队列中所有进程保持ENV_RUNNABLE状态（需由其他函数维护）
 * - 新调度进程的时间片计数被初始化为其优先级值减1，其将可以运行“优先级”次
 *
 * 副作用：
 * - 修改静态变量count（当前进程剩余时间片）
 * - 修改全局变量curenv（通过env_run）
 * - 可能调整env_sched_list队列结构（移动可运行进程至队尾）
 */
void schedule(int yield) {
    static int count = 0; // remaining time slices of current env
    struct Env *e = curenv;

    /* We always decrease the 'count' by 1.
     *
     * If 'yield' is set, or 'count' has been decreased to 0, or 'e' (previous
     * 'curenv') is 'NULL', or 'e' is not runnable, then we pick up a new env
     * from 'env_sched_list' (list of all runnable envs), set 'count' to its
     * priority, and schedule it with 'env_run'. **Panic if that list is
     * empty**.
     *
     * (Note that if 'e' is still a runnable env, we should move it to the tail
     * of 'env_sched_list' before picking up another env from its head, or we
     * will schedule the head env repeatedly.)
     *
     * Otherwise, we simply schedule 'e' again.
     *
     * You may want to use macros below:
     *   'TAILQ_FIRST', 'TAILQ_REMOVE', 'TAILQ_INSERT_TAIL'
     */
    /* Exercise 3.12: Your code here. */

    // 需要发生进程切换的情况
    // 1. yield == 1
    // 2. 当前进程时间片已经用完：count == 0
    // 3. 第一次进程调度：e == NULL
    // 4. 当前进程不再是`RUNNABLE`状态
    if ((yield == 1) || (count == 0) || (e == NULL) ||
        (e->env_status != ENV_RUNNABLE)) {

        // 若切换前的进程仍是RUNNABLE状态，将其从队列头部移除，并插入队列尾部
        // 根据短路逻辑，读取`e->env_status`时，e 一定不为 NULL
        // 这正确处理了yield == 1，但退让的进程是唯一一个进程的情况
        // 此时队列头部仍是该进程，该进程继续运行
        if ((e != NULL) && (e->env_status == ENV_RUNNABLE)) {
            TAILQ_REMOVE(&env_sched_list, e, env_sched_link);
            TAILQ_INSERT_TAIL(&env_sched_list, e, env_sched_link);
        }

        struct Env *nextenv = TAILQ_FIRST(&env_sched_list);

        if (nextenv == NULL) {
            panic("`schedule` called while env_sched_list is empty");
        }

        count = (int)nextenv->env_pri;

        count--;

        env_run(nextenv);
    } else {
        count--;

        env_run(curenv);
    }
}
