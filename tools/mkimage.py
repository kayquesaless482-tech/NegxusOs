#!/usr/bin/env python3
"""NegxusOS - gera disco inicializavel BIOS com particao FAT16 persistente."""
import os
import struct
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

S2_SECTORS = 16
PAYLOAD_LBA = 1 + S2_SECTORS
FAT16_SECTORS = 128 * 1024       # volume de 64 MiB, reconhecido pelo Windows 7
FAT16_SECTORS_PER_CLUSTER = 4   # cluster de 2 KiB
FAT16_RESERVED = 1
FAT16_COPIES = 2
FAT16_ROOT_ENTRIES = 512
FAT16_SECTORS_PER_FAT = 128

nasm = os.environ.get("NASM", "nasm")
kernel = open("build/kernel.bin", "rb").read()
kernel_sectors = (len(kernel) + 511) // 512
kernel += b"\0" * (kernel_sectors * 512 - len(kernel))

payload_end = PAYLOAD_LBA + kernel_sectors
partition_lba = ((payload_end + 2047) // 2048) * 2048
partition_size = FAT16_SECTORS
total_sectors = partition_lba + partition_size
if total_sectors > 0xFFFFFFFF:
    raise SystemExit("Imagem grande demais para MBR/LBA32")

os.makedirs("build", exist_ok=True)
subprocess.check_call([nasm, "-f", "bin", "boot/stage2.asm",
                       "-DPAYLOAD_LBA=%d" % PAYLOAD_LBA,
                       "-DPAYLOAD_SECTORS=%d" % kernel_sectors,
                       "-o", "build/stage2.bin"])
subprocess.check_call([nasm, "-f", "bin", "boot/mbr.asm",
                       "-DSTAGE2_SECTORS=%d" % S2_SECTORS,
                       "-o", "build/mbr.bin"])

mbr = bytearray(open("build/mbr.bin", "rb").read())
stage2 = open("build/stage2.bin", "rb").read()
assert len(mbr) == 512 and len(stage2) == S2_SECTORS * 512, "tamanho dos estagios invalido"

# Primeira particao: FAT16 tipo 0x06. O bootloader continua lendo o kernel
# diretamente dos setores fixos; a particao comeca depois do kernel.
mbr[446:462] = struct.pack("<B3sB3sII", 0x80, b"\xfe\xff\xff", 0x06,
                             b"\xfe\xff\xff", partition_lba, partition_size)

# Boot sector FAT16 (BPB + EBR).
boot = bytearray(512)
boot[0:3] = b"\xEB\x3C\x90"
boot[3:11] = b"NEGXUSOS"
struct.pack_into("<H", boot, 11, 512)                  # bytes/sector
boot[13] = FAT16_SECTORS_PER_CLUSTER
struct.pack_into("<H", boot, 14, FAT16_RESERVED)
boot[16] = FAT16_COPIES
struct.pack_into("<H", boot, 17, FAT16_ROOT_ENTRIES)
struct.pack_into("<H", boot, 19, 0)                    # total16 (usa total32)
boot[21] = 0xF8                                         # midia fixa
struct.pack_into("<H", boot, 22, FAT16_SECTORS_PER_FAT)
struct.pack_into("<H", boot, 24, 63)                   # geometria legado
struct.pack_into("<H", boot, 26, 255)
struct.pack_into("<I", boot, 28, partition_lba)        # hidden sectors
struct.pack_into("<I", boot, 32, FAT16_SECTORS)
boot[36] = 0x80
boot[38] = 0x29
struct.pack_into("<I", boot, 39, 0x4E475853)
boot[43:54] = b"NEGXUSOS   "
boot[54:62] = b"FAT16   "
boot[510:512] = b"\x55\xAA"

# FAT16 inicial: entradas 0 e 1 reservadas; raiz e area de dados vazias.
fat = bytearray(FAT16_SECTORS_PER_FAT * 512)
struct.pack_into("<HH", fat, 0, 0xFFF8, 0xFFFF)

image = bytearray(total_sectors * 512)
image[0:512] = mbr
image[512:512 + len(stage2)] = stage2
image[PAYLOAD_LBA * 512:PAYLOAD_LBA * 512 + len(kernel)] = kernel
vol = partition_lba * 512
image[vol:vol + 512] = boot
fat1 = vol + FAT16_RESERVED * 512
fat2 = fat1 + FAT16_SECTORS_PER_FAT * 512
image[fat1:fat1 + len(fat)] = fat
image[fat2:fat2 + len(fat)] = fat

with open("build/negxusos.img", "wb") as f:
    f.write(image)

cluster_count = (FAT16_SECTORS - (FAT16_RESERVED + FAT16_COPIES * FAT16_SECTORS_PER_FAT +
                                  (FAT16_ROOT_ENTRIES * 32 + 511) // 512)) // FAT16_SECTORS_PER_CLUSTER
print("negxusos.img: %d MiB | kernel=%d KiB | FAT16=%d MiB | clusters=%d" %
      (len(image) // (1024 * 1024), len(kernel) // 1024,
       FAT16_SECTORS // 2048, cluster_count))
