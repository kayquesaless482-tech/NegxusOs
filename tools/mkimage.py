#!/usr/bin/env python3
"""NegxusOS - monta a imagem de disco bootavel (MBR + estagio 2 + kernel/initrd)."""
import subprocess, struct, sys, os
S2 = 16
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
kernel = open("build/kernel.bin", "rb").read()
sectors = (len(kernel) + 511) // 512
kernel += b"\0" * (sectors * 512 - len(kernel))
subprocess.check_call(["nasm", "-f", "bin", "boot/stage2.asm", "-DPAYLOAD_LBA=%d" % (1 + S2),
                       "-DPAYLOAD_SECTORS=%d" % sectors, "-o", "build/stage2.bin"])
subprocess.check_call(["nasm", "-f", "bin", "boot/mbr.asm", "-DSTAGE2_SECTORS=%d" % S2, "-o", "build/mbr.bin"])
mbr = bytearray(open("build/mbr.bin", "rb").read())
stage2 = open("build/stage2.bin", "rb").read()
assert len(mbr) == 512 and len(stage2) == S2 * 512, "tamanho dos estagios invalido"
total = 1 + S2 + sectors
total_pad = (total + 2047) // 2048 * 2048          # alinha em 1 MiB
mbr[446:462] = struct.pack("<B3sB3sII", 0x80, b"\xfe\xff\xff", 0xDA, b"\xfe\xff\xff", 1, total_pad - 1)
img = bytes(mbr) + stage2 + kernel
img += b"\0" * (total_pad * 512 - len(img))
open("build/negxusos.img", "wb").write(img)
print("negxusos.img: %d KiB (kernel+initrd: %d KiB)" % (len(img) // 1024, len(kernel) // 1024))
