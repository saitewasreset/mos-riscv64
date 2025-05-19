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
KERNEL_END_ADDR = 0xFFFFFFC000400000;

SECTIONS {
	
	. = BASE_ADDR;

	.text : AT(LOAD_ADDR) {
        PROVIDE(stext = .);
        *(.text.boot)
        *(.text)
        PROVIDE(etext = .);
    }

	. = ALIGN(4K);

	.data : AT(LOAD_ADDR) {
        PROVIDE(sdata = .);
        *(.data .data.*)
        PROVIDE(edata = .);
    }

	.bss : AT(LOAD_ADDR) {
        .bss.stack = .;
        PROVIDE(sbss = .);

		bss_start = .;

        *(.bss .bss.*)

		bss_end = .;

        PROVIDE(ebss = .);
    }
    . = KERNEL_END_ADDR;
}
