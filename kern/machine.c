#include <malta.h>
#include <mmu.h>
#include <printk.h>

/* Lab 1 Key Code "printcharc" */

/*
 * 概述：
 *   停止/重置整个系统。向Malta板的FPGA的SOFTRES寄存器写入魔数0x42，
 *   触发板级复位。在QEMU模拟器中（使用'-no-reboot'参数时）会停止模拟。
 *
 * Precondition：
 *   - 依赖全局硬件状态：MALTA_FPGA_HALT寄存器
 *
 * Postcondition：
 *   - 成功：系统复位/模拟器停止
 *   - 失败：打印警告信息并进入死循环
 *
 * 副作用：
 *   - 修改全局硬件状态：MALTA_FPGA_HALT寄存器
 *   - 可能输出警告信息到控制台
 *
 * 关键点：
 *   - 0x42是特定的复位魔数
 *   - 失败处理：通过死循环确保系统不再继续执行
 */
void halt(void) {
    *(volatile uint8_t *)(KSEG1 + MALTA_FPGA_HALT) = 0x42;
    printk("machine.c:\thalt is not supported in this machine!\n");
    while (1) {
    }
}
