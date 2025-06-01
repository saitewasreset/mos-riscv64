#ifndef _IO_H_
#define _IO_H_

#include <pmap.h>

/*
 * 概述：
 *   从MMIO地址paddr读取8位数据（使用MMIO映射地址）
 *
 * Precondition：
 *   - paddr必须是合法MMIO设备物理地址（由调用者保证）
 *
 * Postcondition：
 *   - 返回指定物理地址对应的设备寄存器值
 *
 * 关键点：
 *   - volatile确保每次访问都直接从设备读取
 */
static inline uint8_t ioread8(u_reg_t paddr) {
    return *(volatile uint8_t *)(paddr + MMIO_VA_OFFSET);
}

/*
 * 概述：
 *   从MMIO地址paddr读取16位数据（使用MMIO映射地址）
 *
 * 其他说明同ioread8
 */
static inline uint16_t ioread16(u_reg_t paddr) {
    return *(volatile uint16_t *)(paddr + MMIO_VA_OFFSET);
}

/*
 * 概述：
 *   从MMIO地址paddr读取32位数据（使用MMIO映射地址）
 *
 * 其他说明同ioread8
 */
static inline uint32_t ioread32(u_reg_t paddr) {
    return *(volatile uint32_t *)(paddr + MMIO_VA_OFFSET);
}

/*
 * 概述：
 *   从MMIO地址paddr读取64位数据（使用MMIO映射地址）
 *
 * 其他说明同ioread8
 */
static inline uint64_t ioread64(u_reg_t paddr) {
    return *(volatile uint64_t *)(paddr + MMIO_VA_OFFSET);
}

/*
 * 概述：
 *   向MMIO地址paddr写入8位数据（使用MMIO映射地址）
 *
 * Precondition：
 *   - paddr必须是合法MMIO设备物理地址（由调用者保证）
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
 */
static inline void iowrite8(uint8_t data, u_reg_t paddr) {
    *(volatile uint8_t *)(paddr + MMIO_VA_OFFSET) = data;
}

/*
 * 概述：
 *   向MMIO地址paddr写入16位数据（使用MMIO映射地址）
 *
 *   注意，被映射的到设备的地址和映射到物理内存的地址效果不同
 *   对于物理内存，向字节地址`pa`写入16位数据将影响字节地址`pa + 1`的值
 *   但对于映射的到设备的地址，
 *   向字节地址`pa`写入16位数据**不影响**字节地址`pa + 1`的值
 *   `pa`和`pa + 1`可映射到不同的设备寄存器，且设备寄存器位宽都大于8位
 *
 * 其他说明同iowrite8
 */
static inline void iowrite16(uint16_t data, u_reg_t paddr) {
    *(volatile uint16_t *)(paddr + MMIO_VA_OFFSET) = data;
}

/*
 * 概述：
 *   向MMIO地址paddr写入32位数据（使用MMIO映射地址）
 *
 *   注意，被映射的到设备的地址和映射到物理内存的地址效果不同
 *   对于物理内存，向字节地址`pa`写入32位数据将影响字节地址`pa + 1`的值
 *   但对于映射的到设备的地址，
 *   向字节地址`pa`写入32位数据**不影响**字节地址`pa + 1`的值
 *   `pa`和`pa + 1`可映射到不同的设备寄存器，且设备寄存器位宽都大于8位
 *
 * 其他说明同iowrite8
 */
static inline void iowrite32(uint32_t data, u_reg_t paddr) {
    *(volatile uint32_t *)(paddr + MMIO_VA_OFFSET) = data;
}

/*
 * 概述：
 *   向MMIO地址paddr写入64位数据（使用MMIO映射地址）
 *
 * 其他说明同iowrite8
 */
static inline void iowrite64(uint64_t data, u_reg_t paddr) {
    *(volatile uint64_t *)(paddr + MMIO_VA_OFFSET) = data;
}

#endif
