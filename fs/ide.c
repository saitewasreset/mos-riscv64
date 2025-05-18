/*
 * operations on IDE disk.
 */

#include "serv.h"
#include <lib.h>
#include <malta.h>
#include <mmu.h>

/*
 * 概述：
 *   等待IDE设备完成之前的请求并准备好接收后续请求。
 *   通过循环读取设备状态寄存器，检查"忙碌"标志位，当设备就绪时返回状态值。
 *
 * Precondition：
 *   - 设备状态寄存器物理地址MALTA_IDE_STATUS必须合法可用
 *
 * Postcondition：
 *   - 返回时状态寄存器BUSY位（MALTA_IDE_BUSY）必须为0
 *   - 返回值为状态寄存器的完整8位数据
 *
 * 副作用：
 *   - 可能修改设备状态寄存器（读取操作可能清除某些硬件标志位）
 *   - 调用syscall_yield导致进程调度，修改全局调度队列和curenv
 *
 * 关键点：
 *   - 使用syscall_read_dev系统调用确保用户态安全访问MMIO设备寄存器
 *   - 主动通过syscall_yield让出CPU，避免忙等待消耗资源
 *   - panic_on保证系统调用错误时立即终止，隐含设备访问可靠性假设
 */
static uint8_t wait_ide_ready() {
    uint8_t flag;
    while (1) {
        panic_on(syscall_read_dev(&flag, MALTA_IDE_STATUS, 1));
        if ((flag & MALTA_IDE_BUSY) == 0) {
            break;
        }
        syscall_yield();
    }
    return flag;
}

/*
 * 概述：
 *   用户态IDE磁盘读取函数。通过系统调用配置IDE控制器实现多扇区读取，
 *   遵循Intel PIIX4 IDE控制器的编程规范。执行9步操作流程，包含LBA模式设置、
 *   状态检查及4字节对齐数据传输。
 *
 *   读取数据阶段，每次系统调用传递4字节数据。
 *
 * Precondition：
 *   - diskno必须为0或1
 *   - secno的有效性隐含由逻辑块地址结构保证（28位LBA地址）
 *   - 目标缓冲区dst必须已分配且可写入至少nsecs*512字节
 *
 * Postcondition：
 *   - 成功：nsecs个扇区（每个512字节）数据存储到dst，按序递增secno
 *   - 失败：任何系统调用错误立即触发user panic
 *
 * 副作用：
 *   - 修改IDE控制器的6个设备寄存器（NSECT/LBAL/LBAM/LBAH/DEVICE/STATUS）
 *   - 通过MALTA_IDE_DATA进行数据传输，改变设备缓冲区内容
 *   - 可能多次调用syscall_yield改变进程调度状态（`wait_ide_ready`）
 *
 * 关键点：
 *   - 使用位运算分解28位LBA地址（0-27位）
 *   - 每个扇区数据传输分128次，每次4字节（SECT_SIZE/4循环）
 */
// Checked by DeepSeek-R1 20250508 16:58
void ide_read(u_int diskno, u_int secno, void *dst, u_int nsecs) {
    uint8_t temp;
    u_int offset = 0, max = nsecs + secno;
    panic_on(diskno >= 2);

    // Read the sector in turn
    while (secno < max) {
        temp = wait_ide_ready();
        // Step 1: Write the number of operating sectors to NSECT register
        temp = 1;
        panic_on(syscall_write_dev(&temp, MALTA_IDE_NSECT, 1));

        // Step 2: Write the 7:0 bits of sector number to LBAL register
        temp = secno & 0xff;
        panic_on(syscall_write_dev(&temp, MALTA_IDE_LBAL, 1));

        // Step 3: Write the 15:8 bits of sector number to LBAM register
        /* Exercise 5.3: Your code here. (1/9) */
        temp = (secno >> 8) & 0xFF;
        panic_on(syscall_write_dev(&temp, MALTA_IDE_LBAM, 1));

        // Step 4: Write the 23:16 bits of sector number to LBAH register
        /* Exercise 5.3: Your code here. (2/9) */
        temp = (secno >> 16) & 0xFF;
        panic_on(syscall_write_dev(&temp, MALTA_IDE_LBAH, 1));

        // Step 5: Write the 27:24 bits of sector number, addressing mode
        // and diskno to DEVICE register
        temp = ((secno >> 24) & 0x0f) | MALTA_IDE_LBA | (diskno << 4);
        panic_on(syscall_write_dev(&temp, MALTA_IDE_DEVICE, 1));

        // Step 6: Write the working mode to STATUS register
        temp = MALTA_IDE_CMD_PIO_READ;
        panic_on(syscall_write_dev(&temp, MALTA_IDE_STATUS, 1));

        // Step 7: Wait until the IDE is ready
        temp = wait_ide_ready();

        // Step 8: Read the data from device
        for (int i = 0; i < SECT_SIZE / 4; i++) {
            panic_on(syscall_read_dev((void *)((size_t)dst + offset + i * 4),
                                      MALTA_IDE_DATA, 4));
        }

        // Step 9: Check IDE status
        panic_on(syscall_read_dev(&temp, MALTA_IDE_STATUS, 1));

        offset += SECT_SIZE;
        secno += 1;
    }
}

/*
 * 概述：
 *   用户态IDE磁盘写入函数。通过系统调用实现与ide_read对称的写操作流程，
 *   主要区别在于命令码及数据传输方向。
 *
 * Precondition：
 *   - diskno必须为0或1
 *   - secno的有效性隐含由逻辑块地址结构保证（28位LBA地址）
 *   - 源数据缓冲区src必须包含有效数据且可读取至少nsecs*512字节
 *
 * Postcondition：
 *   - 成功：nsecs个扇区数据从src写入磁盘，按序递增secno
 *   - 失败：任何系统调用错误立即触发user panic
 *
 * 副作用：
 *   - 改变磁盘物理存储内容（永久性存储改变）
 *   - 修改IDE控制器的6个设备寄存器（NSECT/LBAL/LBAM/LBAH/DEVICE/STATUS）
 *   - 通过MALTA_IDE_DATA进行数据传输，改变设备缓冲区内容
 *   - 可能多次调用syscall_yield改变进程调度状态（`wait_ide_ready`）
 *
 * 关键点：
 *   - 写命令触发后需严格等待设备就绪，防止数据损坏
 */
// Checked by DeepSeek-R1 20250508 16:58
void ide_write(u_int diskno, u_int secno, void *src, u_int nsecs) {
    uint8_t temp;
    u_int offset = 0, max = nsecs + secno;
    panic_on(diskno >= 2);

    // Write the sector in turn
    while (secno < max) {
        temp = wait_ide_ready();

        temp = 1;
        // Step 1: Write the number of operating sectors to NSECT register

        panic_on(syscall_write_dev(&temp, MALTA_IDE_NSECT, 1));

        // Step 2: Write the 7:0 bits of sector number to LBAL register
        temp = secno & 0xff;
        panic_on(syscall_write_dev(&temp, MALTA_IDE_LBAL, 1));

        // Step 3: Write the 15:8 bits of sector number to LBAM register
        temp = (secno >> 8) & 0xFF;
        panic_on(syscall_write_dev(&temp, MALTA_IDE_LBAM, 1));

        // Step 4: Write the 23:16 bits of sector number to LBAH register
        temp = (secno >> 16) & 0xFF;
        panic_on(syscall_write_dev(&temp, MALTA_IDE_LBAH, 1));

        // Step 5: Write the 27:24 bits of sector number, addressing mode
        // and diskno to DEVICE register
        temp = ((secno >> 24) & 0x0f) | MALTA_IDE_LBA | (diskno << 4);
        panic_on(syscall_write_dev(&temp, MALTA_IDE_DEVICE, 1));

        // Step 6: Write the working mode to STATUS register
        temp = MALTA_IDE_CMD_PIO_WRITE;
        panic_on(syscall_write_dev(&temp, MALTA_IDE_STATUS, 1));

        // Step 7: Wait until the IDE is ready
        temp = wait_ide_ready();

        // Step 8: Write the data to device
        for (int i = 0; i < SECT_SIZE / 4; i++) {
            /* Exercise 5.3: Your code here. (9/9) */
            panic_on(syscall_write_dev((void *)((size_t)src + offset + i * 4),
                                       MALTA_IDE_DATA, 4));
        }

        // Step 9: Check IDE status
        panic_on(syscall_read_dev(&temp, MALTA_IDE_STATUS, 1));

        offset += SECT_SIZE;
        secno += 1;
    }
}
