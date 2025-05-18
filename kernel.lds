/*
 * Set the architecture to mips.
 */
OUTPUT_ARCH(riscv)

/*
 * Set the ENTRY point of the program to _start.
 */
ENTRY(_start)

BASE_ADDR = 0x80200000;
KERNEL_END_ADDR = 0x80400000;

SECTIONS {
	
	. = BASE_ADDR;

	.text : {
        PROVIDE(stext = .);
        *(.text.boot)
        *(.text)
        PROVIDE(etext = .);
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
    . = KERNEL_END_ADDR;
}
