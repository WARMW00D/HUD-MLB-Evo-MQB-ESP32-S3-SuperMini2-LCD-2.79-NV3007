#!/usr/bin/env python3
"""
gen_speed_outline.py — hud_font_mspeed_ol.c: «обводка» цифр скорости ОДНИМ шрифтом.
Раньше красная обводка рисовалась восемью копиями цифр со сдвигами (8 больших надписей поверх белой — экран лагал).
Здесь те же восемь сдвигов склеены в маску заранее: глyфы того же шрифта, но с запасом в 2 px вокруг, поэтому
обводка — одна надпись под белыми цифрами, а выглядит так же.  Метрики (adv_w) те же, смещения ofs уменьшены на 2:
надпись обводки ставится по x в ту же точку, что и белая, по y — на 2 px выше (строка шрифта выше на 4 px).
Запуск (из папки скетча):  python3 tools/gen_min_native.py fonts hud_font_mspeed   # даёт tools/fonts/Overpass-Medium-sq90.ttf
                           python3 tools/gen_speed_outline.py
Нужен lv_font_conv (npm i -g lv_font_conv) и Pillow не нужен.
"""
import os, re, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.environ.get("HUD_OUT", os.path.join(HERE, ".."))
FONT = os.path.join(HERE, "fonts", "Overpass-Medium-sq90.ttf")
SIZE = 102
OFFS = [(-2, 0), (2, 0), (0, -2), (0, 2), (-2, -1), (2, -1), (-2, 1), (2, 1)]   # (dx, dy), dy вниз — как в hud_mockup.c
PAD = 2

def main():
    with tempfile.TemporaryDirectory() as td:
        raw = os.path.join(td, "raw.c")
        subprocess.run(["lv_font_conv", "--font", FONT, "--size", str(SIZE), "--bpp", "4", "--format", "lvgl", "--symbols", "0123456789",
                        "--no-compress", "--no-prefilter", "--force-fast-kern-format", "-o", raw], check=True)
        s = open(raw, encoding="utf-8").read()
    bm = re.search(r"glyph_bitmap\[\] = \{(.*?)\n\};", s, re.S)
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{1,2})", bm.group(1)))
    dsc_m = re.search(r"(glyph_dsc\[\] = \{\n)(.*?)(\n\};)", s, re.S)
    rows = re.findall(r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}", dsc_m.group(2))
    glyphs = [tuple(int(v) for v in r) for r in rows]
    new_bm = bytearray(); new_dsc = []
    for gi, (idx, adv, w, h, ox, oy) in enumerate(glyphs):
        if w == 0 or h == 0:
            new_dsc.append((0, adv, 0, 0, ox, oy)); continue
        def px(k):
            b = data[idx + k // 2]
            return (b >> 4) if k % 2 == 0 else (b & 15)
        nw, nh = w + 2 * PAD, h + 2 * PAD
        img = [[0] * nw for _ in range(nh)]
        for y in range(h):
            for x in range(w):
                v = px(y * w + x)
                if not v: continue
                for dx, dy in OFFS:
                    X, Y = x + PAD + dx, y + PAD + dy
                    if img[Y][X] < v: img[Y][X] = v
        flat = [v for row in img for v in row]
        if len(flat) % 2: flat.append(0)
        start = len(new_bm)
        for i in range(0, len(flat), 2): new_bm.append((flat[i] << 4) | flat[i + 1])
        new_dsc.append((start, adv, nw, nh, ox - PAD, oy - PAD))
    s = s.replace(bm.group(1), "\n    " + ",\n    ".join(", ".join("0x%02x" % b for b in new_bm[i:i + 24]) for i in range(0, len(new_bm), 24)))
    lines = []
    for i, (st, adv, w, h, ox, oy) in enumerate(new_dsc):
        tail = " /* id = 0 reserved */," if i == 0 else ("," if i < len(new_dsc) - 1 else "")
        lines.append("    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, .ofs_x = %d, .ofs_y = %d}%s" % (st, adv, w, h, ox, oy, tail))
    s = s.replace(dsc_m.group(2), "\n".join(lines))
    # строка выше на 2*PAD и базовая линия выше на PAD: раздутый глиф целиком внутри рамки надписи. Иначе при частичной
    # отрисовке (прошивка рисует полосами только изменившиеся области) крайние строки обводки за рамкой не перерисовываются.
    # Надпись обводки ставится на PAD px выше белой (SPD_OL_PAD в hud_mockup.c).
    s = re.sub(r"\.line_height = (\d+),", lambda m: ".line_height = %d," % (int(m.group(1)) + 2 * PAD), s, count=1)
    s = re.sub(r"\.base_line = (-?\d+),", lambda m: ".base_line = %d," % (int(m.group(1)) + PAD), s, count=1)
    s = s.replace("HUD_FONT_MSPEED", "HUD_FONT_MSPEED_OL").replace("hud_font_mspeed", "hud_font_mspeed_ol")
    # lv_font_conv называет шрифт по имени выходного файла (raw) — привести к hud_font_mspeed_ol
    s = re.sub(r"\braw\b", "hud_font_mspeed_ol", s)
    s = re.sub(r"HUD_FONT_RAW", "HUD_FONT_MSPEED_OL", s)
    s = re.sub(r"\bRAW\b", "MSPEED_OL", s)
    s = s.replace("#ifdef LV_LVGL_H_INCLUDE_SIMPLE", "#define LV_LVGL_H_INCLUDE_SIMPLE\n#ifdef LV_LVGL_H_INCLUDE_SIMPLE", 1)
    s = re.sub(r"-o \S*hud_font_mspeed_ol\.c", "-o hud_font_mspeed_ol.c", s)
    s = re.sub(r"\bMSPEED_OL\b", "HUD_FONT_MSPEED_OL", s)
    s = re.sub(r"--font \S*[/\\]([^/\\\s]+\.(?:ttf|otf))", r"--font \1", s)
    out = os.path.join(OUT, "hud_font_mspeed_ol.c")
    open(out, "w", encoding="utf-8").write(s)
    print("ok", out, os.path.getsize(out))

if __name__ == "__main__":
    main()
