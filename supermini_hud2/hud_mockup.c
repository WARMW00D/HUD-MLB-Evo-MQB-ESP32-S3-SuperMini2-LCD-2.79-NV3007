/*
  hud_mockup.c — HUD supermini_hud2: ТОЛЬКО минимальный режим, без анимации
  ------------------------------------------------------------------------------
  Раскладка в нативных координатах панели 428x142 (утверждена по превью v13).
  Левая зона — стрелка манёвра (последний кадр с перспективой) и расстояние под ней;
  центр — скорость; под ней ОДИН знак по приоритету (камера > знак на дороге / ограничение с карты)
  и значок камеры с расстоянием; справа — иконка ACC / Lane Assist, значки Side Assist по бокам
  и заданная скорость под иконкой.
  Ресурсы (шрифты, картинки) уже подогнаны под панель: tools/gen_min_native.py.
  Обновление — lv_timer 50 мс внутри задачи LVGL (под её мьютексом).
*/

#include "lvgl.h"
#include "esp_system.h"
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "hud_overspeed.h"
#include "hud_light.h"
#include "hud_config.h"
#include "user_config.h"
#include "hud_mockup.h"
#include "hud_data.h"
#include "vze_table.h"
#include "hud_min_images.h"
#include "hud_nav_images.h"
#include "hud_source.h"
#include "psd_speedlimit.h"
#include "cam_alert.h"
#include "hud_cams.h"
#include "hud_sound.h"
#include "src/lcd_bl_bsp/lcd_bl_pwm_bsp.h"

LV_FONT_DECLARE(hud_font_mspeed);   /* скорость: Overpass Medium, цифры 74 px, сжато по ширине   */
LV_FONT_DECLARE(hud_font_mdist);    /* расстояние до манёвра, заданная скорость ACC: цифры 25 px */
LV_FONT_DECLARE(hud_font_munit22);  /* «км/ч» рядом со скоростью                                 */
LV_FONT_DECLARE(hud_font_munit20);  /* единицы у расстояния и заданной скорости                  */
LV_FONT_DECLARE(hud_font_mcam);     /* расстояние до камеры                                      */
LV_FONT_DECLARE(hud_font_msign2);   /* цифры знака: 2 цифры                                      */
LV_FONT_DECLARE(hud_font_msign3);   /*   3 цифры                                                 */
LV_FONT_DECLARE(hud_font_mcdisc2);  /* цифры круга камеры: 2 цифры                               */
LV_FONT_DECLARE(hud_font_mcdisc3);  /*   3 цифры                                                 */
LV_FONT_DECLARE(hud_font_menu);     /* сообщения (кириллица + латиница)                          */

#ifdef __cplusplus
extern "C" {
#endif
    void arduino_printf(const char *format, ...);
#ifdef __cplusplus
}
#endif

/* ---------- цвета ---------- */
#define C_GREEN      0x2ee64a
#define C_GRAY       0x888888
#define C_ORANGE     0xffa500
#define C_YELLOW     0xffc400
#define C_OBJ_WARN   0xe01414   /* машина впереди при опасной дистанции */
#define C_OVERSPEED  0xff1a1a   /* обводка скорости при превышении */
#define C_SWA        0xe22020   /* Side Assist */
#define C_SIGN_RED   0xcc0000
#define C_SIGN_WARN  0xff8080
#define C_LANE_GREY  0xb0b4be
#define C_LANE_DARK  0x46464a
#define C_CAR_GREY   0x787878
#define C_UNIT_SPEED 0x969696
#define C_UNIT_SMALL 0xc8c8c8
#define C_NAV_ARROW  0x30b0f0
#define C_CAR_BODY   0xe6e6e6
#define C_CAR_LIGHTS 0xc81414
#define C_CAR_OPEN   0xeb1c1c
#define C_BLINK_GRN  0x00cc00
#define C_BLINK_RED  0xe00000

/* ---------- раскладка панели 428x142 ---------- */
#define CX_LEFT      73      /* ось расстояния до манёвра                         */
#define CX_CENTER    195     /* ось скорости (1-2 цифры) и знака                   */
#define CX_RIGHT     370     /* ось иконки ACC: иконка (116 px) упирается в правый край панели */
#define CX_SET       359     /* ось заданной скорости: «200 км/ч» целиком влезает у правого края */
#define ARROW_X      2       /* кадр стрелки 142x96                                */
#define ARROW_Y      18
#define DIST_BASE    141     /* базовая линия расстояния до манёвра / заданной скорости */
#define SPD_BASE     75      /* базовая линия скорости                             */
#define SPD3_RIGHT   292     /* 3 цифры: правый край чернил; «км/ч» — справа сверху */
#define SIGN_D       60      /* знак / квадрат камеры                              */
#define SIGN_RING    6
#define SIGN_Y       (HUD_PANEL_H - SIGN_D)
#define CAM_DISC     47      /* круг на жёлтом квадрате камеры                     */
#define CAM_RING     5
#define CAM_GAP      15      /* квадрат камеры -> значок и расстояние              */
#define CAM_LEFT     137     /* левый край ряда камеры (фиксирован)                */
#define CAM_BASE     141     /* базовая линия расстояния до камеры                 */
#define CAM_EYE_BOT  115     /* низ значка камеры                                  */
#define ICON_X       (CX_RIGHT - M_ACC_W / 2)
#define ICON_Y       19

/* ---------- хэндлы ---------- */
static lv_obj_t *mn_arrow, *mn_dist_num, *mn_dist_unit, *mn_calc;
static lv_obj_t *mn_spd, *mn_spd_unit, *mn_spd_ol[8];   /* mn_spd_ol — красная обводка цифр скорости при превышении */
static lv_obj_t *mn_sign, *mn_sign_lbl, *mn_sign_end, *mn_sign_car_l, *mn_sign_car_r;
static lv_obj_t *mn_cam_sq, *mn_cam_disc, *mn_cam_lbl, *mn_cam_eye, *mn_cam_dist;
static lv_obj_t *mn_acc_box, *mn_lane_l, *mn_lane_r, *mn_lead, *mn_arcs, *mn_own, *mn_swa_l, *mn_swa_r;
static lv_obj_t *mn_car_box, *mn_car_part[6], *mn_car_under[6];   /* FL FR RL RR HOOD TRUNK — как биты DOOR_* */
static lv_obj_t *mn_ps_box, *mn_ps_tri;
static lv_obj_t *mn_bl_out[2], *mn_bl_fill[2];                        /* поворотники: [0] левый, [1] правый */
static lv_obj_t *mn_set_num, *mn_set_unit, *mn_lim;
static lv_obj_t *mn_link, *mn_pair;
static lv_obj_t *ota_panel, *ota_title, *ota_status, *ota_bar_bg, *ota_bar_fill;
static volatile uint8_t ota_ui_state;
static volatile uint32_t ota_ui_received, ota_ui_total;
#define OTA_BAR_W    300

/* ---------- мелкие помощники ---------- */
static void vis(lv_obj_t *o, bool v)
{
    bool hidden = lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (v && hidden)        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else if (!v && !hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void set_text(lv_obj_t *l, const char *s)
{
    const char *cur = lv_label_get_text(l);
    if (!cur || strcmp(cur, s) != 0) lv_label_set_text(l, s);
}

static void set_text_color(lv_obj_t *o, uint32_t hex)
{
    lv_color_t c = lv_color_hex(hex);
    if (lv_obj_get_style_text_color(o, LV_PART_MAIN).full != c.full)
        lv_obj_set_style_text_color(o, c, 0);
}

static void set_bg_color(lv_obj_t *o, uint32_t hex)
{
    lv_color_t c = lv_color_hex(hex);
    if (lv_obj_get_style_bg_color(o, LV_PART_MAIN).full != c.full)
        lv_obj_set_style_bg_color(o, c, 0);
}

static void set_border_color(lv_obj_t *o, uint32_t hex)
{
    lv_color_t c = lv_color_hex(hex);
    if (lv_obj_get_style_border_color(o, LV_PART_MAIN).full != c.full)
        lv_obj_set_style_border_color(o, c, 0);
}

static void set_font(lv_obj_t *o, const lv_font_t *f)
{
    if (lv_obj_get_style_text_font(o, LV_PART_MAIN) != f)
        lv_obj_set_style_text_font(o, f, 0);
}

static void set_img_color(lv_obj_t *o, uint32_t hex)
{
    /* Для масок ALPHA_8BIT цвет картинки = img_recolor, НО LVGL 8 берёт его,
       только если img_recolor_opa > 0 (иначе маска рисуется чёрной).
       Поэтому у всех таких картинок recolor_opa = COVER. */
    lv_color_t c = lv_color_hex(hex);
    if (lv_obj_get_style_img_recolor(o, LV_PART_MAIN).full != c.full)
        lv_obj_set_style_img_recolor(o, c, 0);
}

enum { SIGN_HIDE, SIGN_LIMIT, SIGN_END, SIGN_RAW, SIGN_NOOVT };

typedef struct { uint8_t mode; uint16_t val; } SignItem;

/* Код VZE -> знак. allow_fallback: для основного знака можно взять ACC_Tempolimit
   и показать сырой код; для дополнительных знаков 2/3 — только то, что точно известно. */
static SignItem vze_eval(uint8_t code, bool suppress, uint8_t tl, bool allow_fallback)
{
    SignItem it = { SIGN_HIDE, 0 };
    if (!code || suppress) return it;
    const VzeEntry *e = &vze_table[code];
    if (e->type == VZE_LIMIT)               { it.mode = SIGN_LIMIT; it.val = e->value; }
    else if (e->type == VZE_END_LIMIT)      { it.mode = SIGN_END; }
    else if (e->type == VZE_NO_OVERTAKE)    { it.mode = SIGN_NOOVT; }
    else if (e->type != VZE_UNKNOWN)        { it.mode = SIGN_HIDE; }
    else if (vze_linear_kmh(code))          { it.mode = SIGN_LIMIT; it.val = vze_linear_kmh(code); }
    else if (!allow_fallback)               { it.mode = SIGN_HIDE; }
    else if (tl == ACC_TEMPOLIMIT_END)      { it.mode = SIGN_END; }
    else if (acc_tempolimit_kmh(tl))        { it.mode = SIGN_LIMIT; it.val = acc_tempolimit_kmh(tl); }
    else                                    { it.mode = SIGN_RAW; it.val = code; }
    return it;
}


/* ---------- язык, единицы и настройки (из портала / NVS) ---------- */
static volatile uint8_t g_lang  = HUD_LANG;
static volatile uint8_t g_units = HUD_UNITS;
static volatile bool    g_psd   = HUD_PSD_LIMITS;
static volatile bool    g_vze   = HUD_VZE_SIGNS;
static volatile uint8_t g_speed_tol = HUD_OVERSPEED_TOL_KMH;
static volatile bool    g_cams_on  = HUD_CAMS_ON;
static volatile bool    g_cam_beep = HUD_CAM_BEEP;

void    hud_set_lang(uint8_t lang)   { g_lang  = lang  == HUD_LANG_EN  ? HUD_LANG_EN  : HUD_LANG_RU;  }
void    hud_set_units(uint8_t units) { g_units = units == HUD_UNITS_MI ? HUD_UNITS_MI : HUD_UNITS_KM; }
uint8_t hud_get_lang(void)           { return g_lang;  }
uint8_t hud_get_units(void)          { return g_units; }
void    hud_set_psd(bool on)         { g_psd = on; }
bool    hud_get_psd(void)            { return g_psd; }
void    hud_set_vze(bool on)         { g_vze = on; }
bool    hud_get_vze(void)            { return g_vze; }
void    hud_set_overspeed_tol(uint8_t kmh) { if (kmh <= 100) g_speed_tol = kmh; }
uint8_t hud_get_overspeed_tol(void)  { return g_speed_tol; }
void    hud_set_cams(bool on)        { g_cams_on = on; }
bool    hud_get_cams(void)           { return g_cams_on; }
void    hud_set_cam_beep(bool on)    { g_cam_beep = on; }
bool    hud_get_cam_beep(void)       { return g_cam_beep; }

typedef struct { const char *kmh, *m, *km, *h; } HudTxt;
static const HudTxt TXT_RU = { "км/ч", "м", "км", "ч" };
static const HudTxt TXT_EN = { "km/h", "m", "km", "h" };
static inline const HudTxt *txt(void) { return g_lang == HUD_LANG_EN ? &TXT_EN : &TXT_RU; }

/* скорость для показа: км/ч -> mph в режиме миль. Знаки не пересчитываются:
   на знаке — то, что написано на дорожном знаке */
static inline unsigned spd(unsigned kmh)
{
    return g_units == HUD_UNITS_MI ? (kmh * 1000u + 804u) / 1609u : kmh;
}

/* Дистанция BAP (x10 + единица MIB: 0 м, 1 км, 2 ярд, 3 фут, 4 миля, 5 1/4 мили) -> текст.
   В режиме миль метрика переводится: до 0.1 мили — футы (шаг 10), дальше — мили.
   Имперские единицы (если так настроен MMI) показываются как есть, ярды — в футах.
   Для имперских единиц сокращения латиницей на обоих языках (mi, ft). */
static void fmt_dist(char *b, size_t n, uint32_t x10, uint8_t unit)
{
    const HudTxt *t = txt();
    if (g_units == HUD_UNITS_MI && (unit == 0 || unit == 1)) {
        uint32_t m = (unit == 0) ? x10 / 10 : x10 * 100;            /* метры */
        if (m < 161) {
            uint32_t ft = ((m * 3281u + 500u) / 1000u + 5u) / 10u * 10u;
            snprintf(b, n, "%luft", (unsigned long)ft);
            return;
        }
        x10 = (m * 10u + 804u) / 1609u;                               /* мили x10 */
        unit = 4;
    }
    if (unit == 5) { x10 = x10 / 4; unit = 4; }                       /* 1/4 мили -> мили */
    if (unit == 2) { x10 = x10 * 3; unit = 3; }                       /* ярды -> футы (в шрифте нет 'y') */
    const char *u = (unit == 0) ? t->m : (unit == 1) ? t->km : (unit == 2) ? "" :
                    (unit == 3) ? "ft" : (unit == 4) ? "mi" : "";
    if (unit == 0 || unit == 2 || unit == 3 || x10 >= 100)
        snprintf(b, n, "%lu%s", (unsigned long)(x10 / 10), u);
    else
        snprintf(b, n, "%lu.%lu%s", (unsigned long)(x10 / 10), (unsigned long)(x10 % 10), u);
}

/* ---------- выбор стрелки по BAP ManeuverDescriptor ----------
   Direction: 0x00 прямо, 0x40 налево, 0x80 назад, 0xC0 направо.
   Коды MainElement — из handoff; неизвестные рисуются поворотом по Direction
   и один раз печатаются в Serial, чтобы дописать таблицу. */
static const NavImg *nav_pick(const NavSet *set, uint8_t main, uint8_t dir)
{
    uint8_t sec = (uint8_t)(((dir + 8) >> 4) & 15);
    bool    right = dir > 0x80;
    switch (main) {
    case 0x00: case 0x01: return NULL;                  /* нет символа / нет данных — ничего не рисуем */
    case 0x03: return &set->arrived[0];                 /* Arrived               */
    case 0x09: case 0x0A: return NULL;                  /* расчёт маршрута ("...") */
    case 0x0B: return &set->turn[0];                     /* FollowStreet          */
    case 0x0F: return &set->exit[1];                     /* ExitRight             */
    case 0x10: return &set->exit[0];                     /* ExitLeft              */
    case 0x13: case 0x14:                               /* Exit: приборка рисует съезд с дороги, а не развилку */
        if (dir == 0) right = (main == 0x13);
        return &set->exit[right ? 1 : 0];
    case 0x15: case 0x16: return &set->round[sec];       /* Roundabout            */
    case 0x19: return &set->uturn[right ? 1 : 0];        /* U-turn                */
    case 0x0D: case 0x1C: return &set->turn[sec];       /* Turn / PrepareTurn    */
    default: {
#if HUD_LOG_BAP
        static uint8_t reported[32];                    /* битовая карта 0..255  */
        if (!(reported[main >> 3] & (1u << (main & 7)))) {
            reported[main >> 3] |= 1u << (main & 7);
            arduino_printf("[nav] неизвестный MainElement 0x%02X (dir 0x%02X) — рисую поворотом\n", main, dir);
        }
#endif
        return &set->turn[sec];
    }
    }
}

/* ---------- автояркость по Dimmung_01 ---------- */
/* освещённость, лк: LS_Helligkeit_FW x6 (до 6126); когда датчик в потолке —
   верхняя граница диапазона по RLS_Vorfeldhelligkeit_Boost (0 — ярче 24413) */
static int light_lux(const HudData *d)
{
    /* Poll the optional ADC1 photoresistor from the LVGL task. RLS_01 has priority. */
    hud_light_poll();
    static const uint16_t boost_lux[16] = { 30000, 24413, 22193, 20176, 18342, 16647, 15158, 13780,
                                            12527, 11388, 10353, 9412, 8556, 7778, 7071, 6428 };
    if ((d->valid & V_RLS) && d->rls_fw <= 1021) {
        if (d->rls_fw >= 1020 && d->rls_boost <= 15) return boost_lux[d->rls_boost];
        return d->rls_fw * 6;
    }
    /* Optional photoresistor is the fallback when RLS_01 is absent. */
    int ldr = hud_light_lux();
    if (ldr >= 0) return ldr;
    /* датчика нет — оценка по авто-яркости дисплеев (0 лк -> ~99, 6000 лк -> 253) */
    if ((d->valid & V_DIM) && d->dim_raw <= 253)
        return d->dim_raw <= 99 ? 0 : (d->dim_raw - 99) * 6000 / 154;
    return -1;
}

static void update_brightness(const HudData *d, uint32_t now)
{
    static int cur = -1;
    int lux = light_lux(d);
    /* колёсико 0..1: BCM1_04; нет — DI_KL_58xd в темноте (там он = колёсико 10..100); нет — по умолчанию */
    float w;
    if (d->valid & V_WHEEL)                                   w = (d->wheel_pct - 1) / 99.0f;
    else if ((d->valid & V_DIM) && d->dim_raw <= 100 && lux <= 0) w = (d->dim_raw - 10) / 90.0f;
    else                                                      w = (HUD_WHEEL_DEFAULT - 1) / 99.0f;
    if (w < 0) w = 0;
    if (w > 1) w = 1;
    int target = HUD_BRIGHT_NO_DATA;
    float t = 0;
    if (lux >= 0) {
        /* освещённость 0..1, логарифмически между DARK и BRIGHT */
        float lo = log10f(HUD_LIGHT_LUX_DARK + 10.0f), hi = log10f(HUD_LIGHT_LUX_BRIGHT + 10.0f);
        t = (log10f(lux + 10.0f) - lo) / (hi - lo);
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float dark   = HUD_BR_DARK_WHEEL_MIN   + (HUD_BR_DARK_WHEEL_MAX   - HUD_BR_DARK_WHEEL_MIN)   * w;
        float bright = HUD_BR_BRIGHT_WHEEL_MIN + (HUD_BR_BRIGHT_WHEEL_MAX - HUD_BR_BRIGHT_WHEEL_MIN) * w;
        target = (int)(dark + (bright - dark) * t + 0.5f);
    }
    if (now < 1500) return;                      /* подсветка ещё не инициализирована в setup() */
    int next = (cur < 0) ? target
             : (target > cur) ? (target - cur > HUD_BRIGHT_STEP ? cur + HUD_BRIGHT_STEP : target)
             : (cur - target > HUD_BRIGHT_STEP ? cur - HUD_BRIGHT_STEP : target);
    if (next != cur) {
        cur = next;
        setUpduty((uint16_t)(255 - cur));        /* ШИМ подсветки инвертирован: 0 = максимум */
    }
    /* диагностика: раз в 10 с, что пришло и что выставлено */
    static uint32_t last_log;
    if (HUD_LOG_DIM && now - last_log > 10000) {
        last_log = now;
        arduino_printf("[dim] свет %d лк (FW %s%u, Boost %u) -> %.0f%%, колёсико %s%u%% -> подсветка %d/255\n",
                       lux, (d->valid & V_RLS) ? "" : "нет ", d->rls_fw * 6u, d->rls_boost, t * 100.0f,
                       (d->valid & V_WHEEL) ? "" : "нет ", d->wheel_pct, cur);
    }
}


/* ---------- камеры фиксации скорости: данные для ряда под скоростью ---------- */
static bool    g_cam_hit;
static uint8_t g_cam_lim;
static bool    g_cam_over;
static char    g_cam_txt[16];

/* true — камера впереди есть. *lim_for_speed уменьшается до лимита камеры вблизи неё. */
static bool cams_update(uint32_t now, const HudData *d, uint16_t *lim_for_speed)
{
    static uint32_t beeped = 0xFFFFFFFFu;
    cam_hit_t h; bool hit = false;
    bool gate = g_cams_on && hud_cams_ready()
             && (d->valid & V_GPS) && (d->valid & V_HDG) && (d->valid & V_SPEED)
             && d->gps_fix == 3 && d->gps_sat >= 4 && d->gps_hdop_x10 <= HUD_CAM_MAX_HDOP_X10
             && d->gps_hdg_x10 != 0xFFFF && d->speed_kmh >= HUD_CAM_MIN_SPEED_KMH;
    if (gate)
        hit = cam_query(now, d->gps_lat_e6 / 1e6, d->gps_lon_e6 / 1e6, d->gps_hdg_x10 / 10.0f, (float)d->speed_kmh, &h);

    /* Условия допуска на миг не выполнились (курс / HDOP / GNSS мигнули) — камеру не гасим сразу:
       расстояние продолжает уменьшаться. Если запрос шёл и камеры нет — это настоящий конец. */
    static cam_hit_t s_h_last; static uint32_t s_t_last = 0; static bool s_was = false;
    if (hit)                      { s_h_last = h; s_t_last = now; s_was = true; }
    else if (!gate && g_cams_on && s_was && (now - s_t_last) <= 1500) {
        float left = s_h_last.dist_m - d->speed_kmh / 3.6f * (now - s_t_last) / 1000.0f;
        if (left > 0) { h = s_h_last; h.dist_m = left; hit = true; } else s_was = false;
    }
    else                          s_was = false;

    g_cam_hit = hit;
    if (!hit) return false;

    if (g_units == HUD_UNITS_MI) snprintf(g_cam_txt, sizeof g_cam_txt, "%dft", (int)(h.dist_m * 3.281f / 50) * 50);
    else                         snprintf(g_cam_txt, sizeof g_cam_txt, "%d%s", (int)(h.dist_m / 10) * 10, g_lang == HUD_LANG_EN ? "m" : "м");
    g_cam_lim  = h.limit;
    g_cam_over = h.limit && d->speed_kmh > h.limit + g_speed_tol;

    if (h.idx != beeped) {                              /* новая камера */
        beeped = h.idx;
        arduino_printf("[cam] #%lu limit %u at %.0f m, v=%u, hdg %u/cam %d\n", (unsigned long)h.idx, h.limit,
                       h.dist_m, d->speed_kmh, d->gps_hdg_x10 / 10, (int)h.dir_deg);
        if (g_cam_beep && h.limit && d->speed_kmh > h.limit) hud_sound_play(HUD_SND_WARN);
    }
    if (h.limit && h.dist_m <= HUD_CAM_OVERSPEED_M && (*lim_for_speed == 0 || h.limit < *lim_for_speed))
        *lim_for_speed = h.limit;
    return true;
}


void hud_ota_ui_status(uint8_t state, uint32_t received, uint32_t total)
{
    ota_ui_state = state;
    ota_ui_received = received;
    ota_ui_total = total;
}

static void hud_ota_ui_update(void)
{
    const uint8_t state = ota_ui_state;
    const uint32_t received = ota_ui_received;
    const uint32_t total = ota_ui_total;
    const bool show = state != 0;
    static uint8_t last_state = 0;
    vis(ota_panel, show);
    if (show && state != last_state) lv_obj_move_foreground(ota_panel);
    last_state = state;
    if (!show) return;

    uint32_t pct = (total && received <= total) ? (received * 100u / total) : 0u;
    if (pct > 100u) pct = 100u;
    lv_obj_set_width(ota_bar_fill, pct ? (OTA_BAR_W * pct) / 100u : 1);
    vis(ota_bar_fill, pct != 0);
    char buf[48];
    if (state == 1) {
        set_text(ota_title, g_lang == HUD_LANG_EN ? "Firmware update" : "Обновление ПО");
        snprintf(buf, sizeof buf, g_lang == HUD_LANG_EN ? "Uploading... %lu%%" : "Загрузка... %lu%%", (unsigned long)pct);
    } else if (state == 2) {
        set_text(ota_title, g_lang == HUD_LANG_EN ? "Firmware update" : "Обновление ПО");
        snprintf(buf, sizeof buf, g_lang == HUD_LANG_EN ? "Verifying image..." : "Проверка прошивки...");
    } else if (state == 3) {
        set_text(ota_title, g_lang == HUD_LANG_EN ? "Update complete" : "Обновление завершено");
        snprintf(buf, sizeof buf, g_lang == HUD_LANG_EN ? "Restarting..." : "Перезапуск...");
        lv_obj_set_width(ota_bar_fill, OTA_BAR_W); vis(ota_bar_fill, true);
    } else {
        set_text(ota_title, g_lang == HUD_LANG_EN ? "Update failed" : "Ошибка обновления");
        snprintf(buf, sizeof buf, g_lang == HUD_LANG_EN ? "Transfer aborted" : "Передача прервана");
    }
    set_text(ota_status, buf);
}


/* ============================================================================
   Текст и картинки минимального режима
   ========================================================================== */
static lv_obj_t *car_img(lv_obj_t *parent, const lv_img_dsc_t *src, int x, int y, uint32_t col)
{
    lv_obj_t *i = lv_img_create(parent);
    lv_img_set_src(i, src);
    lv_obj_set_style_img_recolor(i, lv_color_hex(col), 0);
    lv_obj_set_style_img_recolor_opa(i, LV_OPA_COVER, 0);
    lv_obj_set_pos(i, x, y);
    return i;
}

static lv_obj_t *mn_label(lv_obj_t *parent, const lv_font_t *f, uint32_t col)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    return l;
}

static lv_obj_t *mn_img(lv_obj_t *parent, const lv_img_dsc_t *src, int x, int y, uint32_t col)
{
    lv_obj_t *i = car_img(parent, src, x, y, col);
    lv_obj_add_flag(i, LV_OBJ_FLAG_HIDDEN);
    return i;
}

/* границы чернил строки (от начала надписи): первая и последняя ненулевые колонки глифов */
static void mn_ink(const lv_font_t *f, const char *s, int *x0, int *x1)
{
    int adv = 0, a = 0, b = 0;
    bool first = true;
    for (const char *p = s; *p; p++) {
        lv_font_glyph_dsc_t g;
        if (!lv_font_get_glyph_dsc(f, &g, (uint32_t)(uint8_t)*p, 0)) continue;
        if (g.box_w > 0) {
            if (first) { a = adv + g.ofs_x; first = false; }
            b = adv + g.ofs_x + g.box_w;
        }
        adv += g.adv_w;
    }
    *x0 = a; *x1 = b;
}

/* высота чернил над базовой линией для строки (макс. по глифам) — для выравнивания «км/ч» по верху цифр */
static int mn_ink_up(const lv_font_t *f, const char *s)
{
    int up = 0;
    for (const char *p = s; *p; p++) {
        lv_font_glyph_dsc_t g;
        if (!lv_font_get_glyph_dsc(f, &g, (uint32_t)(uint8_t)*p, 0) || g.box_w <= 0) continue;
        int t = g.ofs_y + g.box_h;
        if (t > up) up = t;
    }
    return up;
}

/* надпись цифр: чернила по центру cx, базовая линия base; возвращает ширину чернил и левый край чернил */
static int mn_dx, mn_dy;                  /* смещение надписи (для обводки скорости), сбрасывается после вызова */
static int mn_digits(lv_obj_t *l, const lv_font_t *f, const char *s, int cx, int base, int *left)
{
    int a, b;
    set_text(l, s);
    mn_ink(f, s, &a, &b);
    int w = b - a;
    int x = cx - w / 2;                       /* левый край чернил */
    lv_obj_set_pos(l, x - a + mn_dx, base - (f->line_height - f->base_line) + mn_dy);
    vis(l, s[0] != 0);
    if (left) *left = x;
    return w;
}

/* цифры в круге: чернила по центру поля (cx, cy в координатах родителя) */
static void mn_center_ink(lv_obj_t *l, const lv_font_t *f, const char *s, int cx, int cy)
{
    int a, b;
    mn_ink(f, s, &a, &b);
    lv_font_glyph_dsc_t g;
    int top = 0, h = 0;
    if (lv_font_get_glyph_dsc(f, &g, '0', 0)) { h = g.box_h; top = (f->line_height - f->base_line) - g.ofs_y - g.box_h; }
    lv_obj_set_align(l, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(l, cx - (a + b) / 2, cy - (top + h / 2));
}

/* надпись на базовой линии, левый край чернил = x */
static void mn_text_at(lv_obj_t *l, const lv_font_t *f, const char *s, int x, int base)
{
    set_text(l, s);
    int a, b;
    mn_ink(f, s, &a, &b);
    lv_obj_set_pos(l, x - a, base - (f->line_height - f->base_line));
    vis(l, s[0] != 0);
}

static int mn_text_w(const lv_font_t *f, const char *s)
{
    lv_point_t p = { 0, 0 };
    lv_txt_get_size(&p, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return p.x;
}

static lv_obj_t *mn_disc(lv_obj_t *parent, int d, int ring, int x, int y)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(C_SIGN_RED), 0);
    lv_obj_set_style_border_width(o, ring, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void mn_build(lv_obj_t *scr)
{
    /* левая зона: стрелка манёвра и расстояние */
    mn_arrow = lv_img_create(scr);
    lv_img_set_src(mn_arrow, nav_big.turn[0].img);
    lv_obj_add_flag(mn_arrow, LV_OBJ_FLAG_HIDDEN);
    mn_dist_num  = mn_label(scr, &hud_font_mdist, 0xffffff);
    mn_dist_unit = mn_label(scr, &hud_font_munit20, C_UNIT_SMALL);
    mn_calc      = mn_label(scr, &hud_font_mdist, C_GRAY);

    /* двери: машинка на месте стрелки (приоритет над навигацией) */
    mn_car_box = lv_obj_create(scr);
    lv_obj_remove_style_all(mn_car_box);
    lv_obj_set_size(mn_car_box, M_CAR_W, M_CAR_H);
    lv_obj_set_pos(mn_car_box, ARROW_X, ARROW_Y + 3);
    lv_obj_clear_flag(mn_car_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    car_img(mn_car_box, &img_m_car_body,   M_CAR_BODY_X,   M_CAR_BODY_Y,   C_CAR_BODY);
    car_img(mn_car_box, &img_m_car_lights, M_CAR_LIGHTS_X, M_CAR_LIGHTS_Y, C_CAR_LIGHTS);
    {
        static const lv_img_dsc_t *under[6] = { &img_m_car_fl_under, &img_m_car_fr_under, &img_m_car_rl_under,
                                                &img_m_car_rr_under, &img_m_car_hood_under, &img_m_car_trunk_under };
        static const lv_img_dsc_t *part[6]  = { &img_m_car_fl, &img_m_car_fr, &img_m_car_rl,
                                                &img_m_car_rr, &img_m_car_hood, &img_m_car_trunk };
        static const int16_t ux[6] = { M_CAR_FL_UNDER_X, M_CAR_FR_UNDER_X, M_CAR_RL_UNDER_X, M_CAR_RR_UNDER_X, M_CAR_HOOD_UNDER_X, M_CAR_TRUNK_UNDER_X };
        static const int16_t uy[6] = { M_CAR_FL_UNDER_Y, M_CAR_FR_UNDER_Y, M_CAR_RL_UNDER_Y, M_CAR_RR_UNDER_Y, M_CAR_HOOD_UNDER_Y, M_CAR_TRUNK_UNDER_Y };
        static const int16_t px[6] = { M_CAR_FL_X, M_CAR_FR_X, M_CAR_RL_X, M_CAR_RR_X, M_CAR_HOOD_X, M_CAR_TRUNK_X };
        static const int16_t py[6] = { M_CAR_FL_Y, M_CAR_FR_Y, M_CAR_RL_Y, M_CAR_RR_Y, M_CAR_HOOD_Y, M_CAR_TRUNK_Y };
        for (int i = 0; i < 6; i++) {
            mn_car_under[i] = mn_img(mn_car_box, under[i], ux[i], uy[i], 0x000000);
            mn_car_part[i]  = mn_img(mn_car_box, part[i],  px[i], py[i], C_CAR_OPEN);
        }
    }
    lv_obj_add_flag(mn_car_box, LV_OBJ_FLAG_HIDDEN);

    /* pre sense: красный треугольник + надпись, поверх машинки и стрелки */
    mn_ps_box = lv_obj_create(scr);
    lv_obj_remove_style_all(mn_ps_box);
    lv_obj_set_size(mn_ps_box, M_CAR_W, M_CAR_H);
    lv_obj_set_pos(mn_ps_box, ARROW_X, ARROW_Y + 3);
    lv_obj_clear_flag(mn_ps_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    mn_ps_tri = car_img(mn_ps_box, &img_m_presense_tri, M_PRESENSE_TRI_X, M_PRESENSE_TRI_Y, 0xe01414);
    car_img(mn_ps_box, &img_m_presense_txt, M_PRESENSE_TXT_X, M_PRESENSE_TXT_Y, 0xffffff);
    lv_obj_add_flag(mn_ps_box, LV_OBJ_FLAG_HIDDEN);

    /* центр: скорость (красная обводка — 8 копий под белыми цифрами) */
    for (int i = 0; i < 8; i++) mn_spd_ol[i] = mn_label(scr, &hud_font_mspeed, C_OVERSPEED);
    mn_spd      = mn_label(scr, &hud_font_mspeed, 0xffffff);
    mn_spd_unit = mn_label(scr, &hud_font_munit22, C_UNIT_SPEED);

    /* знак */
    mn_sign = mn_disc(scr, SIGN_D, SIGN_RING, 0, SIGN_Y);
    mn_sign_lbl = lv_label_create(mn_sign);
    lv_label_set_text(mn_sign_lbl, "");
    lv_obj_set_style_text_font(mn_sign_lbl, &hud_font_msign2, 0);
    lv_obj_set_style_text_color(mn_sign_lbl, lv_color_black(), 0);
    lv_obj_set_style_pad_all(mn_sign_lbl, 0, 0);
    {
        const int f = SIGN_D - 2 * SIGN_RING;                      /* белое поле */
        static lv_point_t end_pts[2];
        end_pts[0] = (lv_point_t){ f * 11 / 44, f - f * 11 / 44 };
        end_pts[1] = (lv_point_t){ f - f * 11 / 44, f * 11 / 44 };
        mn_sign_end = lv_line_create(mn_sign);
        lv_line_set_points(mn_sign_end, end_pts, 2);
        lv_obj_set_style_line_width(mn_sign_end, 4, 0);
        lv_obj_set_style_line_color(mn_sign_end, lv_color_hex(C_GRAY), 0);
        lv_obj_set_pos(mn_sign_end, 0, 0);
        lv_obj_add_flag(mn_sign_end, LV_OBJ_FLAG_HIDDEN);
        /* «обгон запрещён»: левая машина красная, правая чёрная */
        const int cw = M_SIGN_CAR_W, ch = M_SIGN_CAR_H;
        mn_sign_car_l = car_img(mn_sign, &img_m_sign_car, f / 2 - cw - 1, (f - ch) / 2, C_SIGN_RED);
        mn_sign_car_r = car_img(mn_sign, &img_m_sign_car, f / 2 + 1, (f - ch) / 2, 0x000000);
        lv_obj_add_flag(mn_sign_car_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mn_sign_car_r, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(mn_sign, LV_OBJ_FLAG_HIDDEN);

    /* камера: жёлтый квадрат с кругом, значок камеры и расстояние */
    mn_cam_sq = lv_obj_create(scr);
    lv_obj_remove_style_all(mn_cam_sq);
    lv_obj_set_size(mn_cam_sq, SIGN_D, SIGN_D);
    lv_obj_set_pos(mn_cam_sq, 0, SIGN_Y);
    lv_obj_set_style_radius(mn_cam_sq, 6, 0);
    lv_obj_set_style_bg_color(mn_cam_sq, lv_color_hex(C_YELLOW), 0);
    lv_obj_set_style_bg_opa(mn_cam_sq, LV_OPA_COVER, 0);
    lv_obj_clear_flag(mn_cam_sq, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    mn_cam_disc = mn_disc(mn_cam_sq, CAM_DISC, CAM_RING, (SIGN_D - CAM_DISC) / 2, (SIGN_D - CAM_DISC) / 2);
    mn_cam_lbl = lv_label_create(mn_cam_disc);
    lv_label_set_text(mn_cam_lbl, "");
    lv_obj_set_style_text_font(mn_cam_lbl, &hud_font_mcdisc2, 0);
    lv_obj_set_style_text_color(mn_cam_lbl, lv_color_black(), 0);
    lv_obj_set_style_pad_all(mn_cam_lbl, 0, 0);
    lv_obj_add_flag(mn_cam_sq, LV_OBJ_FLAG_HIDDEN);
    mn_cam_eye  = mn_img(scr, &img_m_cam, 0, 0, C_YELLOW);
    mn_cam_dist = mn_label(scr, &hud_font_mcam, C_YELLOW);

    /* правая зона: иконка ACC / Lane Assist (слои-маски), Side Assist, заданная скорость */
    mn_acc_box = lv_obj_create(scr);
    lv_obj_remove_style_all(mn_acc_box);
    lv_obj_set_size(mn_acc_box, M_ACC_W, M_ACC_H);
    lv_obj_set_pos(mn_acc_box, ICON_X, ICON_Y);
    lv_obj_clear_flag(mn_acc_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    mn_lane_l = car_img(mn_acc_box, &img_m_lane_l, M_LANE_L_X, M_LANE_L_Y, C_LANE_GREY);
    mn_lane_r = car_img(mn_acc_box, &img_m_lane_r, M_LANE_R_X, M_LANE_R_Y, C_LANE_GREY);
    mn_lead   = car_img(mn_acc_box, &img_m_lead,   M_LEAD_X,   M_LEAD_Y,   C_GREEN);
    mn_arcs   = car_img(mn_acc_box, &img_m_arcs,   M_ARCS_X,   M_ARCS_Y,   C_GREEN);
    mn_own    = car_img(mn_acc_box, &img_m_own,    M_OWN_X,    M_OWN_Y,    C_GREEN);
    lv_obj_add_flag(mn_acc_box, LV_OBJ_FLAG_HIDDEN);
    mn_swa_l = mn_img(scr, &img_m_swa, ICON_X + M_SWA_L_X, ICON_Y + M_SWA_Y, C_SWA);
    mn_swa_r = mn_img(scr, &img_m_swa, ICON_X + M_SWA_R_X, ICON_Y + M_SWA_Y, C_SWA);
    mn_set_num  = mn_label(scr, &hud_font_mdist, 0xffffff);
    mn_set_unit = mn_label(scr, &hud_font_munit20, C_UNIT_SMALL);
    mn_lim = mn_img(scr, &img_m_lim, CX_RIGHT - M_LIM_W / 2, 60, 0xffffff);

    /* поворотники: нижние углы, поверх остального; чёрная обводка + заливка (зелёная / красная при аварийке) */
    {
        static const lv_img_dsc_t *bo[2] = { &img_m_blink_l_outline, &img_m_blink_r_outline };
        static const lv_img_dsc_t *bf[2] = { &img_m_blink_l_fill,    &img_m_blink_r_fill };
        const int bx[2] = { 2, HUD_PANEL_W - M_BLINK_W - 2 };
        for (int i = 0; i < 2; i++) {
            mn_bl_out[i]  = mn_img(scr, bo[i], bx[i], HUD_PANEL_H - M_BLINK_W, 0x000000);
            mn_bl_fill[i] = mn_img(scr, bf[i], bx[i], HUD_PANEL_H - M_BLINK_W, C_BLINK_GRN);
        }
    }

    /* нет связи / сопряжение не удаётся */
    mn_link = mn_label(scr, LV_FONT_DEFAULT, 0x666666);
    mn_pair = mn_label(scr, &hud_font_menu, C_ORANGE);
    lv_label_set_long_mode(mn_pair, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(mn_pair, 300);
    lv_obj_set_pos(mn_pair, 64, 30);
}

/* центр текущей зоны скорости, левый и правый края цифр (для знака / камеры) — пишет mn_speed() */
static int g_row_cx = CX_CENTER, g_spd_left = CX_CENTER;

static void mn_speed(const HudData *d, int ov_opa)
{
    char buf[16];
    const HudTxt *tx = txt();
    if (!(d->valid & V_SPEED)) {
        vis(mn_spd, false);
        for (int i = 0; i < 8; i++) vis(mn_spd_ol[i], false);
        vis(mn_spd_unit, false);
        g_row_cx = CX_CENTER; g_spd_left = CX_CENTER;
        return;
    }
    snprintf(buf, sizeof buf, "%u", spd(d->speed_kmh));
    const bool d3 = strlen(buf) >= 3;
    int a, b;
    mn_ink(&hud_font_mspeed, buf, &a, &b);
    const int w = b - a;
    const int cx = d3 ? SPD3_RIGHT - w / 2 : CX_CENTER;
    int left;
    mn_digits(mn_spd, &hud_font_mspeed, buf, cx, SPD_BASE, &left);
    /* превышение — красная обводка вокруг белых цифр */
    static const int8_t off[8][2] = { {-2,0},{2,0},{0,-2},{0,2},{-2,-1},{2,-1},{-2,1},{2,1} };
    for (int i = 0; i < 8; i++) {
        if (ov_opa > 0) {
            mn_dx = off[i][0]; mn_dy = off[i][1];
            mn_digits(mn_spd_ol[i], &hud_font_mspeed, buf, cx, SPD_BASE, NULL);
            mn_dx = mn_dy = 0;
            if (lv_obj_get_style_text_opa(mn_spd_ol[i], LV_PART_MAIN) != ov_opa) lv_obj_set_style_text_opa(mn_spd_ol[i], (lv_opa_t)ov_opa, 0);
        }
        vis(mn_spd_ol[i], ov_opa > 0);
    }
    const char *u = g_units == HUD_UNITS_MI ? "mph" : tx->kmh;
    if (d3) {
        /* 3 цифры: единица справа от цифр, верх чернил на уровне верха цифр */
        int top = SPD_BASE - mn_ink_up(&hud_font_mspeed, "0");
        mn_text_at(mn_spd_unit, &hud_font_munit22, u, SPD3_RIGHT + 5, top + mn_ink_up(&hud_font_munit22, u));
    } else {
        mn_text_at(mn_spd_unit, &hud_font_munit22, u, left + w + 9, SPD_BASE);
    }
    g_row_cx = cx; g_spd_left = left;
}


/* ============================================================================
   Обновление экрана — lv_timer, 50 мс, внутри задачи LVGL (под мьютексом)
   ========================================================================== */
static void hud_update_cb(lv_timer_t *t)
{
    (void)t;
    hud_ota_ui_update();
    if (ota_ui_state != 0) return;
    HudData d;
    hud_data_snapshot(&d);
    uint32_t now = hud_now_ms();
    char buf[40];
    const HudTxt *tx = txt();
    uint16_t lim_kmh_for_speed = 0;

    /* --- связь, яркость --- */
    bool pf = !d.link_up && hud_src_pair_failed();
    if (pf) set_text(mn_pair, g_lang == HUD_LANG_EN
        ? "Pairing rejected. Check the CAN gateway passkey."
        : "Сопряжение не принято. Проверьте код доступа CAN-шлюза.");
    vis(mn_pair, pf);
    bool ls = !d.link_up && !pf;
    if (ls) {
        set_text(mn_link, LV_SYMBOL_BLUETOOTH " ...");
        lv_obj_set_pos(mn_link, CX_CENTER - mn_text_w(LV_FONT_DEFAULT, LV_SYMBOL_BLUETOOTH " ...") / 2, 3);
    }
    vis(mn_link, ls);
    update_brightness(&d, now);

    /* --- ACC / лимитер (режимы не пересекаются: при лимитере ACC_Status_Anzeige = 0) --- */
    bool acc_v   = d.valid & V_ACC;
    bool acc_set = d.acc_set_raw != 0 && d.acc_set_raw < 0x3FF;      /* скорость ACC задана */
    /* ACC показываем, когда он регулирует (3) или водитель превышает (4), а в пассиве (2) — только если скорость задана */
    bool acc_on  = acc_v && (d.acc_status == 3 || d.acc_status == 4 ||
                             (d.acc_status == 2 && acc_set));
    bool lim_on  = (d.valid & V_LIM) && d.limiter_raw != 1022;   /* 1022 = выкл, 1023 = вкл без скорости */
    bool lim_set = lim_on && d.limiter_raw < 1022;

    /* --- Lane Assist, машина впереди --- */
    bool lka_on = (d.valid & V_LKA) && d.lka_state != LKA_OFF;
    bool aca_on = acc_on && d.aca_lane == 2;          /* удержание в полосе в составе ACC */
    /* машина впереди (ACC_Relevantes_Objekt_02): 1 — цель ACC (только при включённом ACC);
       2 — предупреждение (опасная дистанция): ВСЕГДА, красным */
    bool obj_warn = (d.valid & V_ACC) && d.acc_object == 2;
    bool obj  = obj_warn || (acc_on && d.acc_object == 1);
    bool arcs = obj && (d.acc_status == 3 || d.acc_status == 4);

    /* Side Assist: машина сбоку / попытка перестроения (мигает) */
    uint8_t swa_i = 0, swa_w = 0;
#if HUD_SWA
    if (d.valid & V_SWA) { swa_i = d.swa_info; swa_w = d.swa_warn; }
#endif

    /* --- знаки ---
       Основной знак, по приоритету: VZE_01 -> PSD явный знак -> PSD по правилам.
       Смена PSD явный -> по правилам — с задержкой HUD_PSD_LEGAL_DELAY_MS (мерцание
       при перестройке дерева сегментов). Дополнительные знаки (VZE 2..5, запрет обгона)
       показываются по очереди с основным, каждый HUD_SIGN_CYCLE_MS. */
    SignItem items[4]; int n_items = 0;
    {
        static uint32_t pe_t = 0; static uint16_t pe_kmh = 0; static bool pe_novt = false;
        psd_limit_t P = { 0, PSD_LIM_NONE, false };
        if (g_psd) {
            P = psd_get(now, 3000);
            if (P.src == PSD_LIM_EXPLICIT) { pe_t = now ? now : 1; pe_kmh = P.kmh; pe_novt = P.no_overtake; }
        } else {
            pe_t = 0;
        }
        bool psd_expl = pe_t && (now - pe_t) < HUD_PSD_LEGAL_DELAY_MS;
        uint8_t tl = (d.valid & V_ACC) ? d.acc_tempolimit : 0;
        bool sv = g_vze && (d.valid & V_SIGN);              /* знаки VZE отключены — не смотрим */

        SignItem main = { SIGN_HIDE, 0 };
        if (sv) main = vze_eval(d.sign_raw, d.sign_suppress, tl, true);
        bool vze_main = main.mode != SIGN_HIDE;
        if (main.mode == SIGN_HIDE && psd_expl)               { main.mode = SIGN_LIMIT; main.val = pe_kmh; }
        if (main.mode == SIGN_HIDE && (P.src == PSD_LIM_LEGAL || P.src == PSD_LIM_ENDED))
                                                             { main.mode = SIGN_LIMIT; main.val = P.kmh; }
        if (main.mode != SIGN_HIDE) items[n_items++] = main;

        if (sv) {
            SignItem x2 = vze_eval(d.sign_raw2, d.sign_sup2, 0, false);
            SignItem x3 = vze_eval(d.sign_raw3, d.sign_sup3, 0, false);
            SignItem x4 = vze_eval(d.sign_raw4, d.sign_sup4, 0, false);
            SignItem x5 = vze_eval(d.sign_raw5, d.sign_sup5, 0, false);
            if (x2.mode == SIGN_LIMIT && x2.val != main.val && n_items < 4) items[n_items++] = x2;
            if (x3.mode == SIGN_LIMIT && x3.val != main.val && x3.val != x2.val && n_items < 4) items[n_items++] = x3;
            if (x4.mode == SIGN_LIMIT && x4.val != main.val && x4.val != x2.val && x4.val != x3.val && n_items < 4) items[n_items++] = x4;
            if (x5.mode == SIGN_LIMIT && x5.val != main.val && x5.val != x2.val && x5.val != x3.val && x5.val != x4.val && n_items < 4) items[n_items++] = x5;
        }
        if ((!vze_main && psd_expl && pe_novt) || (sv && (vze_eval(d.sign_raw2, d.sign_sup2, 0, false).mode == SIGN_NOOVT ||
                                             vze_eval(d.sign_raw3, d.sign_sup3, 0, false).mode == SIGN_NOOVT)))
            if (n_items < 4) items[n_items++] = (SignItem){ SIGN_NOOVT, 0 };

        /* ограничение для контроля превышения — всегда основной знак */
        lim_kmh_for_speed = (main.mode == SIGN_LIMIT) ? main.val : 0;
    }
    int smode = SIGN_HIDE; uint16_t sval = 0;
    if (n_items) {
        SignItem it = items[(now / HUD_SIGN_CYCLE_MS) % n_items];
        smode = it.mode; sval = it.val;
    }
    cams_update(now, &d, &lim_kmh_for_speed);

    /* --- превышение: непрозрачность красной обводки цифр скорости --- */
    int ov_opa = 0;
    if (lim_kmh_for_speed && (d.valid & V_SPEED)) {
        float excess = (float)d.speed_kmh - (float)lim_kmh_for_speed;
        ov_opa = hud_overspeed_opacity(excess, g_speed_tol, HUD_OVERSPEED_START);
    }

    /* ====================== левая зона: стрелка и расстояние ====================== */
    {
        /* pre sense — главный приоритет в области стрелки: 2 предупреждение (горит),
           3..6 острое / торможение / перехватите управление — мигает 4 раза в секунду */
        bool ps = (d.valid & V_AWV) && d.awv_warn >= 2 && d.awv_warn <= 6;
        if (ps) set_img_color(mn_ps_tri, (d.awv_warn >= 3 && (now % 250) < 125) ? 0x5a0000 : 0xe01414);
        vis(mn_ps_box, ps);

        /* двери / капот / багажник: машинка вместо стрелки */
        uint8_t doors = 0;
        if (d.valid & V_DOORS) doors |= d.doors & (DOOR_FL | DOOR_FR | DOOR_RL | DOOR_RR | DOOR_TRUNK);
        if (d.valid & V_HOOD)  doors |= d.doors & DOOR_HOOD;
        for (int i = 0; i < 6; i++) {
            vis(mn_car_under[i], doors & (1u << i));
            vis(mn_car_part[i],  doors & (1u << i));
        }
        vis(mn_car_box, doors != 0 && !ps);

        bool nav = (d.valid & V_MAN) && d.rg_active == 1 && doors == 0 && !ps;
        const NavImg *ni = nav ? nav_pick(&nav_big, d.man_main, d.man_dir) : NULL;
        bool calc = (d.man_main == 0x09 || d.man_main == 0x0A);
        static const NavImg *last = NULL;
        bool show = nav && ni != NULL;
        if (show && ni != last) {
            lv_img_set_src(mn_arrow, ni->img);
            lv_obj_set_pos(mn_arrow, ARROW_X + ni->x, ARROW_Y + ni->y);
        }
        last = ni;
        vis(mn_arrow, show);
        bool dv = show && (d.valid & V_MDIST);
        if (dv) {
            char s[24];
            fmt_dist(s, sizeof s, d.man_dist_x10, d.man_unit);
            int k = 0;
            while (s[k] && ((s[k] >= '0' && s[k] <= '9') || s[k] == '.')) k++;
            char num[16], unit[12];
            snprintf(num, sizeof num, "%.*s", k, s);
            snprintf(unit, sizeof unit, "%s", s + k);
            int left;
            int w = mn_digits(mn_dist_num, &hud_font_mdist, num, CX_LEFT, DIST_BASE, &left);
            mn_text_at(mn_dist_unit, &hud_font_munit20, unit, left + w + 5, DIST_BASE);
        } else {
            vis(mn_dist_num, false);
            vis(mn_dist_unit, false);
        }
        if (nav && calc) mn_digits(mn_calc, &hud_font_mdist, "...", CX_LEFT, 60, NULL);
        else             vis(mn_calc, false);

#if HUD_LOG_BAP
        /* журнал манёвров: каждая смена описания — строка в Serial (для таблицы кодов) */
        static uint8_t lg_raw[32], lg_len = 0xFF;
        if (nav && (d.man_raw_len != lg_len || memcmp(d.man_raw, lg_raw, sizeof lg_raw) != 0)) {
            lg_len = d.man_raw_len;
            memcpy(lg_raw, d.man_raw, sizeof lg_raw);
            char hex[3 * 32 + 1] = ""; int o = 0;
            for (int k = 0; k < d.man_raw_len && k < 32; k++)
                o += snprintf(hex + o, sizeof hex - o, "%02X ", d.man_raw[k]);
            char dist[16] = "-";
            if (d.valid & V_MDIST) fmt_dist(dist, sizeof dist, d.man_dist_x10, d.man_unit);
            arduino_printf("[nav] манёвр main 0x%02X dir 0x%02X (%u°) z 0x%02X  дист %s  0x17[%u]: %s\n",
                           d.man_main, d.man_dir, (unsigned)(d.man_dir * 360u / 256u), d.man_z,
                           dist, d.man_raw_len, hex);
        }
#endif
    }

    /* ====================== центр: скорость, знак / камера ====================== */
    mn_speed(&d, ov_opa);
    {
        bool cam = g_cam_hit;
        vis(mn_cam_sq, cam);
        vis(mn_cam_eye, cam);
        vis(mn_cam_dist, cam);
        if (cam) {
            snprintf(buf, sizeof buf, "%u", g_cam_lim);
            const lv_font_t *cf = g_cam_lim >= 100 ? &hud_font_mcdisc3 : &hud_font_mcdisc2;
            set_font(mn_cam_lbl, cf);
            set_text(mn_cam_lbl, g_cam_lim ? buf : "");
            mn_center_ink(mn_cam_lbl, cf, buf, (CAM_DISC - 2 * CAM_RING) / 2, (CAM_DISC - 2 * CAM_RING) / 2);
            bool ph = g_cam_over && (now % 400) < 200;
            set_bg_color(mn_cam_disc, ph ? C_SIGN_WARN : 0xffffff);
            set_img_color(mn_cam_eye, g_cam_over ? 0xff3030 : C_YELLOW);
            /* ряд камеры: квадрат слева, значок камеры справа от него, расстояние под значком */
            const int left = CAM_LEFT;                    /* ряд камеры зафиксирован, от скорости не зависит */
            lv_obj_set_x(mn_cam_sq, left);
            int tw = mn_text_w(&hud_font_mcam, g_cam_txt);
            int bw = tw > 49 ? tw : 49;
            int bx = left + SIGN_D + CAM_GAP;
            lv_obj_set_pos(mn_cam_eye, bx + (bw - M_CAM_W) / 2, CAM_EYE_BOT - M_CAM_H);
            set_text(mn_cam_dist, g_cam_txt);
            lv_obj_set_pos(mn_cam_dist, bx + (bw - tw) / 2, CAM_BASE - (hud_font_mcam.line_height - hud_font_mcam.base_line));
            set_text_color(mn_cam_dist, g_cam_over ? 0xff3030 : C_YELLOW);
        }

        bool sg = !cam && smode != SIGN_HIDE && smode != SIGN_RAW;
        vis(mn_sign, sg);
        if (sg) {
            lv_obj_set_x(mn_sign, g_row_cx - SIGN_D / 2);
            bool wr = d.sign_warn && (now % 500) < 250;
            set_bg_color(mn_sign, wr ? C_SIGN_WARN : 0xffffff);
            set_border_color(mn_sign, (smode == SIGN_LIMIT || smode == SIGN_NOOVT) ? C_SIGN_RED : C_GRAY);
            vis(mn_sign_end, smode == SIGN_END);
            vis(mn_sign_car_l, smode == SIGN_NOOVT);
            vis(mn_sign_car_r, smode == SIGN_NOOVT);
            if (smode == SIGN_LIMIT) {
                snprintf(buf, sizeof buf, "%u", sval);
                const lv_font_t *sf = sval >= 100 ? &hud_font_msign3 : &hud_font_msign2;
                set_font(mn_sign_lbl, sf);
                set_text(mn_sign_lbl, buf);
                mn_center_ink(mn_sign_lbl, sf, buf, (SIGN_D - 2 * SIGN_RING) / 2, (SIGN_D - 2 * SIGN_RING) / 2);
            } else {
                set_text(mn_sign_lbl, "");
            }
        }
    }

    /* ====================== правая зона: иконка ACC, Side Assist, заданная скорость ====================== */
    {
        bool swa_any = (swa_i | swa_w) != 0;
        bool icon = lim_on ? swa_any : (acc_on || lka_on || aca_on || obj_warn || swa_any);   /* при лимитере — только серые полосы под Side Assist */
        vis(mn_acc_box, icon);
        if (icon) {
            uint32_t car = acc_on ? (d.acc_status == 3 ? C_GREEN : d.acc_status == 4 ? 0xffffff : C_CAR_GREY) : C_CAR_GREY;
            uint32_t lane_idle = ((acc_on && d.acc_status >= 3) || swa_any) ? C_LANE_GREY : C_LANE_DARK;   /* Side Assist — серые полосы */
            for (int side = 0; side < 2; side++) {
                uint8_t lv  = side ? d.lka_line_r : d.lka_line_l;
                bool    wr  = d.lka_warn & (side ? 2 : 1);
                uint32_t col;
                if (lka_on) {
                    if (wr || lv == 3)  col = ((now % 500) < 250) ? C_ORANGE : C_LANE_DARK;
                    else if (lv == 2)   col = C_GREEN;
                    else                col = C_YELLOW;
                } else if (aca_on)      col = C_GREEN;
                else                    col = lane_idle;
                set_img_color(side ? mn_lane_r : mn_lane_l, col);
            }
            bool veh = !lim_on;                          /* при лимитере машин и радара нет — только полосы */
            vis(mn_own, veh);
            if (veh) set_img_color(mn_own, car);
            vis(mn_lead, veh && obj);
            if (veh && obj) set_img_color(mn_lead, obj_warn ? C_OBJ_WARN : car);
            vis(mn_arcs, veh && arcs);
            if (veh && arcs) set_img_color(mn_arcs, car);
        }
        bool tl = (swa_i | swa_w) & 1, tr = (swa_i | swa_w) & 2;
        if (swa_w) { bool ph = (now % 400) < 200; tl = tl && ph; tr = tr && ph; }
        vis(mn_swa_l, tl);
        vis(mn_swa_r, tr);

        /* поворотники: такт из 0x366, красные при аварийке */
        {
            bool bv  = d.valid & V_BLINK;
            bool haz = bv && (d.blink & BLINK_HAZARD);
            bool sl = bv && (d.blink & BLINK_L_TAKT), sr = bv && (d.blink & BLINK_R_TAKT);
            uint32_t bcol = haz ? C_BLINK_RED : C_BLINK_GRN;
            set_img_color(mn_bl_fill[0], bcol);
            set_img_color(mn_bl_fill[1], bcol);
            vis(mn_bl_out[0], sl); vis(mn_bl_fill[0], sl);
            vis(mn_bl_out[1], sr); vis(mn_bl_fill[1], sr);
        }

        /* заданная скорость ACC / лимитера под иконкой */
        bool lim_num = lim_on && lim_set;
        bool acc_num = !lim_on && acc_on && acc_set;
        vis(mn_lim, lim_on);
        if (lim_on) set_img_color(mn_lim, lim_set ? 0xffffff : C_GRAY);
        if (lim_num || acc_num) {
            unsigned raw = lim_num ? d.limiter_raw : d.acc_set_raw;
            snprintf(buf, sizeof buf, "%u", spd((raw * 32u + 50u) / 100u));
            bool dim = acc_num && d.acc_status == 2;
            int left;
            int w = mn_digits(mn_set_num, &hud_font_mdist, buf, CX_SET, DIST_BASE, &left);
            set_text_color(mn_set_num, dim ? 0x828282 : 0xffffff);
            set_text_color(mn_set_unit, dim ? 0x6e6e6e : C_UNIT_SMALL);
            mn_text_at(mn_set_unit, &hud_font_munit20, g_units == HUD_UNITS_MI ? "mph" : tx->kmh, left + w + 5, DIST_BASE);
        } else {
            vis(mn_set_num, false);
            vis(mn_set_unit, false);
        }
    }
}

/* ============================================================================
   Построение экрана
   ========================================================================== */
void build_hud_mockup(void)
{
    if (HUD_LOG_HUD) arduino_printf("[hud] build_hud_mockup(), free heap=%u\n", (unsigned)esp_get_free_heap_size());

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    mn_build(scr);

    /* Экран обновления ПО (портал): создаётся последним — лежит поверх обычного экрана. */
    ota_panel = lv_obj_create(scr);
    lv_obj_remove_style_all(ota_panel);
    lv_obj_set_size(ota_panel, HUD_PANEL_W, HUD_PANEL_H);
    lv_obj_set_pos(ota_panel, 0, 0);
    lv_obj_set_style_bg_color(ota_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ota_panel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ota_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    ota_title = lv_label_create(ota_panel);
    lv_obj_set_style_text_font(ota_title, &hud_font_menu, 0);
    lv_obj_set_style_text_color(ota_title, lv_color_white(), 0);
    lv_obj_align(ota_title, LV_ALIGN_TOP_MID, 0, 22);
    ota_status = lv_label_create(ota_panel);
    lv_obj_set_style_text_font(ota_status, &hud_font_menu, 0);
    lv_obj_set_style_text_color(ota_status, lv_color_hex(C_NAV_ARROW), 0);
    lv_obj_set_style_text_align(ota_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(ota_status, HUD_PANEL_W);
    lv_obj_align(ota_status, LV_ALIGN_TOP_MID, 0, 56);
    ota_bar_bg = lv_obj_create(ota_panel);
    lv_obj_remove_style_all(ota_bar_bg);
    lv_obj_set_style_bg_opa(ota_bar_bg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ota_bar_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ota_bar_bg, OTA_BAR_W, 12);
    lv_obj_align(ota_bar_bg, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_radius(ota_bar_bg, 6, 0);
    lv_obj_set_style_bg_color(ota_bar_bg, lv_color_hex(0x30343a), 0);
    ota_bar_fill = lv_obj_create(ota_bar_bg);
    lv_obj_remove_style_all(ota_bar_fill);
    lv_obj_set_style_bg_opa(ota_bar_fill, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ota_bar_fill, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ota_bar_fill, 1, 12);
    lv_obj_set_pos(ota_bar_fill, 0, 0);
    lv_obj_set_style_radius(ota_bar_fill, 6, 0);
    lv_obj_set_style_bg_color(ota_bar_fill, lv_color_hex(C_NAV_ARROW), 0);
    lv_obj_add_flag(ota_panel, LV_OBJ_FLAG_HIDDEN);

    lv_timer_create(hud_update_cb, 50, NULL);
    if (HUD_LOG_HUD) arduino_printf("[hud] screen built, lv_timer 50 ms started\n");
}
