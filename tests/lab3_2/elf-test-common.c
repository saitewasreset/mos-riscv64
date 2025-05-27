void load_icode_check(void);

void riscv64_init(u_reg_t hart_id, void *dtb_address) {
    printk("init.c:\triscv64_init() is called\n");

    exception_init();

    riscv64_detect_memory();
    riscv64_vm_init();
    page_init();

    env_init();

    load_icode_check();

    halt();
}

void mem_eq(const char *a, const char *b, u_long size) {
    for (--a, --b; size--;) {
        if (*++a != *++b) {
            panic("mismatch: %x at %x, %x at %x\n", *(unsigned char *)a, a,
                  *(unsigned char *)b, b);
        }
    }
}

void mem_eqz(const char *a, u_long size) {
    for (--a; size--;) {
        if (*++a) {
            panic("nonzero: %x at %x\n", *(unsigned char *)a, a);
        }
    }
}

void seg_check(Pte *pgdir, u_long va, const char *std, u_long size) {
    printk("segment check: %x - %x (%d)\n", va, va + size, size);
    Pte *pte;
    u_long off = va - ROUNDDOWN(va, PAGE_SIZE), i;
    if (off) {
        u_long n = MIN(size, PAGE_SIZE - off);
        assert(page_lookup(pgdir, va - off, &pte));
        if (std) {
            mem_eq((char *)P2KADDR(PTE_ADDR(*pte)) + off, std, n);
            std += n;
        } else {
            mem_eqz((char *)P2KADDR(PTE_ADDR(*pte)) + off, n);
        }
        va += n;
        size -= n;
    }

    for (i = 0; i < size; i += PAGE_SIZE) {
        u_long n = MIN(size - i, PAGE_SIZE);
        assert(page_lookup(pgdir, va + i, &pte));
        if (std) {
            mem_eq((char *)P2KADDR(PTE_ADDR(*pte)), std + i, n);
        } else {
            mem_eqz((char *)P2KADDR(PTE_ADDR(*pte)), n);
        }
    }
}
