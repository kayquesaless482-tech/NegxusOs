#!/usr/bin/env python3
"""Build standalone do NegxusOS para Windows (nao requer GNU Make)."""
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

CC = os.environ.get("CC", "x86_64-elf-gcc")
LD = os.environ.get("LD", "x86_64-elf-ld")
OBJCOPY = os.environ.get("OBJCOPY", "x86_64-elf-objcopy")
NASM = os.environ.get("NASM", "nasm")
PYTHON = os.environ.get("PYTHON", sys.executable)

KFLAGS = [
    "-std=gnu11", "-O0", "-ffreestanding", "-fno-pic", "-fno-pie",
    "-mno-red-zone", "-mgeneral-regs-only", "-fno-stack-protector",
    "-fno-asynchronous-unwind-tables", "-fwrapv", "-fno-strict-aliasing",
    "-finput-charset=UTF-8", "-fexec-charset=CP1252", "-Wall", "-Wextra",
    "-Wno-unused-parameter", "-Wno-sign-compare",
    "-Wno-address-of-packed-member",
]


def run(args, env=None):
    print("+", " ".join(args))
    subprocess.check_call(args, cwd=ROOT, env=env)


def main():
    build = os.path.join(ROOT, "build")
    if os.path.isdir(build):
        shutil.rmtree(build)
    os.makedirs(build)

    objects = []
    sources = sorted(glob.glob(os.path.join("kernel", "*.c")))
    sources.append(os.path.join("common", "str.c"))

    for source in sources:
        relative = os.path.splitext(source)[0] + ".o"
        obj = os.path.join("build", relative.replace("/", os.sep))
        os.makedirs(os.path.dirname(obj), exist_ok=True)
        run([CC] + KFLAGS + ["-c", source, "-o", obj])
        objects.append(obj)

    for source in ["kernel/entry.asm", "kernel/isr.asm"]:
        obj = os.path.join("build", "kernel",
                           os.path.basename(source).replace(".asm", ".o"))
        run([NASM, "-f", "elf64", source, "-o", obj])
        objects.append(obj)

    env = os.environ.copy()
    env["CC"] = CC
    env["LD"] = LD
    env["OBJCOPY"] = OBJCOPY
    env["NASM"] = NASM
    env["PYTHON"] = PYTHON
    env.setdefault("NM", "x86_64-elf-nm")
    env.setdefault("READELF", "x86_64-elf-readelf")

    run([PYTHON, "tools/mkinitrd.py", "build/initrd.bin"], env=env)

    initrd_obj = os.path.join("build", "kernel", "initrd.o")
    run([CC, "-c", "kernel/initrd.S", "-o", initrd_obj])
    objects.append(initrd_obj)

    elf = os.path.join("build", "kernel.elf")
    run([LD, "-nostdlib", "-z", "max-page-size=4096",
         "-T", "kernel/linker.ld", "-o", elf] + objects)

    kernel_bin = os.path.join("build", "kernel.bin")
    run([OBJCOPY, "-O", "binary", elf, kernel_bin])
    run([PYTHON, "tools/mkimage.py"], env=env)

    print("")
    print("BUILD CONCLUIDO: build/negxusos.img")


if __name__ == "__main__":
    main()
