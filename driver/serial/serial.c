#include "char_queue.h"
#include "error.h"
#include "mmu.h"
#include "regs.h"
#include <device.h>
#include <lib.h>
#include <serial.h>
#include <serialreq.h>
#include <user_interrupt.h>

#define REQVA 0x60000000

static struct CharQueue tx_queue;
static struct CharQueue rx_queue;

static u_reg_t base_addr = 0;

void parse_lsr_register(uint8_t lsr);
void parse_lcr_register(uint8_t lcr);
void parse_iir_register(uint8_t iir);
void parse_msr_register(uint8_t msr);
void parse_ier_register(uint8_t ier);
void enable_serial_interrupt(uint8_t interrupt_mask);
void disable_serial_interrupt();

void enable_specific_interrupt(uint8_t interrupt_flag);
void disable_specific_interrupt(uint8_t interrupt_flag);

static size_t serial_read(char *buf, size_t len);
static void serial_write(const char *buf, size_t len);

static void serve_read(uint32_t whom, struct SerialReqPayload *payload);
static void serve_write(uint32_t whom, struct SerialReqPayload *payload);

static void *serve_table[MAX_SERIALREQNO] = {
    [SERIALREQ_READ] = serve_read, [SERIALREQ_WRITE] = serve_write};

// Modem 状态改变
static void handle_modem_status(void);
// 发送保存寄存器为空
static void handle_transmitter_holding_register_empty(void);
// 接收到有效数据：接收 FIFO 字符个数达到触发阈值
static void handle_received_data(void);
// 接收线路状态改变
static void handle_line_status_interrupt(void);

static void handle_interrupt(void) {

    while (1) {
        uint8_t iir = 0;
        int ret =
            syscall_read_dev((u_reg_t)&iir, base_addr + IIR_FCR_OFFSET, 1);

        if (ret < 0) {
            debugf("serial: handle_interrupt: cannot read serial IIR register: "
                   "%d\n",
                   ret);
            break; // 读取失败，退出循环
        }

        // INTp 位为 1 表示没有中断挂起，可以退出循环
        if ((iir & IIR_INTP_MASK) == IIR_INTP_NO_INTERRUPT_PENDING) {
            break;
        }

        // 从 IIR 寄存器中提取中断ID
        uint8_t interrupt_code = (iir & IIR_IID_MASK) >> IIR_IID_OFFSET;

        switch (interrupt_code) {
        case IIR_IID_MODEM_STATUS:
            handle_modem_status();
            break;
        case IIR_IID_TRANSMITTER_HOLDING_REGISTER_EMPTY:
            handle_transmitter_holding_register_empty();
            break;
        case IIR_IID_RECEIVED_DATA_AVAILABLE:
            handle_received_data();
            break;
        case IIR_IID_LINE_STATUS:
            handle_line_status_interrupt();
            break;
        case IIR_IID_CHARACTER_TIMEOUT:
            // 超时和接收到数据通常是相同的处理逻辑
            handle_received_data();
            break;
        default:
            debugf("serial: invalid interrupt_code: %d\n", interrupt_code);
        }
    }
}

int main(void) {
    debugf("serial: init serial\n");

    queue_init(&tx_queue);
    queue_init(&rx_queue);

    struct UserDevice serial_device = {0};
    struct SerialDeviceData serial_device_data = {0};

    int ret = syscall_get_device("serial", 0, sizeof(struct SerialDeviceData),
                                 (u_reg_t)&serial_device,
                                 (u_reg_t)&serial_device_data);

    if (ret < 0) {
        debugf("serial: cannot get serial device: %d\n", ret);
    }

    base_addr = serial_device_data.begin_pa;

    debugf("serial: serial base pa: 0x%016lx\n", base_addr);

    uint8_t lsr = 0;

    ret = syscall_read_dev((u_reg_t)&lsr, base_addr + LSR, 1);

    if (ret < 0) {
        debugf("serial: cannot read serial LSR register: %d\n", ret);
    }

    parse_lsr_register(lsr);

    uint8_t lcr = 0;

    ret = syscall_read_dev((u_reg_t)&lcr, base_addr + LCR, 1);

    if (ret < 0) {
        debugf("serial: cannot read serial LCR register: %d\n", ret);
    }

    parse_lcr_register(lcr);

    uint8_t iir = 0;

    ret = syscall_read_dev((u_reg_t)&iir, base_addr + IIR_FCR_OFFSET, 1);

    if (ret < 0) {
        debugf("serial: cannot read serial IIR register: %d\n", ret);
    }

    parse_iir_register(iir);

    uint8_t msr = 0;

    ret = syscall_read_dev((u_reg_t)&msr, base_addr + MSR, 1);

    if (ret < 0) {
        debugf("serial: cannot read serial MSR register: %d\n", ret);
    }

    parse_msr_register(msr);

    uint8_t ier = 0;

    ret = syscall_read_dev((u_reg_t)&ier, base_addr + IER_DLM_OFFSET, 1);

    if (ret < 0) {
        debugf("serial: cannot read serial IER register: %d\n", ret);
    }

    parse_ier_register(ier);

    register_user_interrupt_handler(serial_device_data.interrupt_id,
                                    handle_interrupt);

    ret = syscall_read_dev((u_reg_t)&iir, base_addr + IIR_FCR_OFFSET, 1);

    if (ret < 0) {
        debugf("serial: cannot read serial IIR register: %d\n", ret);
    }

    parse_iir_register(iir);

    debugf("serial: enable interrupt\n");

    enable_serial_interrupt(IER_ALL);

    ret = syscall_read_dev((u_reg_t)&ier, base_addr + IER_DLM_OFFSET, 1);

    if (ret < 0) {
        debugf("serial: cannot read serial IER register: %d\n", ret);
    }

    parse_ier_register(ier);

    ret = syscall_read_dev((u_reg_t)&iir, base_addr + IIR_FCR_OFFSET, 1);

    if (ret < 0) {
        debugf("serial: cannot read serial IIR register: %d\n", ret);
    }

    parse_iir_register(iir);

    debugf("serial: serial init done\n");

    uint32_t whom = 0;
    uint64_t val = 0;
    uint32_t perm = 0;

    void (*func)(uint32_t whom, struct SerialReqPayload *payload) = NULL;

    while (1) {
        int ret = ipc_recv(&whom, &val, (void *)REQVA, &perm);

        if (ret != 0) {
            if (ret == -E_INTR) {
                continue;
            } else {
                debugf("serial: failed to receive request: %d\n", ret);
            }
        }

        if (val >= MAX_SERIALREQNO) {
            debugf("serial: invalid request code %lu from %08x\n", val, whom);

            ipc_send(whom, (uint64_t)-SERIALREQ_NO_FUNC, NULL, 0);

            panic_on(syscall_mem_unmap(0, (void *)REQVA));

            continue;
        }

        if (!(perm & PTE_V)) {
            debugf("serial: invalid request from %08x: no argument page\n",
                   whom);
            ipc_send(whom, (uint64_t)-SERIALREQ_NO_PAYLOAD, NULL, 0);
            continue;
        }

        func = serve_table[val];

        func(whom, (struct SerialReqPayload *)REQVA);
        panic_on(syscall_mem_unmap(0, (void *)REQVA));
    }

    return 0;
}

void parse_lsr_register(uint8_t lsr) {
    debugf(
        "serial: LSR: ERROR:%d TE:%d THRE:%d BI:%d FE:%d PE:%d OE:%d DR:%d\n",
        (lsr >> 7) & 0x1, (lsr >> 6) & 0x1, (lsr >> 5) & 0x1, (lsr >> 4) & 0x1,
        (lsr >> 3) & 0x1, (lsr >> 2) & 0x1, (lsr >> 1) & 0x1, (lsr >> 0) & 0x1);
}

void parse_lcr_register(uint8_t lcr) {
    debugf("serial: LCR: dlab:%d bcb:%d spb:%d eps:%d pe:%d sb:%d bec:%d%d\n",
           (lcr >> 7) & 0x1,  // dlab
           (lcr >> 6) & 0x1,  // bcb
           (lcr >> 5) & 0x1,  // spb
           (lcr >> 4) & 0x1,  // eps
           (lcr >> 3) & 0x1,  // pe
           (lcr >> 2) & 0x1,  // sb
           (lcr >> 1) & 0x1,  // bec[1]
           (lcr >> 0) & 0x1); // bec[0]
}

void parse_iir_register(uint8_t iir) {
    debugf("serial: IIR: FEFLAG:%d%d IID:%d%d%d INTp:%d\n",
           (iir >> 7) & 0x1,  // FEFLAG[1]
           (iir >> 6) & 0x1,  // FEFLAG[0]
           (iir >> 3) & 0x1,  // IID[2]
           (iir >> 2) & 0x1,  // IID[1]
           (iir >> 1) & 0x1,  // IID[0]
           (iir >> 0) & 0x1); // INTp
}

void parse_msr_register(uint8_t msr) {
    debugf("serial: MSR: Carrier detect:%d Ring indicator:%d Data set ready:%d "
           "Clear to send:%d\n",
           (msr >> 7) & 0x1,  // Carrier detect
           (msr >> 6) & 0x1,  // Ring indicator
           (msr >> 5) & 0x1,  // Data set ready
           (msr >> 4) & 0x1); // Clear to send
}

void parse_ier_register(uint8_t ier) {
    debugf("serial: IER: EDSSI:%d ELSI:%d ETBEI:%d ERBFI:%d\n",
           (ier >> 3) & 0x1,  // EDSSI
           (ier >> 2) & 0x1,  // ELSI
           (ier >> 1) & 0x1,  // ETBEI
           (ier >> 0) & 0x1); // ERBFI
}

void enable_serial_interrupt(uint8_t interrupt_mask) {
    int ret = 0;

    if ((ret = syscall_write_dev((u_reg_t)&interrupt_mask,
                                 base_addr + IER_DLM_OFFSET, 1)) < 0) {
        debugf("serial: cannot write serial IER register: %d\n", ret);
    }
}

void disable_serial_interrupt() {
    uint8_t ier = 0x00;

    int ret = 0;

    if ((ret = syscall_write_dev((u_reg_t)&ier, base_addr + IER_DLM_OFFSET,
                                 1)) < 0) {
        debugf("serial: cannot write serial IER register: %d\n", ret);
    }
}

void enable_specific_interrupt(uint8_t interrupt_flag) {
    debugf("enable_specific_interrupt called\n");
    uint8_t mask = 0;
    int ret = 0;

    if ((ret = syscall_read_dev((u_reg_t)&mask, base_addr + IER_DLM_OFFSET,
                                1)) < 0) {
        debugf("serial: enable_specific_interrupt: cannot read serial IER "
               "register: %d\n",
               ret);
    }

    mask |= interrupt_flag;

    enable_serial_interrupt(mask);
}

void disable_specific_interrupt(uint8_t interrupt_flag) {
    uint8_t mask = 0;
    int ret = 0;

    if ((ret = syscall_read_dev((u_reg_t)&mask, base_addr + IER_DLM_OFFSET,
                                1)) < 0) {
        debugf("serial: disable_specific_interrupt: cannot read serial IER "
               "register: %d\n",
               ret);
    }

    mask &= (~interrupt_flag);

    enable_serial_interrupt(mask);
}

// Modem 状态改变
static void handle_modem_status(void) {
    uint8_t msr = 0;

    int ret = syscall_read_dev((u_reg_t)&msr, base_addr + MSR, 1);

    if (ret < 0) {
        debugf("serial: handle_modem_status: cannot read serial MSR register: "
               "%d\n",
               ret);
    }

    debugf("serial: modem status changed: \n");

    parse_msr_register(msr);
}
// 发送保存寄存器为空
static void handle_transmitter_holding_register_empty(void) {

    if (is_empty(&tx_queue)) {
        disable_specific_interrupt(IER_ETBEI);
    } else {
        uint8_t lsr = 0;

        while (1) {
            if (is_empty(&tx_queue)) {
                break;
            }

            syscall_read_dev((u_reg_t)&lsr, base_addr + LSR, 1);

            // 发送缓冲区已空
            if ((lsr & LSR_THRE) == 0) {
                break;
            }

            // 发送缓冲区未空，且仍可发送数据
            char data = dequeue(&tx_queue);

            syscall_write_dev((u_reg_t)&data, base_addr + RBR_THR_DLL_OFFSET,
                              1);
        }

        if (is_empty(&tx_queue)) {
            disable_specific_interrupt(IER_ETBEI);
        } else {
            // 仍有待发送数据，等待下一次发送
        }
    }
}

// 接收到有效数据：接收 FIFO 字符个数达到触发阈值
static void handle_received_data(void) {
    uint8_t lsr = 0;
    int ret = 0;

    while (1) {
        ret = syscall_read_dev((u_reg_t)&lsr, base_addr + LSR, 1);

        if (ret < 0) {
            debugf("serial: handle_received_data: cannot read serial LSR "
                   "register: %d\n",
                   ret);
        }

        // DR位为0，表示没有更多数据
        if ((lsr & LSR_DR) == 0) {
            break;
        }

        uint8_t data;
        ret =
            syscall_read_dev((u_reg_t)&data, base_addr + RBR_THR_DLL_OFFSET, 1);
        if (ret < 0) {
            debugf("serial: drain_fifo: cannot read serial RBR register: %d\n",
                   ret);
            break;
        }

        // 若接收队列未满，接收数据
        // 否则，忽略收到的数据
        if (!is_full(&rx_queue)) {
            enqueue(&rx_queue, (char)data);
        }
    }
}
// 接收线路状态改变
static void handle_line_status_interrupt(void) {
    uint8_t lsr = 0;

    int ret = syscall_read_dev((u_reg_t)&lsr, base_addr + LSR, 1);

    if (ret < 0) {
        debugf("serial: handle_line_status_interrupt: cannot read serial LSR "
               "register: %d\n",
               ret);
    }

    debugf("serial: line status changed: \n");

    parse_lsr_register(lsr);
}

static void serial_write(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i += 1) {
        while (is_full(&tx_queue)) {
            enable_specific_interrupt(IER_ETBEI);
        }

        enqueue(&tx_queue, buf[i]);
    }

    enable_specific_interrupt(IER_ETBEI);
}

static size_t serial_read(char *buf, size_t len) {
    size_t actual = 0;

    while (!is_empty(&rx_queue)) {
        if (actual >= len) {
            break;
        }

        char data = dequeue(&rx_queue);

        buf[actual] = data;
        actual++;
    }

    return actual;
}

static void serve_read(uint32_t whom, struct SerialReqPayload *payload) {
    if (payload->max_len > MAX_PAYLOAD_SIZE) {
        ipc_send(whom, (uint64_t)-SERIALREQ_INVAL, NULL, 0);
        return;
    }

    size_t actual_read = serial_read((char *)payload, payload->max_len);

    ipc_send(whom, actual_read, (char *)REQVA, PTE_V | PTE_RW | PTE_USER);
}

static void serve_write(uint32_t whom, struct SerialReqPayload *payload) {
    if (payload->max_len > MAX_PAYLOAD_SIZE) {
        ipc_send(whom, (uint64_t)-SERIALREQ_INVAL, NULL, 0);
        return;
    }

    serial_write(payload->buf, payload->max_len);

    ipc_send(whom, SERIALREQ_SUCCESS, NULL, 0);
}