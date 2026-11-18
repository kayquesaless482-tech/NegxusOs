#!/usr/bin/env python3
"""NegxusOS - gerador de fontes bitmap antialiased (codificação interna CP1252).
Uso: mkfont.py saida.c
Fontes de origem: Inter (SIL OFL) e DejaVu Sans Mono (licença Bitstream Vera)."""
import sys
from PIL import Image, ImageDraw, ImageFont

INTER = "/usr/share/fonts/opentype/inter/"
DEJAVU = "/usr/share/fonts/truetype/dejavu/"

# (id, nome, arquivo, tamanho, gamma)
FONTS = [
    ("UI",   INTER + "Inter-Regular.otf",  14, 0.80),
    ("UIB",  INTER + "Inter-SemiBold.otf", 14, 0.80),
    ("SM",   INTER + "Inter-Regular.otf",  12, 0.80),
    ("H1",   INTER + "Inter-SemiBold.otf", 28, 0.90),
    ("H2",   INTER + "Inter-SemiBold.otf", 21, 0.90),
    ("H3",   INTER + "Inter-SemiBold.otf", 17, 0.85),
    ("MONO", DEJAVU + "DejaVuSansMono.ttf", 14, 0.80),
]
FIRST, LAST = 0x20, 0xFF


def gen(path, size, gamma):
    font = ImageFont.truetype(path, size, layout_engine=ImageFont.Layout.BASIC)
    ascent, descent = font.getmetrics()
    glyphs, pool = [], bytearray()
    for b in range(FIRST, LAST + 1):
        try:
            ch = bytes([b]).decode("cp1252")
        except UnicodeDecodeError:
            ch = " "
        if b == 0x7F:
            ch = " "
        adv = int(round(font.getlength(ch)))
        if ch == "\u00a0":
            ch = " "
        pad = 4
        img = Image.new("L", (adv + pad * 2 + size, ascent + descent + 8), 0)
        d = ImageDraw.Draw(img)
        d.text((pad, 2), ch, font=font, fill=255)
        bbox = img.getbbox()
        if bbox is None or ch == " ":
            glyphs.append((adv, 0, 0, 0, 0, len(pool)))
            continue
        l, t, r, bt = bbox
        crop = img.crop(bbox)
        w, h = crop.size
        px = crop.load()
        off = len(pool)
        for yy in range(h):
            for xx in range(w):
                a = px[xx, yy] / 255.0
                pool.append(int(round(255 * (a ** gamma))))
        glyphs.append((adv, l - pad, t - 2 - ascent, w, h, off))
    return ascent, descent, glyphs, pool


def main():
    out = sys.argv[1]
    lines = ["/* GERADO por tools/mkfont.py - nao edite */",
             "#include \"../kernel/kernel.h\"", ""]
    table = []
    for i, (name, path, size, gamma) in enumerate(FONTS):
        asc, desc, glyphs, pool = gen(path, size, gamma)
        lines.append(f"static const glyph_t g_{name}[] = {{")
        row = []
        for (adv, bx, by, w, h, off) in glyphs:
            row.append("{%d,%d,%d,%d,%d,%d}" % (adv, w, h, bx, by, off))
        for k in range(0, len(row), 8):
            lines.append("  " + ",".join(row[k:k + 8]) + ",")
        lines.append("};")
        lines.append(f"static const uint8_t a_{name}[] = {{")
        for k in range(0, len(pool), 32):
            lines.append("  " + ",".join(str(v) for v in pool[k:k + 32]) + ",")
        if not pool:
            lines.append("  0")
        lines.append("};")
        table.append(f"  {{{asc + desc + 2}, {asc}, g_{name}, a_{name}}},")
        print(f"font {name}: {size}px, {len(pool)} bytes", file=sys.stderr)
    lines.append("const font_t font_table[NFONTS] = {")
    lines += table
    lines.append("};")
    open(out, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
