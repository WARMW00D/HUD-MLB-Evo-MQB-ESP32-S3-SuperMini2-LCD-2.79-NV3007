#!/usr/bin/env python3
"""
gen_min_native.py — ресурсы минимального режима supermini_hud2 в НАТИВНЫХ координатах панели 428x142.
Геометрия утверждена по превью v13: левая/центральная/правая зоны уменьшены в 0.74, нижний ряд (знак, камера) — в 1.073.
Выход (в папку скетча): hud_min_images.c/.h, hud_nav_images.c/.h, hud_font_m*.c
Запуск:  python3 tools/gen_min_native.py [images|nav|fonts ...]
Нужны: Pillow, fontTools, lv_font_conv (npm i -g lv_font_conv); arrows/arrows.bin и hud_images.c-источник иконок
(tools/icons_src.c — 8-битные маски img_sign_car, img_cam_icon, img_limiter).
"""
import os, re, sys, struct, subprocess
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.environ.get("HUD_OUT", os.path.join(HERE, "..", "supermini_hud2"))
S = 0.74                 # масштаб зон
SB = 1.073               # масштаб нижнего ряда (знак / камера)
S4 = 4

# ------------------------------------------------------------------ общие помощники
def pack4(im):
    w, h = im.size; px = im.tobytes(); out = bytearray()
    for y in range(h):
        row = px[y * w:(y + 1) * w]
        for x in range(0, w, 2):
            a = (row[x] + 8) // 17
            b = (row[x + 1] + 8) // 17 if x + 1 < w else 0
            out.append((min(a, 15) << 4) | min(b, 15))
    return bytes(out)

def carray(name, data):
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 24]) for i in range(0, len(data), 24)]
    return f"static const uint8_t {name}_map[] = {{\n    " + ",\n    ".join(rows) + "\n};\n"

def a4(f, h, n, im, offs=False, export=True):
    """картинка-маска ALPHA_4BIT, обрезанная по рамке; смещение — в #define"""
    bb = im.getbbox() or (0, 0, 1, 1); c = im.crop(bb); data = pack4(c)
    f.write(carray(n, data))
    f.write(f"{'const' if export else 'static const'} lv_img_dsc_t img_{n} = {{\n    .header.cf = LV_IMG_CF_ALPHA_4BIT, .header.always_zero = 0, .header.reserved = 0,\n"
            f"    .header.w = {c.width}, .header.h = {c.height}, .data_size = {len(data)}, .data = {n}_map,\n}};\n\n")
    h.write(f"extern const lv_img_dsc_t img_{n};\n")
    if offs: h.write(f"#define {n.upper()}_X {bb[0]}\n#define {n.upper()}_Y {bb[1]}\n")
    return len(data), c.size

# ------------------------------------------------------------------ иконка ACC (рисунок 1254 px)
OX, OY = 44, 154; CX0 = 626
SC = 0.0999            # 0.135 * 0.74
WI, HI = round(1166 * SC), round(902 * SC); K = SC * S4
def T(x, y): return ((x - OX) * K, (y - OY) * K)
def own_car(d, cx, top, s):
    X = lambda x: T(cx + (x - CX0) * s, 0)[0]
    Y = lambda y: T(0, top + (y - 651) * s)[1]
    w = lambda v: max(1, int(round(v * s * K)))
    col = 255
    pts = [(X(CX0 - 250), Y(786)), (X(CX0 - 172), Y(666)), (X(CX0 + 172), Y(666)), (X(CX0 + 250), Y(786))]
    d.line(pts, fill=col, width=w(32), joint="curve")
    for (px, py) in pts:
        r = w(32) / 2; d.ellipse([px - r, py - r, px + r, py + r], fill=col)
    d.rounded_rectangle([X(289), Y(775), X(962), Y(807)], radius=w(16), fill=col)
    d.rounded_rectangle([X(314), Y(790), X(936), Y(978)], radius=w(55), outline=col, width=w(35))
    d.rounded_rectangle([X(314), Y(864), X(471), Y(896)], radius=w(16), fill=col)
    d.rounded_rectangle([X(780), Y(864), X(936), Y(896)], radius=w(16), fill=col)
    d.rounded_rectangle([X(314), Y(950), X(394), Y(1050)], radius=w(20), fill=col)
    d.rounded_rectangle([X(856), Y(950), X(936), Y(1050)], radius=w(20), fill=col)
def layer(fn):
    im = Image.new("L", (WI * S4, HI * S4), 0); fn(ImageDraw.Draw(im))
    return im.resize((WI, HI), Image.LANCZOS)
P = lambda pts: [T(x, y) for x, y in pts]
def lane_l(d): d.polygon(P([(352, 313), (394, 313), (152, 1050), (50, 1050)]), fill=255)
def lane_r(d): d.polygon(P([(902, 313), (860, 313), (1102, 1050), (1204, 1050)]), fill=255)
def lead(d): own_car(d, 626, 160, 0.652)
def own(d): own_car(d, 626, 651, 1.0)
def arcs(d):
    for apex, half, drop, th in ((445, 209, 40, 15), (498, 182, 32, 15), (550, 155, 26, 14), (598, 118, 20, 12)):
        line = [T(626 + half * u, apex + drop * u * u) for u in [i / 24 * 2 - 1 for i in range(25)]]
        d.line(line, fill=255, width=max(1, int(th * K)), joint="curve")
def swa_tri(w, h):
    im = Image.new("L", (w * 4, h * 4), 0); d = ImageDraw.Draw(im)
    d.polygon([(w * 2, 0), (0, h * 4 - 1), (w * 4 - 1, h * 4 - 1)], fill=255)
    return im.resize((w, h), Image.LANCZOS)

# ------------------------------------------------------------------ 8-битные маски из старых ресурсов
def load_a8(src, name):
    t = open(src, encoding="utf-8").read()
    m = re.search(r"uint8_t img_%s_map\[\] = \{(.*?)\};" % name, t, re.S)
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", m.group(1)))
    mm = re.search(r"const lv_img_dsc_t img_%s = \{.*?header\.w = (\d+), \.header\.h = (\d+)" % name, t, re.S)
    w, h = int(mm.group(1)), int(mm.group(2))
    assert len(data) == w * h, (name, len(data), w, h)
    return Image.frombytes("L", (w, h), data)

def scaled(im, k):
    return im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)

SWA_W, SWA_H = 19, 16     # 26x22 * 0.74
SWA_TOP = 15              # верх треугольника в рамке иконки (24-4 = 20 px * 0.74)
SWA_GAP = 4               # зазор до внешнего края полосы

def gen_images():
    src = os.path.join(HERE, "icons_src.c")
    with open(os.path.join(OUT, "hud_min_images.c"), "w") as f, open(os.path.join(OUT, "hud_min_images.h"), "w") as h:
        f.write("/* Сгенерировано tools/gen_min_native.py — не править вручную */\n")
        f.write('#define LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n#include "hud_min_images.h"\n\n')
        h.write("/* Сгенерировано tools/gen_min_native.py — не править вручную */\n#ifndef HUD_MIN_IMAGES_H\n#define HUD_MIN_IMAGES_H\n#include \"lvgl.h\"\n\n")
        h.write(f"/* иконка ACC: рамка {WI} x {HI}; X/Y — смещение обрезанной картинки в рамке */\n#define M_ACC_W {WI}\n#define M_ACC_H {HI}\n")
        tot = 0
        for n, fn in (("m_lane_l", lane_l), ("m_lane_r", lane_r), ("m_lead", lead), ("m_arcs", arcs), ("m_own", own)):
            tot += a4(f, h, n, layer(fn), True)[0]
        # Side Assist: треугольник; положение — SWA_GAP от внешнего края полосы на нижней строке треугольника
        yb = SWA_TOP + SWA_H - 2
        yd = OY + yb / SC
        xl = 352 - (352 - 50) * (yd - 313) / 737
        xr = 902 + (1204 - 902) * (yd - 313) / 737
        sl = round((xl - OX) * SC) - SWA_GAP - SWA_W
        sr = round((xr - OX) * SC) + SWA_GAP + 1
        tot += a4(f, h, "m_swa", swa_tri(SWA_W, SWA_H))[0]
        h.write(f"#define M_SWA_L_X {sl}\n#define M_SWA_R_X {sr}\n#define M_SWA_Y {SWA_TOP}\n")
        # значки нижнего ряда и под иконкой ACC
        sc = load_a8(src, "sign_car"); cam = load_a8(src, "cam_icon"); lim = load_a8(src, "limiter")
        for n, im, k in (("m_sign_car", sc, SB), ("m_cam", cam, SB), ("m_lim", lim, S)):
            im2 = scaled(im, k); n_, sz = a4(f, h, n, im2); tot += n_
            h.write(f"#define {n.upper()}_W {sz[0]}\n#define {n.upper()}_H {sz[1]}\n")
        h.write("\n#endif\n")
    print("images ok, data", tot, "bytes; SWA x", sl, sr)

# ------------------------------------------------------------------ навигация: последние кадры arrows.bin, 0.74
sys.path.insert(0, HERE)
def gen_nav():
    import gen_nav_last as N
    A = N.Anim(os.path.join(HERE, "arrows", "arrows.bin"))
    pal = [(A.pal[i * 4 + 2], A.pal[i * 4 + 1], A.pal[i * 4], A.pal[i * 4 + 3]) for i in range(16)]   # R,G,B,A
    comp = [tuple(c[j] * c[3] // 255 for j in range(3)) for c in pal]          # на чёрном
    def frame_img(name, k):
        raw = A.frame(name, k); w, h = A.w, A.h; st = (w + 1) // 2
        im = Image.new("RGB", (w, h), (0, 0, 0)); px = im.load()
        for y in range(h):
            for x in range(w):
                v = (raw[y * st + x // 2] >> (4 if x % 2 == 0 else 0)) & 15
                if v: px[x, y] = comp[v]
        return im
    cache = {}
    def idx_img(name, k):
        key = (name, k)
        if key in cache: return cache[key]
        im = frame_img(name, k)
        im = im.resize((round(A.w * S), round(A.h * S)), Image.LANCZOS)
        w, h = im.size; px = im.load()
        idx = [[0] * w for _ in range(h)]
        for y in range(h):
            for x in range(w):
                r, g, b = px[x, y]
                if max(r, g, b) < 10: continue
                best, bd = 1, 1 << 30
                for i in range(1, 16):
                    c = comp[i]; dd = (c[0] - r) ** 2 + (c[1] - g) ** 2 + (c[2] - b) ** 2
                    if dd < bd: bd, best = dd, i
                idx[y][x] = best
        ys = [y for y in range(h) if any(idx[y])]; xs = [x for x in range(w) if any(idx[y][x] for y in range(h))]
        x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
        x0 -= x0 % 2
        if (x1 - x0) % 2: x1 += 1
        out = bytearray()
        for y in range(y0, y1):
            for x in range(x0, x1, 2):
                a = idx[y][x] if x < w else 0; b2 = idx[y][x + 1] if x + 1 < w else 0
                out.append((a << 4) | b2)
        cache[key] = ("nav_f_%s%s" % (name, "" if k < 0 else "_%d" % k), bytes(out), x0, y0, x1 - x0, y1 - y0)
        return cache[key]
    tables = {
        "nav_turn": [N.big_name("turn", s_) for s_ in range(16)],
        "nav_round_rht": [N.big_name("round", s_, 0) for s_ in range(16)],
        "nav_round_lht": [N.big_name("round", s_, 1) for s_ in range(16)],
        "nav_exit": [N.big_name("exit", 0), N.big_name("exit", 1)],
        "nav_uturn": [N.big_name("uturn", 0), N.big_name("uturn", 1)],
        "nav_arrived": [N.big_name("arrived")],
    }
    tables["nav_fork"] = tables["nav_exit"]
    for ks in tables.values():
        for k in ks: idx_img(*k)
    palblob = bytearray()
    for i in range(16):
        r, g, b, a = pal[i]
        palblob += bytes((b, g, r, 0 if i == 0 else 255))     # BGRA, индекс 0 прозрачный, остальные непрозрачные (фон чёрный)
        # палитру кладём как есть (композит на чёрном уже в индексах)
    # цвета палитры: композит на чёрном
    palblob = bytearray()
    for i in range(16):
        r, g, b = comp[i]
        palblob += bytes((b, g, r, 0 if i == 0 else 255))
    tot = 0
    with open(os.path.join(OUT, "hud_nav_images.c"), "w") as f:
        f.write("/* Сгенерировано tools/gen_min_native.py — не править вручную */\n")
        f.write('#define LV_LVGL_H_INCLUDE_SIMPLE\n#include "lvgl.h"\n#include "hud_config.h"\n#include "hud_nav_images.h"\n\n')
        f.write("/* Последний кадр анимации манёвра (arrows.bin), уменьшен до 0.74, индексная 4-бит картинка: палитра 16 x BGRA, затем пиксели */\n")
        for key, (n, data, x, y, w, h) in cache.items():
            blob = bytes(palblob) + data; tot += len(blob)
            f.write(carray(n, blob))
            f.write(f"static const lv_img_dsc_t img_{n} = {{\n    .header.cf = LV_IMG_CF_INDEXED_4BIT, .header.always_zero = 0, .header.reserved = 0,\n"
                    f"    .header.w = {w}, .header.h = {h}, .data_size = {len(blob)}, .data = {n}_map,\n}};\n\n")
        for name, ks in tables.items():
            f.write(f"static const NavImg {name}[{len(ks)}] = {{\n")
            for k in ks:
                n, data, x, y, w, h = cache[k]; f.write(f"    {{ &img_{n}, {x}, {y} }},\n")
            f.write("};\n")
        f.write("#if HUD_LEFT_HAND_TRAFFIC\n#define nav_round nav_round_lht\n#else\n#define nav_round nav_round_rht\n#endif\n\n")
        f.write("const NavSet nav_big = { nav_turn, nav_round, nav_exit, nav_fork, nav_uturn, nav_arrived };\n")
    with open(os.path.join(OUT, "hud_nav_images.h"), "w") as f:
        f.write("/* Сгенерировано tools/gen_min_native.py — не править вручную */\n#ifndef HUD_NAV_IMAGES_H\n#define HUD_NAV_IMAGES_H\n#include \"lvgl.h\"\n\n"
                f"#define NAV_AREA_W {round(A.w * S)}\n#define NAV_AREA_H {round(A.h * S)}\n\n"
                "typedef struct { const lv_img_dsc_t *img; int16_t x, y; } NavImg;   /* x,y — смещение картинки в области стрелки */\n"
                "typedef struct { const NavImg *turn, *round, *exit, *fork, *uturn, *arrived; } NavSet;\n\n"
                "extern const NavSet nav_big;   /* turn[16] — сектор 22.5 град против часовой от «прямо»; round[16]; exit/fork/uturn: [0] налево, [1] направо */\n\n#endif\n")
    print("nav ok", len(cache), "images,", tot, "bytes, area", round(A.w * S), "x", round(A.h * S))

# ------------------------------------------------------------------ шрифты
FD = os.path.join(HERE, "fonts")
OVER = os.path.join(FD, "Overpass-Medium.ttf"); OVER_SQ = os.path.join(FD, "Overpass-Medium-sq90.ttf")
COND = os.path.join(FD, "RobotoCondensed-Bold.ttf")
INTER_R = os.path.join(FD, "Inter-Regular-sub.otf"); INTER_B = os.path.join(FD, "Inter-Bold-sub.otf")
FONTS = [
    ("hud_font_mspeed", ("dh", 74), "0123456789", OVER_SQ),
    ("hud_font_mdist", ("dh", 25), "0123456789.,", OVER_SQ),
    ("hud_font_munit22", 16, "кмч/khpim ", INTER_R),
    ("hud_font_munit20", 15, "кмч/khpimft ", INTER_R),
    ("hud_font_mcam", 26, "0123456789мmft", INTER_B),
    ("hud_font_msign2", 35, "0123456789", COND),
    ("hud_font_msign3", 26, "0123456789", COND),
    ("hud_font_mcdisc2", 27, "0123456789", COND),
    ("hud_font_mcdisc3", 20, "0123456789", COND),
]
# сообщения (кириллица + латиница; встроенные шрифты LVGL кириллицы не имеют) — перегенерируется только по имени:
#   python3 tools/gen_min_native.py fonts hud_font_menu
MENU = ("hud_font_menu", 20, "range:0x20-0x7E,0x401,0x410-0x44F,0x451", os.path.join(FD, "Montserrat-Bold.ttf"))
# название улицы над стрелкой (узкий шрифт, кириллица + латиница) — перегенерируется только по имени:
#   python3 tools/gen_min_native.py fonts hud_font_mstreet
STREET = ("hud_font_mstreet", 18, "range:0x20-0x7E,0x401,0x410-0x44F,0x451", os.path.join(FD, "RobotoCondensed-Bold.ttf"))
# заставка «HUD VAG» при отсутствии CAN (крупная надпись и подпись с платформой) — перегенерируется только по имени:
#   python3 tools/gen_min_native.py fonts hud_font_splash hud_font_splash_sub
SPLASH = ("hud_font_splash", 72, "HUDVAG ", os.path.join(FD, "InterDisplay-Medium-sub.otf"))
SPLASH_SUB = ("hud_font_splash_sub", 22, "MLBEVOQ ", os.path.join(FD, "InterDisplay-Medium-sub.otf"))
# Крупные шрифты — без сжатия: сжатый глиф LVGL распаковывает целиком при каждой отрисовке, причём заново для каждой
# полосы экрана, в которую он попадает (цифра скорости высотой 75 px — это несколько полос). Цена — несколько КБ флеша.
NO_COMPRESS = {"hud_font_mspeed", "hud_font_mdist", "hud_font_splash"}
def make_squeezed():
    from fontTools.ttLib import TTFont
    from fontTools.pens.transformPen import TransformPen
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    f = TTFont(OVER); gs = f.getGlyphSet(); glyf, hmtx = f["glyf"], f["hmtx"]
    for name in f.getGlyphOrder():
        pen = TTGlyphPen(gs); gs[name].draw(TransformPen(pen, (0.90, 0, 0, 1, 0, 0)))
        g = pen.glyph(); adv, _ = hmtx[name]; glyf[name] = g; g.recalcBounds(glyf)
        hmtx[name] = (round(adv * 0.90), getattr(g, "xMin", 0))
    for t in ("hdmx", "LTSH"):
        if t in f: del f[t]
    f.save(OVER_SQ)
def digit_size(font, dh):
    from fontTools.ttLib import TTFont
    f = TTFont(font); g = f["glyf"][f.getBestCmap()[ord("0")]]; g.recalcBounds(f["glyf"])
    return round(dh * f["head"].unitsPerEm / (g.yMax - g.yMin))
def gen_fonts(only=()):
    make_squeezed()
    for name, size, symbols, font in FONTS + [f for f in (MENU, STREET, SPLASH, SPLASH_SUB) if f[0] in only]:
        if only and name not in only: continue
        if isinstance(size, tuple): size = digit_size(font, size[1])
        out = os.path.join(OUT, name + ".c")
        chars = ["--range", symbols[6:]] if symbols.startswith("range:") else ["--symbols", symbols]
        extra = []
        if name == "hud_font_mstreet":      # стрелка «→» (SignPost) — нет в Roboto Condensed, берём из Montserrat Bold
            extra = ["--font", os.path.join(FD, "Montserrat-Bold.ttf"), "--range", "0x2192"]
        subprocess.run(["lv_font_conv", "--font", font, "--size", str(size), "--bpp", "4", "--format", "lvgl"] + chars + extra +
                       ["-o", out, "--force-fast-kern-format", "--no-prefilter"] +
                       (["--no-compress"] if name in NO_COMPRESS else []), check=True)
        s = open(out, encoding="utf-8").read()
        s = s.replace("#ifdef LV_LVGL_H_INCLUDE_SIMPLE", "#define LV_LVGL_H_INCLUDE_SIMPLE\n#ifdef LV_LVGL_H_INCLUDE_SIMPLE", 1)
        s = re.sub(r"--font \S*[/\\]([^/\\\s]+\.(?:ttf|otf))", r"--font \1", s)
        s = re.sub(r"-o \S*" + name + r"\.c", "-o " + name + ".c", s)
        open(out, "w", encoding="utf-8").write(s)
        print("font ok", name, size, os.path.getsize(out))

if __name__ == "__main__":
    args = sys.argv[1:]
    what = [a for a in args if a in ("images", "nav", "fonts")] or ["images", "nav", "fonts"]
    names = [a for a in args if a.startswith("hud_font_")]
    if "images" in what: gen_images()
    if "nav" in what: gen_nav()
    if "fonts" in what: gen_fonts(names)
