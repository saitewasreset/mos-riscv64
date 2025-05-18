#ifndef _MACHINE_H_
#define _MACHINE_H_

/*
 * 概述：
 *   向控制台发送一个字符。如果发送保持寄存器(THR)当前被占用，
 *   则等待直到寄存器可用。
 *
 * Precondition：
 *   - 'ch'是要发送的字符
 *   - 依赖全局硬件状态：MALTA_SERIAL_LSR寄存器（串行线路状态寄存器）
 *
 * Postcondition：
 *   - 字符被发送到串行数据寄存器
 *   - 若字符是'\n'，会先发送'\r'（回车符）
 *
 * 副作用：
 *   - 修改全局硬件状态：MALTA_SERIAL_DATA寄存器（串行数据寄存器）
 *
 * 关键点：
 *   - 使用volatile指针访问硬件寄存器（防止编译器优化）
 *   - KSEG1用于非缓存的内存映射I/O访问
 *   - '\n'处理：自动添加'\r'实现Windows风格的换行
 */
void printcharc(char ch);
/*
 * 概述：
 *   从控制台读取一个字符。
 *
 * Precondition：
 *   - 依赖全局硬件状态：MALTA_SERIAL_LSR寄存器（串行线路状态寄存器）
 *
 * Postcondition：
 *   - 如果数据就绪：返回读取的字符
 *   - 如果数据未就绪：立即返回0
 *
 * 副作用：
 *   - 读取操作会改变硬件状态（某些架构下读取数据寄存器会清除状态位）
 *
 * 关键点：
 *   - 非阻塞式读取：当没有输入时立即返回0
 *   - 使用volatile指针访问硬件寄存器
 */
int scancharc(void);
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
void halt(void) __attribute__((noreturn));

#endif
