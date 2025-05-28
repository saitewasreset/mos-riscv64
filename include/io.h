#ifndef _IO_H_
#define _IO_H_

#include <pmap.h>

/*
 * 概述：
 *   从物理地址paddr读取8位数据（使用KSEG1段地址）
 *
 * Precondition：
 *   - paddr必须是合法设备物理地址（由调用者保证）
 *
 * Postcondition：
 *   - 返回指定物理地址对应的设备寄存器值
 *
 * 关键点：
 *   - volatile确保每次访问都直接从设备读取
 *   - KSEG1转换绕过MMU和缓存
 */
static inline uint8_t ioread8(u_long paddr) {
    return 0;
    // return *(volatile uint8_t *)(paddr | KSEG1);
}

/*
 * 概述：
 *   从物理地址paddr读取16位数据（使用KSEG1段地址）
 *
 * 其他说明同ioread8
 */
static inline uint16_t ioread16(u_long paddr) {
    return 0;
    // return *(volatile uint16_t *)(paddr | KSEG1);
}

/*
 * 概述：
 *   从物理地址paddr读取32位数据（使用KSEG1段地址）
 *
 * 其他说明同ioread8
 */
static inline uint32_t ioread32(u_long paddr) {
    return 0;
    // return *(volatile uint32_t *)(paddr | KSEG1);
}

/*
 * 概述：
 *   向物理地址paddr写入8位数据（使用KSEG1段地址）
 *
 * Precondition：
 *   - paddr必须是合法设备物理地址（由调用者保证）
 *
 * Postcondition：
 *   - 数据被写入指定物理地址对应的设备寄存器
 *
 * 副作用：
 *   - 直接修改设备寄存器状态
 *   - 可能触发硬件操作（如串口输出）
 *
 * 关键点：
 *   - 使用volatile防止编译器优化
 *   - KSEG1转换实现物理地址到内核虚拟地址的映射
 */
static inline void iowrite8(uint8_t data, u_long paddr) {
    //*(volatile uint8_t *)(paddr | KSEG1) = data;
}

/*
 * 概述：
 *   向物理地址paddr写入16位数据（使用KSEG1段地址）
 *
 *   注意，被映射的到设备的地址和映射到物理内存的地址效果不同
 *   对于物理内存，向字节地址`pa`写入16位数据将影响字节地址`pa + 1`的值
 *   但对于映射的到设备的地址，
 *   向字节地址`pa`写入16位数据**不影响**字节地址`pa + 1`的值
 *   `pa`和`pa + 1`可映射到不同的设备寄存器，且设备寄存器位宽都大于8位
 *
 * 其他说明同iowrite8
 */
static inline void iowrite16(uint16_t data, u_long paddr) {
    //*(volatile uint16_t *)(paddr | KSEG1) = data;
}

/*
 * 概述：
 *   向物理地址paddr写入32位数据（使用KSEG1段地址）
 *
 *   注意，被映射的到设备的地址和映射到物理内存的地址效果不同
 *   对于物理内存，向字节地址`pa`写入32位数据将影响字节地址`pa + 1`的值
 *   但对于映射的到设备的地址，
 *   向字节地址`pa`写入32位数据**不影响**字节地址`pa + 1`的值
 *   `pa`和`pa + 1`可映射到不同的设备寄存器，且设备寄存器位宽都大于8位
 *
 * 其他说明同iowrite8
 */
static inline void iowrite32(uint32_t data, u_long paddr) {
    //*(volatile uint32_t *)(paddr | KSEG1) = data;
}

#endif
