#include <kmalloc.h>
#include <pmap.h>

uint64_t address[1000];
uint64_t end[1000];
int pos = 0;

void out_of_range() { printk("Invalid address: out of range\n"); }

void overlap() { printk("Invalid address: address overlap\n"); }

void should_null() { printk("Invalid alloc: address should be NULL\n"); }

void not_null() { printk("Invalid alloc: address should not be NULL\n"); }

void not_aligned() { printk("Invalid address: not aligned to 8\n"); }

char check_null(void *p) {
    if (p != NULL) {
        should_null();
        return 0;
    }
    return 1;
}

char check(uint64_t a, uint64_t b) {

    if (a == 0) {
        not_null();
        return 0;
    }

    if (a < (uint64_t)KMALLOC_BEGIN_VA || b > (uint64_t)(KMALLOC_END_VA)) {
        out_of_range();
        return 0;
    }

    if ((a & 7) != 0) {
        not_aligned();
        return 0;
    }

    for (int i = 0; i < pos; i++) {
        if ((a >= address[i] && a <= end[i]) ||
            (b >= address[i] && b <= end[i])) {
            overlap();
            return 0;
        }
    }

    address[pos] = a;
    end[pos++] = b;

    return 1;
}

void rem(uint64_t a, uint64_t b) {
    for (int i = 0; i < pos; i++) {
        if (address[i] == a && end[i] == b) {
            address[i] = 0;
            end[i] = 0;
        }
    }
}

void malloc_test() {
    void *p1 = kmalloc(0x100000);
    assert(check((uint64_t)p1, (uint64_t)p1 + 0x100000));

    void *p2 = kmalloc(0x100000);
    assert(check((uint64_t)p2, (uint64_t)p2 + 0x100000));

    void *p3 = kmalloc(0x100000);
    assert(check((uint64_t)p3, (uint64_t)p3 + 0x100000));

    void *p4 = kmalloc(0x40000000);
    assert(check_null(p4));

    void *p5 = kmalloc(100);
    assert(check((uint64_t)p5, (uint64_t)p5 + 100));

    printk("malloc_test() is done\n");
}

void riscv64_init(u_reg_t hart_id, void *dtb_address) {
    printk("init.c:\triscv64_init() is called\n");

    exception_init();

    riscv64_detect_memory();
    riscv64_vm_init();
    page_init();

    kmalloc_init();

    malloc_test();

    printk("My life for Super Earth!\n");

    halt();
}