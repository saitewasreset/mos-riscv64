/*
 * Set the architecture to mips.
 */
OUTPUT_ARCH(riscv)

/*
 * Set the ENTRY point of the program to _start.
 */
ENTRY(_start)

LOAD_ADDR = 0x80200000;
BASE_ADDR = 0xFFFFFFC000200000;
KERNEL_END_ADDR_BEFORE_PAGING = 0x81000000;
KERNEL_END_ADDR = 0xFFFFFFC001000000;

SECTIONS {
	
	. = BASE_ADDR;

    . = ALIGN(4K);

    .text.boot : AT(LOAD_ADDR) {
        *(.text.boot)
    }

	.text : {
        PROVIDE(stext = .);
        *(.text)
        PROVIDE(etext = .);
    }

    .text.exc_gen_entry : {
		*(.text.exc_gen_entry)
	}

	. = ALIGN(4K);

	.data : {
        PROVIDE(sdata = .);
        *(.data .data.*)
        PROVIDE(edata = .);
    }

	.bss : {
        .bss.stack = .;
        PROVIDE(sbss = .);

		bss_start = .;

        *(.bss .bss.*)

		bss_end = .;

        PROVIDE(ebss = .);
    }

    .super_info : {
        _super_info_start = .;

        *(.super_info)

        _super_info_end = .;
    }

    .load_symtab : {
        _symtab_start = .;

        *(.symtab_pesudo_data)
        . += 2 * 1024 * 1024 - (. - _symtab_start);

        _symtab_end = .;
    }

    .load_strtab : {
        _strtab_start = .;

        *(.strtab_pesudo_data)
        . += 2 * 1024 * 1024 - (. - _strtab_start);

        _strtab_end = .;
    }

    . = KERNEL_END_ADDR;
    _kernel_end = .;
}
