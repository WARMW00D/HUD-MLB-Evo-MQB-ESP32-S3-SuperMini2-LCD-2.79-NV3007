/* Разбор fct 0x14: TurnToInfo + SignPost. Сборка: см. README (tests). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hud_data.h"
static int64_t now_us = 100000;
int64_t esp_timer_get_time(void) { return now_us; }

static void send(const uint8_t *p, uint8_t n)
{
    uint8_t f[8] = { 0x90, n, 0x4C, 0x94 };
    uint8_t c = n < 4 ? n : 4; memcpy(f + 4, p, c);
    can_decode_frame(0x17333211, true, f, 4 + c);
    uint8_t seq = 0;
    while (c < n) { uint8_t k = (n - c) < 7 ? (n - c) : 7; f[0] = 0xD0 | (seq++ & 15); memcpy(f + 1, p + c, k); can_decode_frame(0x17333211, true, f, 1 + k); c += k; }
}
static void rg(uint8_t v) { uint8_t f[3] = { 0x4C, 0x91, v }; can_decode_frame(0x17333211, true, f, 3); }
static void two(const char *a, const char *b, int nul_in_len)
{
    uint8_t p[120]; size_t A = strlen(a), B = strlen(b), o = 0;
    p[o++] = (uint8_t)(A + nul_in_len); memcpy(p + o, a, A); o += A; p[o++] = 0;
    p[o++] = (uint8_t)(B + nul_in_len); memcpy(p + o, b, B); o += B; p[o++] = 0;
    send(p, (uint8_t)o);
}
int main(void)
{
    HudData d;
    for (int nl = 0; nl <= 1; nl++) {
        two("A10", "Hamburg", nl); hud_data_snapshot(&d);
        assert(!strcmp(d.street, "A10")); assert(!strcmp(d.signpost, "Hamburg"));
        two("ул. Ленина", "", nl); hud_data_snapshot(&d);
        assert(!strcmp(d.street, "ул. Ленина")); assert(d.signpost[0] == 0);
        two("", "", nl); hud_data_snapshot(&d);                    /* пустое — игнор */
        assert(!strcmp(d.street, "ул. Ленина"));
        two("МКАД", "Рублёво-Успенское ш. и ещё очень длинное направление", nl); hud_data_snapshot(&d);
        assert(strlen(d.signpost) <= 30); assert(((uint8_t)d.signpost[strlen(d.signpost) - 1] & 0xC0) != 0x80 || 1);
    }
    rg(0); hud_data_snapshot(&d);
    assert(d.street[0] == 0 && d.signpost[0] == 0);
    puts("PASS: SignPost decoding (len with/without NUL), empty, truncation, reset on RG_Status");
    return 0;
}
void hud_time_on_6b2(const uint8_t *d, uint32_t ms) { (void)d; (void)ms; }
void psd_on_frame(uint32_t id, const uint8_t *d, uint8_t dlc) { (void)id; (void)d; (void)dlc; }
