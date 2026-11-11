#include "hud_time.h"

static uint32_t sig(const uint8_t *d, int start, int len)
{
    uint64_t raw = 0;
    for (int i = 7; i >= 0; i--) raw = (raw << 8) | d[i];
    return (uint32_t)((raw >> start) & ((1ULL << len) - 1));
}

/* Григорианский календарь без таблиц часовых поясов (алгоритм Хиннанта) */
static int32_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

static void civil_from_days(int32_t z, int *y, int *m, int *d)
{
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int yy = (int)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = yy + (*m <= 2);
}

uint32_t hud_time_join(const hud_dt_t *t)
{
    const int32_t days = days_from_civil(t->year, t->mon, t->day);
    return (uint32_t)days * 86400u + (uint32_t)t->hour * 3600u + (uint32_t)t->min * 60u + t->sec;
}

void hud_time_split(uint32_t e, hud_dt_t *o)
{
    const int32_t days = (int32_t)(e / 86400u);
    const uint32_t s = e % 86400u;
    int y, m, d;
    civil_from_days(days, &y, &m, &d);
    o->year = (uint16_t)y; o->mon = (uint8_t)m; o->day = (uint8_t)d;
    o->hour = (uint8_t)(s / 3600u); o->min = (uint8_t)(s % 3600u / 60u); o->sec = (uint8_t)(s % 60u);
}

static bool valid(const hud_dt_t *t)
{
    static const uint8_t dim[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (t->year < 2024 || t->year > 2045 || t->mon < 1 || t->mon > 12) return false;
    if (t->day < 1 || t->day > dim[t->mon - 1] || t->hour > 23 || t->min > 59 || t->sec > 59) return false;
    if (t->mon == 2 && t->day == 29 && !((t->year % 4 == 0 && t->year % 100 != 0) || t->year % 400 == 0)) return false;
    return true;
}

/* опорная точка: (эпоха, момент приёма); публикуется с «seqlock», читает другая задача */
static volatile uint32_t s_ver, s_anchor_epoch, s_anchor_ms;
static uint32_t s_cand_epoch, s_cand_ms;       /* предыдущий кадр */
static uint8_t  s_cand_n;                      /* сколько кадров подряд согласованы */

void hud_time_on_6b2(const uint8_t *d, uint32_t now_ms)
{
    hud_dt_t t;
    t.year = (uint16_t)(2000 + sig(d, 28, 7)); t.mon = (uint8_t)sig(d, 35, 4); t.day = (uint8_t)sig(d, 39, 5);
    t.hour = (uint8_t)sig(d, 44, 5); t.min = (uint8_t)sig(d, 49, 6); t.sec = (uint8_t)sig(d, 55, 6);
    if (!valid(&t)) { s_cand_n = 0; return; }
    const uint32_t e = hud_time_join(&t);
    if (s_cand_n) {
        const int32_t de = (int32_t)(e - s_cand_epoch);
        const int32_t dm = (int32_t)((now_ms - s_cand_ms + 500u) / 1000u);
        const int32_t diff = de - dm;
        s_cand_n = (diff >= -2 && diff <= 2) ? (uint8_t)(s_cand_n < 255 ? s_cand_n + 1 : 255) : 1;
    } else {
        s_cand_n = 1;
    }
    s_cand_epoch = e; s_cand_ms = now_ms;
    if (s_cand_n >= HUD_TIME_CONFIRM) {
        s_ver++;                                   /* нечётная — идёт запись */
        __sync_synchronize();
        s_anchor_epoch = e; s_anchor_ms = now_ms;
        __sync_synchronize();
        s_ver++;
    }
}

uint32_t hud_time_epoch(uint32_t now_ms)
{
    uint32_t e, ms, v1, v2;
    do {
        v1 = s_ver; __sync_synchronize();
        e = s_anchor_epoch; ms = s_anchor_ms;
        __sync_synchronize(); v2 = s_ver;
    } while (v1 != v2 || (v1 & 1u));
    return e ? e + (now_ms - ms) / 1000u : 0;
}
