flash: firmware.bin
	st-flash --reset write firmware.bin 0x08000000
	touch flash

firmware.bin: firmware.elf
	arm-none-eabi-objcopy -O binary firmware.elf firmware.bin

firmware.elf: main.c
	arm-none-eabi-gcc -mcpu=cortex-m4 main.c -nostdlib -T link.ld -o firmware.elf



