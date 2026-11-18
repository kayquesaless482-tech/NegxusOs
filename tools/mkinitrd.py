#!/usr/bin/env python3
"""NegxusOS - empacota aplicativos (.nxe) e arquivos iniciais no initrd (formato NXAR).
Tambem compila os aplicativos e gera os icones ARGB 48x48 de cada um."""
import os, struct, subprocess, sys
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
os.chdir(ROOT)
OUT = sys.argv[1] if len(sys.argv) > 1 else "build/initrd.bin"
APPDIR = "build/apps"
CFLAGS = ["-std=gnu11", "-O2", "-ffreestanding", "-fPIE", "-fvisibility=hidden", "-mno-red-zone",
          "-mgeneral-regs-only", "-fno-stack-protector", "-fno-asynchronous-unwind-tables",
          "-fwrapv", "-fno-strict-aliasing", "-finput-charset=UTF-8", "-fexec-charset=CP1252",
          "-Wall", "-Wno-unused-parameter", "-Wno-sign-compare", "-Isdk"]

# nome do aplicativo, arquivo, flags, ordem, icone
APPS = [
    ("Explorador", "explorer", 1, 1, "folder"),
    ("Cobrinha",   "snake",    1, 2, "snake"),
    ("Campo Minado", "minesweeper", 1, 3, "mine"),
    ("Paint",      "paint",    1, 4, "paint"),
    ("Terminal",   "terminal", 1, 5, "terminal"),
    ("Cubos",      "voxel",    1, 6, "cube"),
    ("Personalizar", "personal", 1, 7, "globe"),
]

def run(cmd):
    subprocess.check_call(cmd)

def icon_pixels(kind):
    S = 192                                       # supersample 4x
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    def grad_round(c0, c1):
        g = Image.new("RGBA", (S, S))
        gd = ImageDraw.Draw(g)
        for y in range(S):
            t = y / (S - 1)
            col = tuple(int(c0[i] + (c1[i] - c0[i]) * t) for i in range(3)) + (255,)
            gd.line([(0, y), (S, y)], fill=col)
        mask = Image.new("L", (S, S), 0)
        ImageDraw.Draw(mask).rounded_rectangle([8, 8, S - 9, S - 9], radius=40, fill=255)
        g.putalpha(mask)
        return g
    base = {
        "folder": ((255, 206, 90), (240, 160, 30)),
        "globe":  ((40, 150, 230), (10, 90, 180)),
        "snake":  ((40, 40, 56), (18, 24, 40)),
        "mine":   ((236, 240, 246), (196, 204, 216)),
        "paint":  ((255, 255, 255), (232, 236, 244)),
        "terminal": ((52, 56, 70), (14, 16, 24)),
        "cube": ((120, 200, 90), (60, 120, 40)),
    }[kind]
    im.alpha_composite(grad_round(*base))
    W = (255, 255, 255, 255)
    if kind == "folder":
        d.rounded_rectangle([40, 60, 152, 150], radius=14, fill=W)
        d.rounded_rectangle([40, 48, 96, 78], radius=10, fill=(255, 245, 210, 255))
        d.rounded_rectangle([40, 78, 152, 150], radius=14, fill=(255, 250, 230, 255))
    elif kind == "globe":
        d.ellipse([36, 36, 156, 156], outline=W, width=12)
        d.ellipse([84, 36, 108, 156], outline=W, width=8)
        d.line([(36, 96), (156, 96)], fill=W, width=8)
        d.line([(50, 64), (142, 64)], fill=W, width=6)
        d.line([(50, 128), (142, 128)], fill=W, width=6)
    elif kind == "snake":
        pts = [(40, 150), (70, 110), (100, 150), (130, 110), (156, 140)]
        d.line(pts, fill=(120, 230, 150, 255), width=30, joint="curve")
        for p in pts[:1]:
            d.ellipse([p[0] - 16, p[1] - 16, p[0] + 16, p[1] + 16], fill=(120, 230, 150, 255))
        d.ellipse([150, 124, 166, 140], fill=(255, 96, 96, 255))
    elif kind == "mine":
        d.ellipse([46, 46, 146, 146], fill=(30, 30, 36, 255))
        for a in range(0, 360, 45):
            import math
            x = 96 + 66 * math.cos(math.radians(a)); y = 96 + 66 * math.sin(math.radians(a))
            d.line([(96, 96), (x, y)], fill=(30, 30, 36, 255), width=12)
        d.ellipse([74, 74, 92, 92], fill=W)
    elif kind == "cube":
        d.polygon([(96, 40), (150, 66), (96, 92), (42, 66)], fill=(200, 255, 170, 255))
        d.polygon([(42, 66), (96, 92), (96, 150), (42, 124)], fill=(120, 170, 80, 255))
        d.polygon([(96, 92), (150, 66), (150, 124), (96, 150)], fill=(90, 130, 60, 255))
    elif kind == "terminal":
        d.line([(44, 62), (84, 96), (44, 130)], fill=(120, 255, 150, 255), width=12, joint="curve")
        d.rounded_rectangle([96, 124, 150, 138], radius=6, fill=W)
    elif kind == "paint":
        d.ellipse([28, 40, 168, 168], fill=(250, 240, 220, 255), outline=(180, 150, 110, 255), width=6)
        for (x, y, c) in [(70, 80, (230, 40, 40)), (110, 70, (40, 150, 230)), (134, 110, (250, 200, 40)),
                          (80, 124, (60, 180, 90)), (110, 140, (150, 60, 200))]:
            d.ellipse([x - 12, y - 12, x + 12, y + 12], fill=c + (255,))
    im = im.resize((48, 48), Image.LANCZOS)
    px = []
    for (r, g, b, a) in im.getdata():  # noqa
        px.append(struct.pack("<I", (a << 24) | (r << 16) | (g << 8) | b))
    return b"".join(px)

def build_app(name, fname, flags, order, icon):
    os.makedirs(APPDIR, exist_ok=True)
    objs = []
    for src in ["sdk/app_rt.c", "common/str.c", "apps/%s.c" % fname]:
        o = os.path.join(APPDIR, os.path.basename(src).replace(".c", "_%s.o" % fname))
        run(["gcc"] + CFLAGS + ["-c", src, "-o", o])
        objs.append(o)
    elf = os.path.join(APPDIR, fname + ".elf")
    run(["ld", "-T", "sdk/crt.ld", "--emit-relocs", "-static", "-nostdlib", "-o", elf] + objs)
    img = os.path.join(APPDIR, fname + ".bin")
    run(["objcopy", "-O", "binary", "-j", ".image", elf, img])
    image = open(img, "rb").read()
    nm = subprocess.check_output(["nm", elf]).decode()
    entry = end = None
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "nxe_main": entry = int(parts[0], 16)
        if len(parts) == 3 and parts[2] == "__nx_end": end = int(parts[0], 16)
    assert entry is not None and end is not None, "simbolos ausentes em " + fname
    rel = []
    for line in subprocess.check_output(["readelf", "-rW", elf]).decode().splitlines():
        if "R_X86_64_64" in line and "Offset" not in line:
            rel.append(int(line.split()[0], 16))
    relb = b"".join(struct.pack("<I", r) for r in rel)
    hdr_size = 68
    icon_b = icon_pixels(icon)
    image_off = hdr_size + len(icon_b)
    reloc_off = image_off + len(image)
    name_b = name.encode("latin-1")[:31].ljust(32, b"\0")
    header = struct.pack("<IHHIIIIIII", 0x3145584E, 1, flags, entry, image_off, len(image), end,
                         reloc_off, len(rel), order) + name_b
    assert len(header) == hdr_size, len(header)
    out = header + icon_b + image + relb
    path = os.path.join(APPDIR, fname + ".nxe")
    open(path, "wb").write(out)
    print("app %-14s %6d bytes  relocs=%d  mem=%d" % (name, len(out), len(rel), end))
    return path

def main():
    entries = []   # (path, isdir, data)
    entries.append(("Apps", 1, b""))
    entries.append(("Documentos", 1, b""))
    entries.append(("Imagens", 1, b""))
    for name, fname, flags, order, icon in APPS:
        p = build_app(name, fname, flags, order, icon)
        entries.append(("Apps/%s.nxe" % fname, 0, open(p, "rb").read()))
    welcome = ("Bem-vindo ao NegxusOS!\r\n\r\n"
               "Aplicativos incluidos:\r\n"
               "  - Explorador de Arquivos\r\n"
               "  - Jogo da Cobrinha\r\n"
               "  - Campo Minado\r\n"
               "  - Paint\r\n\r\n"
               "Arquivos .nxe sao os programas do NegxusOS.\r\n"
               "Desenhos do Paint usam a extensao .npt.\r\n").encode("utf-8")
    entries.append(("Documentos/Bem-vindo.txt", 0, welcome))
    blob = bytearray(b"NXAR" + struct.pack("<I", len(entries)))
    for path, isdir, data in entries:
        pb = path.encode()
        blob += struct.pack("<HBBI", len(pb), isdir, 0, len(data)) + pb
        blob += b"\0" * ((-len(pb)) % 4)
        if not isdir:
            blob += data + b"\0" * ((-len(data)) % 4)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "wb").write(bytes(blob))
    print("initrd: %d entradas, %d bytes" % (len(entries), len(blob)))

if __name__ == "__main__":
    main()
