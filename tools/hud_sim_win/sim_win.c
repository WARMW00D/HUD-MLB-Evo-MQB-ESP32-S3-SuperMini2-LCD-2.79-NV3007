/*
  sim_win.c — HUD Sim: окно Windows для проверки экрана HUD на записанных логах сниффера
  ------------------------------------------------------------------------------
  Экран рисует тот же код, что в прошивке (hud_mockup.c + can_decode.c + LVGL 8), кадры
  берутся из can_log_NNNN.txt. Тачскрина и меню на самом HUD нет — настройки в машине меняются
  с Wi-Fi-портала, здесь их заменяет отдельное окно «Настройки HUD» (кнопка или M).
  Клавиши: Пробел — пуск/пауза, ←/→ — 10 с (с Shift — 60 с), ↑/↓ — скорость,
  N/P — следующий/предыдущий манёвр, PgUp/PgDn — файл, M — настройки HUD, S — скриншот, Ctrl+O — открыть.
  Сборка — build_win.bat (zig cc или MinGW-w64 gcc).
*/
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "sim_core.h"
#include "sim_map.h"
#include "hud_config.h"
#include "hud_data.h"
#include "hud_sound.h"

/* ---- GDI+ (плоский API) — только чтобы сохранить PNG ---- */
typedef struct { UINT32 GdiplusVersion; void *DebugEventCallback; BOOL SuppressBackgroundThread; BOOL SuppressExternalCodecs; } GpStartupInput;
int WINAPI GdiplusStartup(ULONG_PTR *token, const GpStartupInput *in, void *out);
int WINAPI GdipCreateBitmapFromScan0(INT w, INT h, INT stride, INT format, BYTE *scan0, void **bitmap);
int WINAPI GdipSaveImageToFile(void *image, const WCHAR *filename, const CLSID *encoder, const void *params);
int WINAPI GdipDisposeImage(void *image);
#define GP_PIXFMT_32RGB 0x00022009
static const CLSID CLSID_PNG_ENC = { 0x557CF406, 0x1A04, 0x11D3, { 0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E } };

static int g_ui;                                   /* язык интерфейса симулятора: 0 — русский, 1 — English (hud_sim.ini [ui] lang) */
#define TR(ru, en) (g_ui ? L##en : L##ru)

enum { IDC_OPENF = 100, IDC_OPEND, IDC_PREV, IDC_PLAY, IDC_NEXT, IDC_B10, IDC_F10, IDC_PMAN, IDC_NMAN,
       IDC_SPEED, IDC_SETTINGS, IDC_MAP, IDC_SHOT, IDC_SOUND, IDC_LIST, IDC_LOG,
       IDM_CAMS = 200, IDM_EXIT, IDM_ZOOM1, IDM_ZOOM2, IDM_ZOOM3, IDM_ZOOM4, IDM_CLEARLOG, IDM_PLAT_MLB, IDM_PLAT_MQB, IDM_SETTINGS, IDM_MAP, IDM_LANG_RU, IDM_LANG_EN };

#define NBTN 12
static const struct { int id; const wchar_t *ru, *en; int w; } k_btn[NBTN] = {
    { IDC_OPENF, L"Файл…", L"File…", 62 },      { IDC_OPEND, L"Папка…", L"Folder…", 66 },
    { IDC_PREV,  L"« файл", L"« file", 62 },     { IDC_PLAY,  L"Пуск", L"Play", 70 },       { IDC_NEXT, L"файл »", L"file »", 62 },
    { IDC_B10,   L"−10 с", L"−10 s", 54 },      { IDC_F10,   L"+10 с", L"+10 s", 54 },
    { IDC_PMAN,  L"« манёвр", L"« turn", 78 },   { IDC_NMAN,  L"манёвр »", L"turn »", 78 },
    { IDC_SETTINGS, L"Настройки HUD", L"HUD settings", 104 },   { IDC_MAP, L"Карта", L"Map", 62 },   { IDC_SHOT,  L"Скриншот", L"Screenshot", 80 },
};
static const double k_speeds[] = { 0.25, 0.5, 1, 2, 4, 8, 16, 32 };
#define NSPEED ((int)(sizeof k_speeds / sizeof k_speeds[0]))

static HWND   g_wnd, g_tl, g_list, g_log, g_st1, g_st2, g_btn[NBTN], g_speed, g_sound;
static HFONT  g_font, g_mono;
static int    g_zoom = 2, g_dpi = 96;
static RECT   g_hud;
static int    g_band_h;
static bool   g_sound_on = true;
static wchar_t g_exedir[MAX_PATH], g_ini[MAX_PATH + 16];
static wchar_t g_dir[MAX_PATH];
static wchar_t (*g_files)[MAX_PATH];
static int    g_nfiles, g_cur = -1;
static bool   g_in_tick;

static int S(int v) { return MulDiv(v, g_dpi, 96); }

/* ============================ настройки (hud_sim.ini рядом с exe) ============================ */
static void cfg_set_int(const wchar_t *sec, const wchar_t *key, int v)
{
    wchar_t b[32]; swprintf(b, 32, L"%d", v);
    WritePrivateProfileStringW(sec, key, b, g_ini);
}

/* ============================ журнал ============================ */
static bool s_log_bol = true;                      /* начало строки — перед ней ставится время лога */

static void log_append_w(const wchar_t *w)
{
    if (!g_log) return;
    int len = GetWindowTextLengthW(g_log);
    if (len > 300000) {                            /* журнал разросся — убрать старую половину */
        SendMessageW(g_log, EM_SETSEL, 0, len / 2);
        SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)L"");
        len = GetWindowTextLengthW(g_log);
    }
    SendMessageW(g_log, EM_SETSEL, len, len);
    SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)w);
}

void sim_plat_log(const char *utf8)
{
    wchar_t in[2048], out[4400];
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, in, 2048);
    if (n <= 0) return;
    int o = 0;
    for (int i = 0; in[i] && o < 4300; i++) {
        if (s_log_bol) {
            s_log_bol = false;
            if (sim_loaded()) {
                uint32_t p = sim_pos_ms(); sim_dt_t t;
                o += swprintf(out + o, 16, L"%02u:%04.1f  ", p / 60000, (p % 60000) / 1000.0);
                if (sim_car_time(&t)) o += swprintf(out + o, 16, L"[%02u:%02u:%02u]  ", t.hour, t.min, t.sec);    /* часы машины из лога */
            }
        }
        if (in[i] == L'\r') continue;
        if (in[i] == L'\n') { out[o++] = L'\r'; out[o++] = L'\n'; s_log_bol = true; }
        else out[o++] = in[i];
    }
    out[o] = 0;
    log_append_w(out);
}

static void logf_w(const wchar_t *fmt, ...)
{
    wchar_t b[1024]; char u[3072];
    va_list ap; va_start(ap, fmt); vswprintf(b, 1024, fmt, ap); va_end(ap);
    if (WideCharToMultiByte(CP_UTF8, 0, b, -1, u, sizeof u, NULL, NULL) > 0) sim_plat_log(u);
}

/* ============================ звук ============================ */
void sim_plat_play(const int16_t *pcm, uint32_t n, uint32_t rate, uint8_t vol)
{
    static uint8_t *wav;
    if (!g_sound_on || !vol) return;
    PlaySoundW(NULL, NULL, 0);                     /* остановить прежний, прежде чем освобождать его буфер */
    free(wav);
    wav = (uint8_t *)malloc(44 + (size_t)n * 2);
    if (!wav) return;
    uint32_t dsz = n * 2, rsz = 36 + dsz, brate = rate * 2;
    memcpy(wav, "RIFF", 4); memcpy(wav + 4, &rsz, 4); memcpy(wav + 8, "WAVEfmt ", 8);
    uint32_t fl = 16; uint16_t pcmf = 1, ch = 1, ba = 2, bits = 16;
    memcpy(wav + 16, &fl, 4); memcpy(wav + 20, &pcmf, 2); memcpy(wav + 22, &ch, 2);
    memcpy(wav + 24, &rate, 4); memcpy(wav + 28, &brate, 4); memcpy(wav + 32, &ba, 2); memcpy(wav + 34, &bits, 2);
    memcpy(wav + 36, "data", 4); memcpy(wav + 40, &dsz, 4);
    int16_t *dst = (int16_t *)(wav + 44);
    for (uint32_t i = 0; i < n; i++) dst[i] = (int16_t)((int)pcm[i] * vol / 100);
    PlaySoundW((LPCWSTR)wav, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

/* ============================ файлы ============================ */
static void *read_file(const wchar_t *path, size_t *len)
{
    FILE *f = _wfopen(path, L"rb");
    if (!f) return NULL;
    _fseeki64(f, 0, SEEK_END); long long n = _ftelli64(f); _fseeki64(f, 0, SEEK_SET);
    char *b = (n >= 0 && n < 0x7FF00000LL) ? (char *)malloc((size_t)n + 1) : NULL;
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f); b[n] = 0; *len = (size_t)n;
    return b;
}

static const wchar_t *base_name(const wchar_t *p)
{
    const wchar_t *a = wcsrchr(p, L'\\'), *b = wcsrchr(p, L'/');
    if (b > a) a = b;
    return a ? a + 1 : p;
}

static int cmp_names(const void *a, const void *b) { return lstrcmpiW((const wchar_t *)a, (const wchar_t *)b); }

static void list_dir(const wchar_t *dir)
{
    static const wchar_t *pat[] = { L"*.txt", L"*.log" };
    free(g_files); g_files = NULL; g_nfiles = 0; g_cur = -1;
    int cap = 0;
    lstrcpynW(g_dir, dir, MAX_PATH);
    int dl = lstrlenW(g_dir);
    if (dl && (g_dir[dl - 1] == L'\\' || g_dir[dl - 1] == L'/')) g_dir[dl - 1] = 0;
    for (int k = 0; k < 2; k++) {
        wchar_t q[MAX_PATH + 8]; WIN32_FIND_DATAW fd;
        swprintf(q, MAX_PATH + 8, L"%ls\\%ls", g_dir, pat[k]);
        HANDLE h = FindFirstFileW(q, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (g_nfiles == cap) { cap = cap ? cap * 2 : 64; g_files = realloc(g_files, (size_t)cap * sizeof *g_files); if (!g_files) { g_nfiles = 0; FindClose(h); return; } }
            lstrcpynW(g_files[g_nfiles++], fd.cFileName, MAX_PATH);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (g_nfiles) qsort(g_files, (size_t)g_nfiles, sizeof *g_files, cmp_names);
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_nfiles; i++) SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)g_files[i]);
}

static void update_title(void);
static void update_play_button(void) { SetWindowTextW(g_btn[3], sim_playing() ? TR("Пауза", "Pause") : TR("Пуск", "Play")); }

static bool load_index(int i, bool cont)
{
    if (i < 0 || i >= g_nfiles) return false;
    wchar_t path[MAX_PATH * 2];
    swprintf(path, MAX_PATH * 2, L"%ls\\%ls", g_dir, g_files[i]);
    HCURSOR old = SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
    size_t len = 0;
    char *txt = (char *)read_file(path, &len);
    bool ok = txt && sim_load_log(txt, len, cont);
    free(txt);
    SetCursor(old);
    if (!ok) {
        logf_w(TR("[sim] не удалось прочитать лог (нет кадров формата «мс S|X ID DLC байты»): %ls\n", "[sim] cannot read the log (no frames in the format \"ms S|X ID DLC bytes\"): %ls\n"), path);
        return false;
    }
    g_cur = i;
    SendMessageW(g_list, LB_SETCURSEL, (WPARAM)i, 0);
    update_title();
    WritePrivateProfileStringW(L"sim", L"last", path, g_ini);
    uint32_t nm, ns; sim_maneuvers(&nm); sim_nav_spans(&ns);
    logf_w(TR("[sim] %ls: кадров %u, %u:%02u, смен манёвра %u%ls\n", "[sim] %ls: %u frames, %u:%02u, %u maneuver changes%ls\n"), g_files[i], sim_frames_total(),
           sim_dur_ms() / 60000, sim_dur_ms() / 1000 % 60, nm, ns ? L"" : TR(", ведения по маршруту нет", ", no route guidance"));
    InvalidateRect(g_tl, NULL, FALSE);
    return true;
}

static void open_path(const wchar_t *path, bool autoplay)
{
    DWORD a = GetFileAttributesW(path);
    if (a == INVALID_FILE_ATTRIBUTES) return;
    int idx = 0;
    if (a & FILE_ATTRIBUTE_DIRECTORY) list_dir(path);
    else {
        wchar_t dir[MAX_PATH]; lstrcpynW(dir, path, MAX_PATH);
        wchar_t *bn = (wchar_t *)base_name(dir);
        wchar_t name[MAX_PATH]; lstrcpynW(name, bn, MAX_PATH);
        if (bn > dir) bn[-1] = 0; else lstrcpyW(dir, L".");
        list_dir(dir);
        idx = -1;
        for (int i = 0; i < g_nfiles; i++) if (!lstrcmpiW(g_files[i], name)) { idx = i; break; }
        if (idx < 0) {                             /* файл с другим расширением — добавить в список */
            g_files = realloc(g_files, (size_t)(g_nfiles + 1) * sizeof *g_files);
            if (!g_files) { g_nfiles = 0; return; }
            lstrcpynW(g_files[g_nfiles], name, MAX_PATH);
            SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)name);
            idx = g_nfiles++;
        }
    }
    if (!g_nfiles) { logf_w(TR("[sim] в папке нет логов (*.txt, *.log): %ls\n", "[sim] no logs (*.txt, *.log) in the folder: %ls\n"), path); return; }
    if (load_index(idx, false)) sim_play(autoplay);
    update_play_button();
}

static bool pick_file(const wchar_t *filter, wchar_t *out)
{
    OPENFILENAMEW o; memset(&o, 0, sizeof o);
    out[0] = 0;
    o.lStructSize = sizeof o; o.hwndOwner = g_wnd; o.lpstrFilter = filter; o.lpstrFile = out; o.nMaxFile = MAX_PATH;
    o.lpstrInitialDir = g_dir[0] ? g_dir : NULL;
    o.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&o) != 0;
}

static int CALLBACK browse_cb(HWND h, UINT msg, LPARAM lp, LPARAM data)
{
    (void)lp;
    if (msg == BFFM_INITIALIZED && data && ((const wchar_t *)data)[0]) SendMessageW(h, BFFM_SETSELECTIONW, TRUE, data);
    return 0;
}

static bool pick_folder(wchar_t *out)
{
    BROWSEINFOW b; memset(&b, 0, sizeof b);
    b.hwndOwner = g_wnd; b.lpszTitle = TR("Папка с логами сниффера (can_log_NNNN.txt)", "Folder with sniffer logs (can_log_NNNN.txt)");
    b.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE; b.lpfn = browse_cb; b.lParam = (LPARAM)g_dir;
    LPITEMIDLIST p = SHBrowseForFolderW(&b);
    if (!p) return false;
    bool ok = SHGetPathFromIDListW(p, out) != 0;
    CoTaskMemFree(p);
    return ok;
}

/* cams.bin: буфер остаётся жить, пока работает программа (как PSRAM в прошивке) */
static bool load_cams(const wchar_t *path, bool quiet)
{
    size_t n; void *b = read_file(path, &n);
    if (!b) { if (!quiet) logf_w(TR("[sim] не читается: %ls\n", "[sim] cannot read: %ls\n"), path); return false; }
    if (!sim_set_cams(b, n)) { if (!quiet) logf_w(TR("[sim] cams.bin повреждён или другой версии: %ls\n", "[sim] cams.bin is corrupt or a different version: %ls\n"), path); return false; }
    logf_w(TR("[sim] база камер загружена: %ls\n", "[sim] camera database loaded: %ls\n"), path);
    return true;
}

/* ============================ скриншот ============================ */
static void screenshot(void)
{
    int z = g_zoom, w = SIM_W * z, h = SIM_H * z;
    uint32_t *px = (uint32_t *)malloc((size_t)w * h * 4);
    if (!px) return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) px[y * w + x] = sim_fb[(y / z) * SIM_W + x / z];

    /* в буфер обмена (CF_DIB, строки снизу вверх) */
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + (size_t)w * h * 4);
    if (hg) {
        BITMAPINFOHEADER *bh = (BITMAPINFOHEADER *)GlobalLock(hg);
        memset(bh, 0, sizeof *bh);
        bh->biSize = sizeof *bh; bh->biWidth = w; bh->biHeight = h; bh->biPlanes = 1; bh->biBitCount = 32; bh->biCompression = BI_RGB;
        uint32_t *d = (uint32_t *)(bh + 1);
        for (int y = 0; y < h; y++) memcpy(d + (size_t)(h - 1 - y) * w, px + (size_t)y * w, (size_t)w * 4);
        GlobalUnlock(hg);
        if (OpenClipboard(g_wnd)) { EmptyClipboard(); if (!SetClipboardData(CF_DIB, hg)) GlobalFree(hg); CloseClipboard(); }
        else GlobalFree(hg);
    }

    /* PNG в папку shots рядом с программой */
    wchar_t dir[MAX_PATH + 16], path[MAX_PATH * 2], stem[MAX_PATH] = L"hud";
    swprintf(dir, MAX_PATH + 16, L"%ls\\shots", g_exedir);
    CreateDirectoryW(dir, NULL);
    if (g_cur >= 0) { lstrcpynW(stem, g_files[g_cur], MAX_PATH); wchar_t *dot = wcsrchr(stem, L'.'); if (dot) *dot = 0; }
    uint32_t p = sim_pos_ms();
    swprintf(path, MAX_PATH * 2, L"%ls\\%ls_%02um%02us%ums.png", dir, stem, p / 60000, p / 1000 % 60, p % 1000 / 100);
    void *bmp = NULL; int st = GdipCreateBitmapFromScan0(w, h, w * 4, GP_PIXFMT_32RGB, (BYTE *)px, &bmp);
    if (!st && bmp) { st = GdipSaveImageToFile(bmp, path, &CLSID_PNG_ENC, NULL); GdipDisposeImage(bmp); }
    free(px);
    if (st) logf_w(TR("[sim] скриншот в буфере обмена; PNG сохранить не удалось (GDI+ %d): %ls\n", "[sim] screenshot is on the clipboard; PNG could not be saved (GDI+ %d): %ls\n"), st, path);
    else    logf_w(TR("[sim] скриншот: %ls (и в буфере обмена)\n", "[sim] screenshot: %ls (and on the clipboard)\n"), path);
}

/* ============================ управление ============================ */
static void toggle_play(void)
{
    if (!sim_loaded()) return;
    if (!sim_playing() && sim_pos_ms() >= sim_dur_ms()) sim_seek_ms(0);     /* доиграно до конца — с начала */
    sim_play(!sim_playing());
    update_play_button();
}

static void seek_rel(int ms)
{
    if (!sim_loaded()) return;
    int64_t p = (int64_t)sim_pos_ms() + ms;
    if (p < 0) p = 0;
    sim_seek_ms((uint32_t)p);
    InvalidateRect(g_tl, NULL, FALSE);
}

static void seek_maneuver(int dir)
{
    uint32_t n; const sim_man_t *m = sim_maneuvers(&n);
    uint32_t pos = sim_pos_ms();
    if (!sim_loaded() || !n) return;
    if (dir > 0) { for (uint32_t i = 0; i < n; i++) if (m[i].t > pos + 500) { sim_seek_ms(m[i].t); break; } }
    else         { for (uint32_t i = n; i-- > 0;) if (m[i].t + 3000 < pos) { sim_seek_ms(m[i].t); break; } }
    InvalidateRect(g_tl, NULL, FALSE);
}

static void switch_file(int d)
{
    if (g_cur < 0) return;
    bool was = sim_playing();
    if (load_index(g_cur + d, false)) sim_play(was);
    update_play_button();
}

static const wchar_t *plat_name(void) { return sim_platform() == SIM_PLATFORM_MQB ? L"MQB" : L"MLB-Evo"; }

static void update_title(void)
{
    wchar_t t[MAX_PATH + 96];
    if (g_cur >= 0) swprintf(t, MAX_PATH + 96, TR("HUD Sim (прошивка %hs, %ls) — %ls", "HUD Sim (firmware %hs, %ls) — %ls"), HUD_FW_VERSION_STRING, plat_name(), g_files[g_cur]);
    else            swprintf(t, MAX_PATH + 96, TR("HUD Sim (прошивка %hs, %ls)", "HUD Sim (firmware %hs, %ls)"), HUD_FW_VERSION_STRING, plat_name());
    SetWindowTextW(g_wnd, t);
}

static void set_platform(int p, bool announce)
{
    sim_set_platform(p);
    cfg_set_int(L"sim", L"platform", sim_platform());
    CheckMenuRadioItem(GetMenu(g_wnd), IDM_PLAT_MLB, IDM_PLAT_MQB, sim_platform() == SIM_PLATFORM_MQB ? IDM_PLAT_MQB : IDM_PLAT_MLB, MF_BYCOMMAND);
    update_title();
    if (announce) logf_w(TR("[sim] платформа декодера: %ls\n", "[sim] decoder platform: %ls\n"), plat_name());
    if (g_tl) InvalidateRect(g_tl, NULL, FALSE);
}

static int speed_index(void)
{
    int best = 2; double s = sim_speed();
    for (int i = 0; i < NSPEED; i++) if (k_speeds[i] == s) best = i;
    return best;
}

static void set_speed_index(int i)
{
    if (i < 0) i = 0;
    if (i >= NSPEED) i = NSPEED - 1;
    sim_set_speed(k_speeds[i]);
    SendMessageW(g_speed, CB_SETCURSEL, (WPARAM)i, 0);
    cfg_set_int(L"sim", L"speed", i);
}

/* ============================ размеры ============================ */
static void layout(void)
{
    RECT rc; GetClientRect(g_wnd, &rc);
    int W = rc.right, H = rc.bottom, m = S(8);
    g_band_h = SIM_H * g_zoom + 2 * S(10);
    g_hud.left = (W - SIM_W * g_zoom) / 2; if (g_hud.left < 0) g_hud.left = 0;
    g_hud.top = S(10); g_hud.right = g_hud.left + SIM_W * g_zoom; g_hud.bottom = g_hud.top + SIM_H * g_zoom;
    int y = g_band_h + S(6), x = m, bh = S(26);
    for (int i = 0; i < NBTN; i++) {
        MoveWindow(g_btn[i], x, y, S(k_btn[i].w), bh, TRUE);
        x += S(k_btn[i].w) + S(4);
        if (i == 1 || i == 4 || i == 6 || i == 8) x += S(8);
        if (i == 8) { MoveWindow(g_speed, x, y, S(66), S(300), TRUE); x += S(66) + S(12); }
    }
    MoveWindow(g_sound, x + S(4), y, S(64), bh, TRUE);
    y += bh + S(6);
    MoveWindow(g_tl, m, y, W - 2 * m, S(30), TRUE);
    y += S(30) + S(4);
    MoveWindow(g_st1, m, y, W - 2 * m, S(18), TRUE); y += S(18);
    MoveWindow(g_st2, m, y, W - 2 * m, S(18), TRUE); y += S(18) + S(4);
    int bot = H - y - m; if (bot < S(40)) bot = S(40);
    MoveWindow(g_list, m, y, S(170), bot, TRUE);
    MoveWindow(g_log, m + S(170) + S(6), y, W - 2 * m - S(170) - S(6), bot, TRUE);
    InvalidateRect(g_wnd, NULL, TRUE);
}

static int min_client_w(void) { int a = SIM_W * g_zoom + 2 * S(10), b = S(1010); return a > b ? a : b; }

static void fit_window(void)
{
    RECT r = { 0, 0, min_client_w(), SIM_H * g_zoom + 2 * S(10) + S(330) };
    AdjustWindowRectEx(&r, (DWORD)GetWindowLongPtrW(g_wnd, GWL_STYLE), TRUE, 0);
    SetWindowPos(g_wnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
}

static void set_zoom(int z)
{
    g_zoom = z < 1 ? 1 : z > 4 ? 4 : z;
    cfg_set_int(L"sim", L"zoom", g_zoom);
    CheckMenuRadioItem(GetMenu(g_wnd), IDM_ZOOM1, IDM_ZOOM4, IDM_ZOOM1 + g_zoom - 1, MF_BYCOMMAND);
    fit_window();
    layout();
}

/* ============================ шкала времени ============================ */
static void tl_seek(HWND h, int x)
{
    RECT rc; GetClientRect(h, &rc);
    if (!sim_loaded() || rc.right < 2) return;
    if (x < 0) x = 0;
    if (x > rc.right - 1) x = rc.right - 1;
    sim_seek_ms((uint32_t)((double)x / (rc.right - 1) * sim_dur_ms()));
    InvalidateRect(h, NULL, FALSE);
}

static void fill(HDC dc, int x0, int y0, int x1, int y1, COLORREF c)
{
    RECT r = { x0, y0, x1, y1 };
    SetBkColor(dc, c);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &r, NULL, 0, NULL);
}

static LRESULT CALLBACK tl_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        int W = rc.right, H = rc.bottom;
        fill(dc, 0, 0, W, H, RGB(38, 38, 38));
        uint32_t dur = sim_dur_ms();
        if (sim_loaded() && dur && W > 2) {
            double k = (double)(W - 1) / dur;
            int px = (int)(sim_pos_ms() * k);
            fill(dc, 0, 0, px, H, RGB(62, 62, 70));                           /* пройдено */
            uint32_t n; const sim_span_t *sp = sim_nav_spans(&n);             /* ведение по маршруту — зелёная полоса */
            for (uint32_t i = 0; i < n; i++) {
                uint32_t t1 = sp[i].t1 == 0xFFFFFFFFu ? dur : sp[i].t1;
                fill(dc, (int)(sp[i].t0 * k), H - S(7), (int)(t1 * k) + 1, H - S(2), RGB(60, 170, 60));
            }
            const sim_man_t *m = sim_maneuvers(&n);                           /* смены манёвра — оранжевые метки */
            for (uint32_t i = 0; i < n; i++) { int x = (int)(m[i].t * k); fill(dc, x, S(3), x + (S(2) > 1 ? S(2) : 1), H - S(9), RGB(255, 150, 30)); }
            fill(dc, px, 0, px + S(2), H, RGB(255, 255, 255));                /* курсор */
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: SetCapture(h); tl_seek(h, (short)LOWORD(lp)); return 0;
    case WM_MOUSEMOVE:   if (GetCapture() == h) tl_seek(h, (short)LOWORD(lp)); return 0;
    case WM_LBUTTONUP:   if (GetCapture() == h) ReleaseCapture(); SetFocus(g_wnd); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* ============================ строки состояния ============================ */
static void set_text(HWND h, const wchar_t *s)
{
    wchar_t cur[600];
    GetWindowTextW(h, cur, 600);
    if (wcscmp(cur, s)) SetWindowTextW(h, s);
}

static const wchar_t *unit_name(uint8_t u)
{
    static const wchar_t *n_ru[] = { L"м", L"км", L"ярд", L"фут", L"миль", L"¼ мили" };
    static const wchar_t *n_en[] = { L"m", L"km", L"yd", L"ft", L"mi", L"¼ mi" };
    const wchar_t **n = g_ui ? n_en : n_ru;
    return u < 6 ? n[u] : L"?";
}

static void update_status(void)
{
    wchar_t a[600], b[600]; int o;
    HudData d; hud_data_snapshot(&d);
    {   /* «нет фикса» кадр 0x486 приходит с маркерными значениями (широта 134.2…) — их не берём, держим последнюю годную позицию */
        static double s_lat, s_lon; static int s_have;
        double la = d.gps_lat_e6 / 1e6, lo = d.gps_lon_e6 / 1e6;
        if ((d.valid & V_GPS) && d.gps_fix >= 3 && la > -90 && la < 90 && lo > -180 && lo < 180 && (d.gps_lat_e6 || d.gps_lon_e6)) { s_lat = la; s_lon = lo; s_have = 1; }
        map_update(sim_loaded(), sim_loaded() && s_have, s_lat, s_lon, ((d.valid & V_HDG) && d.gps_hdg_x10 != 0xFFFF) ? d.gps_hdg_x10 : -1,
                   (d.valid & V_SPEED) ? d.speed_kmh : 0, (d.valid & V_GPS) ? d.gps_sat : 0, (d.valid & V_GPS) ? d.gps_fix : 0, sim_pos_ms()); }
    if (!sim_loaded()) {
        set_text(g_st1, TR("Лог не открыт. Откройте файл can_log_NNNN.txt или папку поездки (кнопки, меню «Файл» или перетащите в окно).", "No log open. Open a can_log_NNNN.txt file or a trip folder (buttons, File menu, or drag it into the window)."));
        set_text(g_st2, TR("Язык, единицы, знаки, камеры, допуск, громкость — кнопка «Настройки HUD» (клавиша M).", "Language, units, signs, cameras, tolerance, volume — the \"HUD settings\" button (key M)."));
        return;
    }
    uint32_t p = sim_pos_ms(), du = sim_dur_ms();
    o = swprintf(a, 600, TR("%ls   [%ls]   %02u:%04.1f / %02u:%02u   ×%g   кадр %u / %u", "%ls   [%ls]   %02u:%04.1f / %02u:%02u   ×%g   frame %u / %u"), g_cur >= 0 ? g_files[g_cur] : L"", plat_name(), p / 60000, (p % 60000) / 1000.0,
                 du / 60000, du / 1000 % 60, sim_speed(), sim_frames_pos(), sim_frames_total());
    {   sim_dt_t t;                                /* время машины (кадр 0x6B2 в логе) */
        if (sim_car_time(&t)) o += swprintf(a + o, 600 - o, TR("   часы машины %04u-%02u-%02u %02u:%02u:%02u", "   car clock %04u-%02u-%02u %02u:%02u:%02u"), t.year, t.mon, t.day, t.hour, t.min, t.sec);
        else                  o += swprintf(a + o, 600 - o, TR("   часов машины в логе нет", "   no car clock in the log"));
    }
    if (sim_backlight >= 0) o += swprintf(a + o, 600 - o, TR("   подсветка %d/255", "   backlight %d/255"), sim_backlight);
    set_text(g_st1, a);

    o = 0;
    if (d.valid & V_SPEED) o += swprintf(b + o, 600 - o, TR("%u км/ч", "%u km/h"), d.speed_kmh); else o += swprintf(b + o, 600 - o, TR("скорости нет", "no speed"));
    if (d.valid & V_MAN) {
        o += swprintf(b + o, 600 - o, TR("   навигация: %ls, манёвр 0x%02X / 0x%02X (%u°)", "   navigation: %ls, maneuver 0x%02X / 0x%02X (%u°)"), d.rg_active == 1 ? TR("ведёт", "guiding") : d.rg_active == 2 ? TR("пауза", "paused") : TR("выкл", "off"),
                      d.man_main, d.man_dir, d.man_dir * 360u / 256u);
        if (d.man2_main) o += swprintf(b + o, 600 - o, TR(", затем 0x%02X / 0x%02X", ", then 0x%02X / 0x%02X"), d.man2_main, d.man2_dir);
    } else o += swprintf(b + o, 600 - o, TR("   навигация: нет данных", "   navigation: no data"));
    if (d.valid & V_MDIST) {
        o += swprintf(b + o, 600 - o, TR("   через %g %ls", "   in %g %ls"), d.man_dist_x10 / 10.0, unit_name(d.man_unit));
        if (d.bargraph != 0xFF) o += swprintf(b + o, 600 - o, TR(" (бар %u%%)", " (bar %u%%)"), d.bargraph);
    }
    if (d.valid & V_DEST) o += swprintf(b + o, 600 - o, TR("   до цели %g %ls", "   to destination %g %ls"), d.dest_dist_x10 / 10.0, unit_name(d.dest_unit));
    if (d.valid & V_TTE)  o += swprintf(b + o, 600 - o, d.tte_type ? TR("   прибытие %02u:%02u", "   arrival %02u:%02u") : TR("   в пути %u:%02u", "   travel time %u:%02u"), d.tte_h, d.tte_min);
    if ((d.valid & V_STREET) && d.street[0]) {     /* название улицы (UTF-8) */
        wchar_t w[80];
        if (MultiByteToWideChar(CP_UTF8, 0, d.street, -1, w, 80) > 0) o += swprintf(b + o, 600 - o, TR("   улица: %ls", "   street: %ls"), w);
    }
    if (d.valid & V_GPS) {
        o += swprintf(b + o, 600 - o, L"   GNSS %.5f, %.5f", d.gps_lat_e6 / 1e6, d.gps_lon_e6 / 1e6);
        if ((d.valid & V_HDG) && d.gps_hdg_x10 != 0xFFFF) o += swprintf(b + o, 600 - o, TR(" курс %u°", " heading %u°"), d.gps_hdg_x10 / 10);
    }
    set_text(g_st2, b);

    /* журнал: смена манёвра — с сырым сообщением BAP 0x17, чтобы разбирать новые коды */
    static uint8_t last[6]; static bool have;
    uint8_t now[6] = { (uint8_t)((d.valid & V_MAN) != 0), d.rg_active, d.man_main, d.man_dir, d.man2_main, d.man2_dir };
    if (!have || memcmp(last, now, 6)) {
        have = true; memcpy(last, now, 6);
        if (d.valid & V_MAN) {
            wchar_t hx[3 * 32 + 1] = L""; int k = 0;
            for (int i = 0; i < d.man_raw_len && i < 32; i++) k += swprintf(hx + k, 4, L"%02X ", d.man_raw[i]);
            logf_w(TR("[sim] навигация %u, манёвр main 0x%02X dir 0x%02X (%u°) z 0x%02X, боковых %u, затем 0x%02X/0x%02X; 0x17[%u]: %ls\n", "[sim] navigation %u, maneuver main 0x%02X dir 0x%02X (%u°) z 0x%02X, side %u, then 0x%02X/0x%02X; 0x17[%u]: %ls\n"),
                   d.rg_active, d.man_main, d.man_dir, d.man_dir * 360u / 256u, d.man_z, d.man_side_n, d.man2_main, d.man2_dir, d.man_raw_len, hx);
        }
    }
}

/* ============================ окно настроек HUD ============================ */
/* В машине эти настройки приходят из приложения на Android (BLE); здесь — то же самое в отдельном окне.
   Значения хранятся в hud_sim.ini, секция [hud]. */
enum { IDS_LANG = 400, IDS_UNITS, IDS_PSD, IDS_VZE, IDS_CAMS, IDS_BEEP, IDS_STREET,
       IDS_TOL, IDS_VOL, IDS_LDR, IDS_LDRMV, IDS_DEFAULT,
       IDS_L_TOL = 430, IDS_L_VOL, IDS_L_LDRMV };

static HWND g_cfg;
static sim_settings_t g_set;

static void settings_load(void)
{
    sim_settings_t d; sim_settings_defaults(&d);
    g_set.lang = (uint8_t)(GetPrivateProfileIntW(L"hud", L"lang", d.lang, g_ini) ? 1 : 0);
    g_set.units = (uint8_t)(GetPrivateProfileIntW(L"hud", L"units", d.units, g_ini) ? 1 : 0);
    g_set.psd = GetPrivateProfileIntW(L"hud", L"psd", d.psd, g_ini) != 0;
    g_set.vze = GetPrivateProfileIntW(L"hud", L"vze", d.vze, g_ini) != 0;
    g_set.cams = GetPrivateProfileIntW(L"hud", L"cams", d.cams, g_ini) != 0;
    g_set.cam_beep = GetPrivateProfileIntW(L"hud", L"beep", d.cam_beep, g_ini) != 0;
    g_set.ldr_on = GetPrivateProfileIntW(L"hud", L"ldr", d.ldr_on, g_ini) != 0;
    g_set.street = GetPrivateProfileIntW(L"hud", L"street", d.street, g_ini) != 0;
    int v;
    v = (int)GetPrivateProfileIntW(L"hud", L"tol", d.tol, g_ini);      g_set.tol = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
    v = (int)GetPrivateProfileIntW(L"hud", L"vol", d.volume, g_ini);   g_set.volume = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
    v = (int)GetPrivateProfileIntW(L"hud", L"ldrmv", d.ldr_mv, g_ini); g_set.ldr_mv = v < 0 ? 0 : v > 3300 ? 3300 : v;
}

static void settings_save(void)
{
    cfg_set_int(L"hud", L"lang", g_set.lang);    cfg_set_int(L"hud", L"units", g_set.units);
    cfg_set_int(L"hud", L"psd", g_set.psd);      cfg_set_int(L"hud", L"vze", g_set.vze);
    cfg_set_int(L"hud", L"cams", g_set.cams);    cfg_set_int(L"hud", L"beep", g_set.cam_beep);
    cfg_set_int(L"hud", L"tol", g_set.tol);
    cfg_set_int(L"hud", L"vol", g_set.volume);   cfg_set_int(L"hud", L"ldr", g_set.ldr_on); cfg_set_int(L"hud", L"street", g_set.street);
    cfg_set_int(L"hud", L"ldrmv", g_set.ldr_mv);
}

static HWND cfg_ctl(int id) { return GetDlgItem(g_cfg, id); }

static void cfg_labels(void)
{
    wchar_t t[96];
    swprintf(t, 96, TR("Допуск превышения скорости: %d км/ч", "Speed alert tolerance: %d km/h"), g_set.tol);  SetWindowTextW(cfg_ctl(IDS_L_TOL), t);
    swprintf(t, 96, TR("Громкость: %d %% (на ПК условная)", "Volume: %d %% (nominal on PC)"), g_set.volume); SetWindowTextW(cfg_ctl(IDS_L_VOL), t);
    swprintf(t, 96, TR("Напряжение на датчике света: %d мВ", "Light sensor voltage: %d mV"), g_set.ldr_mv); SetWindowTextW(cfg_ctl(IDS_L_LDRMV), t);
}

static void cfg_to_ui(void)
{
    SendMessageW(cfg_ctl(IDS_LANG), CB_SETCURSEL, g_set.lang, 0);
    SendMessageW(cfg_ctl(IDS_UNITS), CB_SETCURSEL, g_set.units, 0);
    struct { int id; uint8_t v; } cb[] = { { IDS_PSD, g_set.psd }, { IDS_VZE, g_set.vze },
                                           { IDS_CAMS, g_set.cams }, { IDS_BEEP, g_set.cam_beep }, { IDS_STREET, g_set.street }, { IDS_LDR, g_set.ldr_on } };
    for (size_t i = 0; i < sizeof cb / sizeof cb[0]; i++) SendMessageW(cfg_ctl(cb[i].id), BM_SETCHECK, cb[i].v ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(cfg_ctl(IDS_TOL), TBM_SETPOS, TRUE, g_set.tol);
    SendMessageW(cfg_ctl(IDS_VOL), TBM_SETPOS, TRUE, g_set.volume);
    SendMessageW(cfg_ctl(IDS_LDRMV), TBM_SETPOS, TRUE, g_set.ldr_mv);
    cfg_labels();
}

static bool cfg_chk(int id) { return SendMessageW(cfg_ctl(id), BM_GETCHECK, 0, 0) == BST_CHECKED; }

static void cfg_from_ui(void)
{
    g_set.lang = (uint8_t)(SendMessageW(cfg_ctl(IDS_LANG), CB_GETCURSEL, 0, 0) == 1);
    g_set.units = (uint8_t)(SendMessageW(cfg_ctl(IDS_UNITS), CB_GETCURSEL, 0, 0) == 1);
    g_set.psd = cfg_chk(IDS_PSD); g_set.vze = cfg_chk(IDS_VZE);
    g_set.cams = cfg_chk(IDS_CAMS); g_set.cam_beep = cfg_chk(IDS_BEEP); g_set.street = cfg_chk(IDS_STREET); g_set.ldr_on = cfg_chk(IDS_LDR);
    g_set.tol = (uint8_t)SendMessageW(cfg_ctl(IDS_TOL), TBM_GETPOS, 0, 0);
    g_set.volume = (uint8_t)SendMessageW(cfg_ctl(IDS_VOL), TBM_GETPOS, 0, 0);
    g_set.ldr_mv = (int)SendMessageW(cfg_ctl(IDS_LDRMV), TBM_GETPOS, 0, 0);
}

static void cfg_changed(void)
{
    cfg_from_ui();
    cfg_labels();
    sim_settings_apply(&g_set);
    settings_save();
}

static LRESULT CALLBACK cfg_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id == IDS_DEFAULT) { sim_settings_defaults(&g_set); cfg_to_ui(); sim_settings_apply(&g_set); settings_save(); }
        else if (id == IDS_LANG || id == IDS_UNITS) { if (code == CBN_SELCHANGE) cfg_changed(); }
        else if (id >= IDS_PSD && id <= IDS_STREET) cfg_changed();
        else if (id == IDS_LDR) cfg_changed();
        return 0;
    }
    case WM_HSCROLL:
        cfg_changed();
        if (LOWORD(wp) == TB_ENDTRACK && (HWND)lp == cfg_ctl(IDS_VOL) && g_set.volume) hud_sound_play(HUD_SND_BEEP);
        return 0;
    case WM_CLOSE: ShowWindow(h, SW_HIDE); SetFocus(g_wnd); return 0;     /* окно не уничтожается — настройки остаются */
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static HWND cfg_mk(const wchar_t *cls, const wchar_t *text, DWORD style, int id, int x, int y, int w, int hgt)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, S(x), S(y), S(w), S(hgt), g_cfg, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void cfg_trackbar(int id, int lblid, int y, int lo, int hi, int page)
{
    cfg_mk(L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_NOPREFIX, lblid, 14, y, 350, 16);
    HWND t = cfg_mk(L"msctls_trackbar32", L"", TBS_HORZ | TBS_AUTOTICKS | WS_TABSTOP, id, 8, y + 16, 366, 28);
    SendMessageW(t, TBM_SETRANGE, TRUE, MAKELONG(lo, hi));
    SendMessageW(t, TBM_SETPAGESIZE, 0, page);
}

static void settings_window(bool show)
{
    if (!g_cfg) {
        WNDCLASSW wc; memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = cfg_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"HudSimSettings";
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        RegisterClassW(&wc);
        RECT r = { 0, 0, S(388), S(454) };
        AdjustWindowRectEx(&r, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW);
        g_cfg = CreateWindowExW(WS_EX_TOOLWINDOW, L"HudSimSettings", TR("Настройки HUD (как на Wi-Fi-портале)", "HUD settings (as on the Wi-Fi portal)"), WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
                                CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, g_wnd, NULL, GetModuleHandleW(NULL), NULL);
        if (!g_cfg) return;
        cfg_mk(L"STATIC", TR("Язык", "HUD language"), SS_LEFTNOWORDWRAP, 0, 14, 16, 110, 18);
        HWND c = cfg_mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, IDS_LANG, 130, 12, 230, 200);
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"Русский"); SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"English");
        cfg_mk(L"STATIC", TR("Единицы", "Units"), SS_LEFTNOWORDWRAP, 0, 14, 46, 110, 18);
        c = cfg_mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, IDS_UNITS, 130, 42, 230, 200);
        SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)TR("км/ч, км, м", "km/h, km, m")); SendMessageW(c, CB_ADDSTRING, 0, (LPARAM)L"mph, mi, ft");
        cfg_mk(L"BUTTON", TR("Знаки из PSD (прогноз маршрута MIB)", "Signs from PSD (MIB route preview)"), BS_AUTOCHECKBOX | WS_TABSTOP, IDS_PSD, 14, 76, 350, 20);
        cfg_mk(L"BUTTON", TR("Знаки из VZE (камера / карта приборки)", "Signs from VZE (camera / cluster map)"), BS_AUTOCHECKBOX | WS_TABSTOP, IDS_VZE, 14, 100, 350, 20);
        cfg_mk(L"BUTTON", TR("Камеры (нужен cams.bin)", "Cameras (needs cams.bin)"), BS_AUTOCHECKBOX | WS_TABSTOP, IDS_CAMS, 14, 124, 350, 20);
        cfg_mk(L"BUTTON", TR("Сигнал при появлении камеры", "Beep when a camera appears"), BS_AUTOCHECKBOX | WS_TABSTOP, IDS_BEEP, 14, 148, 350, 20);
        cfg_mk(L"BUTTON", TR("Название улицы над стрелкой", "Street name above the arrow"), BS_AUTOCHECKBOX | WS_TABSTOP, IDS_STREET, 14, 172, 350, 20);
        cfg_trackbar(IDS_TOL, IDS_L_TOL, 200, 0, 100, 5);
        cfg_trackbar(IDS_VOL, IDS_L_VOL, 246, 0, 100, 5);
        cfg_mk(L"BUTTON", TR("Датчик света (фоторезистор) включён", "Light sensor (photoresistor) enabled"), BS_AUTOCHECKBOX | WS_TABSTOP, IDS_LDR, 14, 296, 350, 20);
        cfg_trackbar(IDS_LDRMV, IDS_L_LDRMV, 322, 0, 3300, 100);
        cfg_mk(L"BUTTON", TR("По умолчанию (из hud_config.h)", "Defaults (from hud_config.h)"), BS_PUSHBUTTON | WS_TABSTOP, IDS_DEFAULT, 14, 408, 230, 28);
        cfg_to_ui();
    }
    ShowWindow(g_cfg, show ? SW_SHOWNORMAL : SW_HIDE);
    if (show) SetForegroundWindow(g_cfg);
}

/* ============================ такт ============================ */
/* ---- окно карты: сервер на 127.0.0.1 + отдельное окно браузера (Edge/Chrome в режиме приложения) ---- */
static void open_map(void)
{
    int port = map_server_port();
    if (!port) { logf_w(TR("[карта] не удалось запустить локальный сервер\n", "[map] could not start the local server\n")); return; }
    wchar_t url[96]; swprintf(url, 96, L"http://127.0.0.1:%d/%ls", port, g_ui ? L"?lang=en" : L"");
    wchar_t arg[160]; swprintf(arg, 160, L"--app=%ls --window-size=1000,720", url);
    static const wchar_t *k_br[] = { L"msedge.exe", L"chrome.exe" };
    for (int i = 0; i < 2; i++) {
        HINSTANCE r = ShellExecuteW(NULL, L"open", k_br[i], arg, NULL, SW_SHOWNORMAL);
        if ((INT_PTR)r > 32) return;
    }
    ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);      /* браузер по умолчанию, во вкладке */
}

static void tick(void)
{
    static LARGE_INTEGER freq, last; static double acc; static DWORD last_ui;
    if (g_in_tick) return;
    g_in_tick = true;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!freq.QuadPart) { QueryPerformanceFrequency(&freq); last = now; }
    acc += (double)(now.QuadPart - last.QuadPart) * 1000.0 / (double)freq.QuadPart;
    last = now;
    if (acc > 250) acc = 250;                      /* окно таскали / отладчик — не догонять рывком */
    uint32_t dt = (uint32_t)acc;
    if (dt) {
        acc -= dt;
        sim_step(dt);
        if (sim_take_eof()) {                      /* файл кончился — следующий файл той же папки */
            if (!(g_cur + 1 < g_nfiles && load_index(g_cur + 1, true))) { sim_play(false); logf_w(TR("[sim] конец лога\n", "[sim] end of log\n")); }
            update_play_button();
        }
        if (sim_fb_dirty) { sim_fb_dirty = false; InvalidateRect(g_wnd, &g_hud, FALSE); }
        DWORD t = GetTickCount();
        if (t - last_ui >= 100) { last_ui = t; update_status(); if (sim_playing()) InvalidateRect(g_tl, NULL, FALSE); }
    }
    g_in_tick = false;
}

/* ============================ главное окно ============================ */
static HMENU build_menu(void)
{
    HMENU mb = CreateMenu(), mf = CreatePopupMenu(), mv = CreatePopupMenu(), mp = CreatePopupMenu(), ml = CreatePopupMenu();
    AppendMenuW(mp, MF_STRING, IDM_PLAT_MLB, L"MLB-Evo (Audi A4 B9, I-CAN)");
    AppendMenuW(mp, MF_STRING, IDM_PLAT_MQB, L"MQB (Comfort CAN)");
    AppendMenuW(mf, MF_STRING, IDC_OPENF, TR("Открыть лог…\tCtrl+O", "Open log…\tCtrl+O"));
    AppendMenuW(mf, MF_STRING, IDC_OPEND, TR("Открыть папку поездки…", "Open trip folder…"));
    AppendMenuW(mf, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mf, MF_STRING, IDM_CAMS, TR("Загрузить базу камер (cams.bin)…", "Load camera database (cams.bin)…"));
    AppendMenuW(mf, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mf, MF_STRING, IDC_SHOT, TR("Скриншот (PNG + буфер обмена)\tS", "Screenshot (PNG + clipboard)\tS"));
    AppendMenuW(mf, MF_STRING, IDM_CLEARLOG, TR("Очистить журнал", "Clear log"));
    AppendMenuW(mf, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mf, MF_STRING, IDM_EXIT, TR("Выход", "Exit"));
    AppendMenuW(mv, MF_STRING, IDM_SETTINGS, TR("Настройки HUD…\tM", "HUD settings…\tM"));
    AppendMenuW(mv, MF_STRING, IDM_MAP, TR("Карта (Яндекс / 2ГИС)…\tK", "Map (Yandex / 2GIS)…\tK"));
    AppendMenuW(mv, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mv, MF_STRING, IDM_ZOOM1, TR("Экран HUD 1× (428×142)", "HUD screen 1× (428×142)"));
    AppendMenuW(mv, MF_STRING, IDM_ZOOM2, TR("Экран HUD 2×", "HUD screen 2×"));
    AppendMenuW(mv, MF_STRING, IDM_ZOOM3, TR("Экран HUD 3×", "HUD screen 3×"));
    AppendMenuW(mv, MF_STRING, IDM_ZOOM4, TR("Экран HUD 4×", "HUD screen 4×"));
    AppendMenuW(ml, MF_STRING, IDM_LANG_RU, L"Русский");
    AppendMenuW(ml, MF_STRING, IDM_LANG_EN, L"English");
    AppendMenuW(mv, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mv, MF_POPUP, (UINT_PTR)ml, TR("Язык интерфейса", "Interface language"));
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mf, TR("Файл", "File"));
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mp, TR("Платформа", "Platform"));
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mv, TR("Вид", "View"));
    CheckMenuRadioItem(ml, IDM_LANG_RU, IDM_LANG_EN, g_ui ? IDM_LANG_EN : IDM_LANG_RU, MF_BYCOMMAND);
    CheckMenuRadioItem(mb, IDM_ZOOM1, IDM_ZOOM4, IDM_ZOOM1 + g_zoom - 1, MF_BYCOMMAND);
    return mb;
}

/* переключение языка интерфейса «на лету»: подписи кнопок, меню, заголовок, окно настроек (пересоздаётся) */
static void apply_ui_lang(void)
{
    for (int i = 0; i < NBTN; i++) SetWindowTextW(g_btn[i], g_ui ? k_btn[i].en : k_btn[i].ru);
    update_play_button();
    SetWindowTextW(g_sound, TR("Звук", "Sound"));
    HMENU old = GetMenu(g_wnd), mb = build_menu();
    SetMenu(g_wnd, mb);
    if (old) DestroyMenu(old);
    CheckMenuRadioItem(mb, IDM_PLAT_MLB, IDM_PLAT_MQB, sim_platform() == SIM_PLATFORM_MQB ? IDM_PLAT_MQB : IDM_PLAT_MLB, MF_BYCOMMAND);
    DrawMenuBar(g_wnd);
    update_title();
    if (g_cfg) { bool vis = IsWindowVisible(g_cfg) != 0; DestroyWindow(g_cfg); g_cfg = NULL; if (vis) settings_window(true); }
    layout();
}

static void set_ui_lang(int en)
{
    g_ui = en ? 1 : 0;
    cfg_set_int(L"ui", L"lang", g_ui);
    apply_ui_lang();
}

static int detect_ui_lang(void)
{
    int l = (int)GetPrivateProfileIntW(L"ui", L"lang", -1, g_ini);
    if (l == 0 || l == 1) return l;
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) { case LANG_RUSSIAN: case LANG_UKRAINIAN: case LANG_BELARUSIAN: return 0; }
    return 1;
}

static void on_command(int id, int code)
{
    wchar_t path[MAX_PATH];
    switch (id) {
    case IDC_OPENF: if (pick_file(TR("Логи сниффера (*.txt;*.log)\0*.txt;*.log\0Все файлы\0*.*\0", "Sniffer logs (*.txt;*.log)\0*.txt;*.log\0All files\0*.*\0"), path)) open_path(path, true); break;
    case IDC_OPEND: if (pick_folder(path)) open_path(path, true); break;
    case IDC_PREV:  switch_file(-1); break;
    case IDC_NEXT:  switch_file(+1); break;
    case IDC_PLAY:  toggle_play(); break;
    case IDC_B10:   seek_rel(-10000); break;
    case IDC_F10:   seek_rel(+10000); break;
    case IDC_PMAN:  seek_maneuver(-1); break;
    case IDC_NMAN:  seek_maneuver(+1); break;
    case IDC_SETTINGS: case IDM_SETTINGS: settings_window(true); return;
    case IDC_MAP: case IDM_MAP: open_map(); return;
    case IDC_SHOT:  screenshot(); break;
    case IDC_SOUND: g_sound_on = SendMessageW(g_sound, BM_GETCHECK, 0, 0) == BST_CHECKED; cfg_set_int(L"sim", L"sound", g_sound_on);
                    if (!g_sound_on) PlaySoundW(NULL, NULL, 0);
                    break;
    case IDC_SPEED: if (code == CBN_SELCHANGE) set_speed_index((int)SendMessageW(g_speed, CB_GETCURSEL, 0, 0));
                    if (code != CBN_SELCHANGE && code != CBN_CLOSEUP) return;
                    break;
    case IDC_LIST:  if (code == LBN_SELCHANGE) {
                        int i = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
                        if (i >= 0 && i != g_cur) { bool was = sim_playing() || !sim_loaded(); if (load_index(i, false)) sim_play(was); update_play_button(); }
                    }
                    return;                        /* фокус остаётся в списке — стрелками можно листать файлы */
    case IDC_LOG:   return;
    case IDM_CAMS:  if (pick_file(TR("База камер (cams.bin)\0*.bin\0Все файлы\0*.*\0", "Camera database (cams.bin)\0*.bin\0All files\0*.*\0"), path) && load_cams(path, false))
                        WritePrivateProfileStringW(L"sim", L"cams", path, g_ini);
                    break;
    case IDM_CLEARLOG: SetWindowTextW(g_log, L""); s_log_bol = true; break;
    case IDM_EXIT:  DestroyWindow(g_wnd); return;
    case IDM_PLAT_MLB: set_platform(SIM_PLATFORM_MLB_EVO, true); break;
    case IDM_PLAT_MQB: set_platform(SIM_PLATFORM_MQB, true); break;
    case IDM_LANG_RU: set_ui_lang(0); return;
    case IDM_LANG_EN: set_ui_lang(1); return;
    case IDM_ZOOM1: case IDM_ZOOM2: case IDM_ZOOM3: case IDM_ZOOM4: set_zoom(id - IDM_ZOOM1 + 1); break;
    default: return;
    }
    SetFocus(g_wnd);                               /* чтобы пробел и стрелки снова управляли проигрыванием */
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        COLORREF bg = RGB(24, 24, 24);
        fill(dc, 0, 0, rc.right, g_hud.top, bg);
        fill(dc, 0, g_hud.bottom, rc.right, g_band_h, bg);
        fill(dc, 0, g_hud.top, g_hud.left, g_hud.bottom, bg);
        fill(dc, g_hud.right, g_hud.top, rc.right, g_hud.bottom, bg);
        BITMAPINFO bi; memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = SIM_W; bi.bmiHeader.biHeight = -SIM_H;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, g_hud.left, g_hud.top, SIM_W * g_zoom, SIM_H * g_zoom, 0, 0, SIM_W, SIM_H, sim_fb, &bi, DIB_RGB_COLORS, SRCCOPY);
        RECT rest = { 0, g_band_h, rc.right, rc.bottom };
        FillRect(dc, &rest, GetSysColorBrush(COLOR_BTNFACE));
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SIZE: if (wp != SIZE_MINIMIZED && g_log) layout(); return 0;
    case WM_GETMINMAXINFO: {
        if (!g_wnd) break;
        RECT r = { 0, 0, min_client_w(), SIM_H * g_zoom + 2 * S(10) + S(200) };
        AdjustWindowRectEx(&r, (DWORD)GetWindowLongPtrW(h, GWL_STYLE), TRUE, 0);
        ((MINMAXINFO *)lp)->ptMinTrackSize.x = r.right - r.left;
        ((MINMAXINFO *)lp)->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_TIMER: tick(); return 0;               /* такт идёт и пока окно таскают или открыто меню */
    case WM_LBUTTONDOWN: SetFocus(h); return 0;
    case WM_KEYDOWN: {
        bool shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
        switch (wp) {
        case VK_SPACE: toggle_play(); break;
        case VK_LEFT:  seek_rel(shift ? -60000 : -10000); break;
        case VK_RIGHT: seek_rel(shift ? +60000 : +10000); break;
        case VK_UP:    set_speed_index(speed_index() + 1); break;
        case VK_DOWN:  set_speed_index(speed_index() - 1); break;
        case VK_PRIOR: switch_file(-1); break;
        case VK_NEXT:  switch_file(+1); break;
        case 'N': seek_maneuver(+1); break;
        case 'P': seek_maneuver(-1); break;
        case 'M': settings_window(true); break;
        case 'K': open_map(); break;
        case 'S': screenshot(); break;
        case 'O': if (ctrl) on_command(IDC_OPENF, 0); break;
        }
        return 0;
    }
    case WM_COMMAND: on_command(LOWORD(wp), HIWORD(wp)); return 0;
    case WM_DROPFILES: {
        wchar_t path[MAX_PATH];
        if (DragQueryFileW((HDROP)wp, 0, path, MAX_PATH)) {
            const wchar_t *bn = base_name(path);
            if (!lstrcmpiW(bn, L"cams.bin")) { if (load_cams(path, false)) WritePrivateProfileStringW(L"sim", L"cams", path, g_ini); }
            else open_path(path, true);
        }
        DragFinish((HDROP)wp);
        return 0;
    }
    case WM_DESTROY: PlaySoundW(NULL, NULL, 0); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static HWND mk(const wchar_t *cls, const wchar_t *text, DWORD style, DWORD ex, int id, HFONT font)
{
    HWND w = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(w, WM_SETFONT, (WPARAM)font, TRUE);
    return w;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show)
{
    (void)prev; (void)cmdline;
    SetProcessDPIAware();
    { HDC dc = GetDC(NULL); g_dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(NULL, dc); if (g_dpi < 96) g_dpi = 96; }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    InitCommonControls();
    { ULONG_PTR tok; GpStartupInput in = { 1, NULL, FALSE, FALSE }; GdiplusStartup(&tok, &in, NULL); }

    GetModuleFileNameW(NULL, g_exedir, MAX_PATH);
    { wchar_t *s = wcsrchr(g_exedir, L'\\'); if (s) *s = 0; }
    swprintf(g_ini, MAX_PATH + 16, L"%ls\\hud_sim.ini", g_exedir);
    g_ui = detect_ui_lang();
    g_zoom = (int)GetPrivateProfileIntW(L"sim", L"zoom", g_dpi >= 168 ? 3 : 2, g_ini);
    if (g_zoom < 1 || g_zoom > 4) g_zoom = 2;
    g_sound_on = GetPrivateProfileIntW(L"sim", L"sound", 1, g_ini) != 0;

    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wnd_proc; wc.hInstance = inst; wc.lpszClassName = L"HudSimMain";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    RegisterClassW(&wc);
    wc.lpfnWndProc = tl_proc; wc.lpszClassName = L"HudSimTimeline"; wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_HAND); wc.hIcon = NULL;
    RegisterClassW(&wc);

    HMENU mb = build_menu();

    g_wnd = CreateWindowExW(WS_EX_ACCEPTFILES, L"HudSimMain", g_ui ? L"HUD Sim (firmware " HUD_FW_VERSION_STRING L")" : L"HUD Sim (прошивка " HUD_FW_VERSION_STRING L")",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700, NULL, mb, inst, NULL);
    if (!g_wnd) return 1;
    CheckMenuRadioItem(mb, IDM_ZOOM1, IDM_ZOOM4, IDM_ZOOM1 + g_zoom - 1, MF_BYCOMMAND);

    NONCLIENTMETRICSW ncm; memset(&ncm, 0, sizeof ncm); ncm.cbSize = sizeof ncm;
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0)) g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (!g_font) g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    g_mono = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    for (int i = 0; i < NBTN; i++) g_btn[i] = mk(L"BUTTON", g_ui ? k_btn[i].en : k_btn[i].ru, BS_PUSHBUTTON, 0, k_btn[i].id, g_font);
    g_speed = mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 0, IDC_SPEED, g_font);
    for (int i = 0; i < NSPEED; i++) { wchar_t t[16]; swprintf(t, 16, L"×%g", k_speeds[i]); SendMessageW(g_speed, CB_ADDSTRING, 0, (LPARAM)t); }
    g_sound = mk(L"BUTTON", TR("Звук", "Sound"), BS_AUTOCHECKBOX, 0, IDC_SOUND, g_font);
    SendMessageW(g_sound, BM_SETCHECK, g_sound_on ? BST_CHECKED : BST_UNCHECKED, 0);
    g_tl  = mk(L"HudSimTimeline", L"", 0, 0, 0, g_font);
    g_st1 = mk(L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_NOPREFIX, 0, 0, g_font);
    g_st2 = mk(L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_NOPREFIX, 0, 0, g_font);
    g_list = mk(L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_BORDER, 0, IDC_LIST, g_font);
    g_log  = mk(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, WS_EX_CLIENTEDGE, IDC_LOG, g_mono);
    SendMessageW(g_log, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
    set_speed_index((int)GetPrivateProfileIntW(L"sim", L"speed", 2, g_ini));
    fit_window();
    layout();

    sim_init();
    settings_load();
    sim_settings_apply(&g_set);
    set_platform((int)GetPrivateProfileIntW(L"sim", L"platform", SIM_PLATFORM_MLB_EVO, g_ini), false);

    /* cams.bin: из hud_sim.ini, иначе рядом с программой */
    {
        wchar_t p[MAX_PATH * 2];
        GetPrivateProfileStringW(L"sim", L"cams", L"", p, MAX_PATH, g_ini);
        if (!(p[0] && load_cams(p, true))) {
            swprintf(p, MAX_PATH * 2, L"%ls\\cams.bin", g_exedir);
            if (!load_cams(p, true)) logf_w(TR("[sim] cams.bin не найден — камеры не показываются (меню «Файл» → «Загрузить базу камер»)\n", "[sim] cams.bin not found — cameras are not shown (File menu → Load camera database)\n"));
        }
    }

    /* что открыть: аргумент командной строки (файл или папка), иначе — последний лог (на паузе) */
    {
        int argc = 0; LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        wchar_t p[MAX_PATH];
        if (argv && argc > 1) open_path(argv[1], true);
        else if (GetPrivateProfileStringW(L"sim", L"last", L"", p, MAX_PATH, g_ini) && p[0]) open_path(p, false);
        if (argv) LocalFree(argv);
    }

    {   /* карта: порт и ключи (необязательно) — [map] port, yandex_key, gis_key в hud_sim.ini */
        wchar_t hp[MAX_PATH * 2], yk[160], gk[160]; char y8[320], g8[320];
        GetPrivateProfileStringW(L"map", L"yandex_key", L"", yk, 160, g_ini);
        GetPrivateProfileStringW(L"map", L"gis_key", L"", gk, 160, g_ini);
        WideCharToMultiByte(CP_UTF8, 0, yk, -1, y8, sizeof y8, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, gk, -1, g8, sizeof g8, NULL, NULL);
        swprintf(hp, MAX_PATH * 2, L"%ls\\map.html", g_exedir);
        map_server_start((int)GetPrivateProfileIntW(L"map", L"port", 8765, g_ini), hp, y8, g8);
    }

    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);
    SetTimer(g_wnd, 1, 15, NULL);
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return (int)msg.wParam;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        tick();
        MsgWaitForMultipleObjects(0, NULL, FALSE, 4, QS_ALLINPUT);
    }
}
