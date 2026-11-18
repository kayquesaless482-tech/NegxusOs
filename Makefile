# NegxusOS - sistema de build
CC      := gcc
LD      := ld
NASM    := nasm
B       := build
KFLAGS  := -std=gnu11 -O0 -ffreestanding -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only \
           -fno-stack-protector -fno-asynchronous-unwind-tables -fwrapv -fno-strict-aliasing \
           -finput-charset=UTF-8 -fexec-charset=CP1252 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-address-of-packed-member
KSRC    := $(wildcard kernel/*.c) common/str.c
KOBJ    := $(patsubst %.c,$(B)/%.o,$(KSRC)) $(B)/kernel/entry.o $(B)/kernel/isr.o $(B)/kernel/initrd.o
APPS    := $(notdir $(basename $(wildcard apps/*.c)))

all: $(B)/negxusos.img

$(B)/%.o: %.c kernel/kernel.h sdk/nxe.h
	@mkdir -p $(dir $@)
	$(CC) $(KFLAGS) -c $< -o $@
$(B)/kernel/entry.o: kernel/entry.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@
$(B)/kernel/isr.o: kernel/isr.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@
$(B)/kernel/initrd.o: kernel/initrd.S $(B)/initrd.bin
	$(CC) -c kernel/initrd.S -o $@

$(B)/kernel.elf: $(KOBJ) kernel/linker.ld
	$(LD) -nostdlib -z max-page-size=4096 -T kernel/linker.ld -o $@ $(KOBJ)
$(B)/kernel.bin: $(B)/kernel.elf
	objcopy -O binary $< $@

$(B)/initrd.bin: $(wildcard apps/*.c) $(wildcard sdk/*) common/str.c tools/mkinitrd.py
	python3 tools/mkinitrd.py $@

$(B)/negxusos.img: $(B)/kernel.bin boot/mbr.asm boot/stage2.asm tools/mkimage.py
	python3 tools/mkimage.py

run: all
	qemu-system-x86_64 -m 512 -drive format=raw,file=$(B)/negxusos.img,if=ide,index=0 -nic user,model=e1000 -serial stdio

clean:
	rm -rf $(B)
.PHONY: all run clean
