#include <mmu.h>
#include <printk.h>
#include <sbi.h>

/* Lab 1 Key Code "printcharc" */

/*
 * 概述：
 *   停止/重置整个系统。通过SBI System Reset Extension，请求关闭系统。
 *
 * Precondition：
 *   - 依赖SBI System Reset Extension拓展
 *
 * Postcondition：
 *   - 成功：系统复位/模拟器停止
 *   - 失败：打印警告信息并进入死循环
 *
 * 副作用：
 *   - 可能输出警告信息到控制台
 *
 * 关键点：
 *   - 失败处理：通过死循环确保系统不再继续执行
 */
void halt(void) {
    sbi_system_reset(RESET_TYPE_SHUTDOWN, 0);
    printk("machine.c:\thalt is not supported in this machine!\n");
    while (1) {
    }
}

/*
 * 概述：
 *   重启系统。通过SBI System Reset Extension，请求重启系统。
 *
 * Precondition：
 *   - 依赖SBI System Reset Extension拓展
 *
 * Postcondition：
 *   - 成功：系统重启
 *   - 失败：打印警告信息并进入死循环
 *
 * 副作用：
 *   - 可能输出警告信息到控制台
 *
 * 关键点：
 *   - 失败处理：通过死循环确保系统不再继续执行
 */
void reboot(void) {
    sbi_system_reset(RESET_TYPE_WARM_REBOOT, 0);
    printk("machine.c:\treboot is not supported in this machine!\n");
    while (1) {
    }
}