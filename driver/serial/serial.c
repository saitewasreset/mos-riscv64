#include "regs.h"
#include <device.h>
#include <lib.h>
#include <serial.h>
#include <user_interrupt.h>

static u_reg_t base_addr = 0;

void parse_lsr_register(uint8_t lsr);
void parse_lcr_register(uint8_t lcr);
void parse_iir_register(uint8_t iir);
void parse_msr_register(uint8_t msr);
void parse_ier_register(uint8_t ier);
void enable_serial_interrupt();
void disable_serial_interrupt();

static void handle_interrupt(void) {
    disable_serial_interrupt();

    debugf("serial: handle interrupt called\n");
}

int main(void) {
    debugf("serial: init serial\n");

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

    enable_serial_interrupt();

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

    debugf("serial: WE SHALL NEVER SURRENDER!\n");

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

void enable_serial_interrupt() {
    uint8_t ier = 0x0F;

    int ret = 0;

    if ((ret = syscall_write_dev((u_reg_t)&ier, base_addr + IER_DLM_OFFSET,
                                 1)) < 0) {
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