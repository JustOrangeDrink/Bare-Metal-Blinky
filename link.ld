ENTRY(_reset);

MEMORY {
	flash(rx)	: ORIGIN = 0x08000000, LENGTH = 512K
	sram(rwx)	: ORIGIN = 0x20000000, LENGTH = 96K
}

_estack = ORIGIN(sram) + LENGTH(sram);

SECTIONS {
	.vectors	: {KEEP(*(.vectors))}	> flash
	.text		: {*(.text*)}			> flash
	.rodata		: {*(.rodata*)}			> flash

	.data : {
		_sdata = .;
		*(.first_data*)
		*(.data SORT(.data.*))
		_edata = .;
	} > sram AT > flash
	_sidata = LOADADDR(.data);
	
	.bss : {
		_sbss = .;
		*(.bss SORT(.bss.*))
		_ebss = .;
	}

	. = ALIGN(8);
	_end = .;
}
