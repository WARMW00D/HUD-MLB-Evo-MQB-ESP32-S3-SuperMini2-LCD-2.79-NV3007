/* LaneGuidance: все активные массивы из поездки 2026-11-21 и правила обновления состояния. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hud_data.h"

static int64_t now_us = 100000;
int64_t esp_timer_get_time(void) { return now_us; }

static int hex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    return c - 'A' + 10;
}

static void send_bap(uint16_t hdr, const uint8_t *p, uint16_t n)
{
    uint8_t f[8] = { 0x80 | ((n >> 8) & 0x0F), (uint8_t)n, (uint8_t)(hdr >> 8), (uint8_t)hdr };
    uint16_t c = n < 4 ? n : 4;
    memcpy(f + 4, p, c);
    can_decode_frame(0x17333211, true, f, 4 + c);
    uint8_t seq = 0;
    while (c < n) {
        uint8_t k = n - c < 7 ? n - c : 7;
        f[0] = 0xC0 | (seq++ & 15);
        memcpy(f + 1, p + c, k);
        can_decode_frame(0x17333211, true, f, 1 + k);
        c += k;
    }
}

static void send_hex(const char *s)
{
    uint8_t p[64];
    uint8_t n = 0;
    while (*s) {
        p[n++] = (uint8_t)((hex(s[0]) << 4) | hex(s[1]));
        s += 2;
        if (*s) s++;
    }
    send_bap(0x4C98, p, n);
}

static void send_lane2(uint8_t mode, uint8_t count, bool valid_pos, uint8_t sides)
{
    uint8_t p[512];
    uint16_t o = 0;
    p[o++] = 0x10;                         /* ASG_ID / TAID */
    p[o++] = 1;                            /* LaneGuidanceOnOff */
    p[o++] = 2;                            /* CurrentCarPosition */
    p[o++] = 15;                           /* RecommendedCarPosition */
    p[o++] = (uint8_t)(mode << 4);           /* Mode = 0 / Pos transmitted = 4, старший полубайт */
    p[o++] = 0;                            /* Start */
    p[o++] = count;
    for (uint8_t i = 0; i < count; i++) {
        if (mode & 0x04) p[o++] = valid_pos ? i : (uint8_t)(i + 1);
        p[o++] = i < 8 ? 1 : 2;            /* LaneFocus */
        p[o++] = 0;                        /* LaneDirection1: straight */
        p[o++] = i == count - 1 ? 0xE0 : 0xFF;
        uint8_t g1 = i == 1 ? 2 : 0, g2 = i == count - 1 ? 2 : 0;
        p[o++] = (uint8_t)((g2 << 4) | g1);
        p[o++] = sides;                      /* LaneSidestreets.length */
        for (uint8_t k = 0; k < sides; k++) p[o++] = (uint8_t)(k * 8);
        p[o++] = i == 2 ? 0x17 : 1;        /* LaneType; 0x17 = hide in LaneGuidance2 */
        p[o++] = 0;                        /* LaneMarking_left/right */
        p[o++] = i == 0 ? 3 : 0;           /* LaneAttributes */
        p[o++] = i == 0 ? 0xFF : 0;        /* LaneDescription: full byte */
    }
    send_bap(0x4CBD, p, o);
}

static const char *const active_cases[] = {
    "11 01 00 00 04 00 00 01 00 00 00 00 01 00 00 00 00 01 00 00 E0 00 01 00 02",
    "13 01 00 00 03 20 01 E0 01 00 00 E0 00 01 00 02 E0 00 01 00 02",
    "14 01 00 00 03 00 00 01 00 00 00 00 01 00 02 00 01 C0 01 00 02",
    "15 01 00 00 04 20 00 01 00 00 20 00 01 00 00 00 00 01 00 02 00 00 01 00 02",
    "17 01 00 00 06 00 00 01 00 02 00 00 01 00 02 00 00 01 00 02 00 00 01 00 02 00 00 01 00 02 E0 00 01 00 00",
    "19 01 00 00 05 00 00 01 00 00 00 00 01 00 00 00 00 01 00 00 00 00 01 00 00 E0 01 00 01 00 02",
    "1A 01 00 00 02 00 00 01 00 00 00 00 01 00 02",
    "1B 01 00 00 02 E0 00 01 00 00 E0 00 01 00 02",
    "1C 01 00 00 03 20 00 01 00 00 20 00 01 00 00 C0 01 20 01 00 02",
    "13 01 00 00 02 00 01 40 01 00 00 C0 01 00 01 00 02",
    "14 01 00 00 02 E0 00 01 00 02 E0 00 01 00 00",
    "16 01 00 00 05 00 00 01 00 00 00 00 01 00 00 00 00 01 00 00 00 00 01 00 00 E0 00 01 00 02",
    "18 01 00 00 06 00 00 01 00 02 00 00 01 00 02 00 00 01 00 02 00 00 01 00 02 00 01 E0 01 00 02 E0 00 01 00 00",
    "1A 01 00 00 08 00 00 01 00 00 00 00 01 00 00 00 00 01 00 00 00 00 01 00 00 00 00 01 00 00 C0 00 01 00 00 C0 00 01 00 02 C0 00 01 00 00",
    "1B 01 00 00 03 00 00 01 00 00 E0 01 00 01 00 02 E0 00 01 00 00",
    "1D 01 00 00 04 00 00 01 00 00 00 00 01 00 00 00 00 01 00 00 E0 00 01 00 02"
};

int main(void)
{
    static const uint8_t counts[] = { 4, 3, 3, 4, 6, 5, 2, 2, 3, 2, 2, 5, 6, 8, 3, 4 };
    static const uint8_t recommended[] = { 0x08, 0x06, 0x06, 0x0C, 0x1F, 0x10, 0x02, 0x02,
                                           0x04, 0x02, 0x01, 0x10, 0x1F, 0x40, 0x02, 0x08 };
    HudData d;
    for (uint8_t i = 0; i < 16; i++) {
        send_hex(active_cases[i]);
        hud_data_snapshot(&d);
        assert((d.valid & V_LANES) && d.lane_count == counts[i]);
        uint8_t mask = 0;
        for (uint8_t k = 0; k < d.lane_count; k++)
            if (d.lanes[k].guidance == 2) mask |= 1u << k;
        assert(mask == recommended[i]);
    }

    send_hex(active_cases[1]);
    hud_data_snapshot(&d);
    assert(d.lanes[0].direction == 0x20 && d.lanes[0].side_count == 1 && d.lanes[0].side[0] == 0xE0);
    assert(d.lanes[0].guidance == 0 && d.lanes[1].guidance == 2 && d.lanes[2].guidance == 2);

    const uint8_t hb_on[] = { 1, 0, 0, 0xFF };
    send_bap(0x3C98, hb_on, sizeof hb_on);
    now_us += 30000000;
    hud_data_snapshot(&d);
    assert((d.valid & V_LANES) && d.lane_count == 3);

    send_hex("13 01 01 00 03 20 01 E0 01 00 00");
    hud_data_snapshot(&d);
    assert(d.lane_count == 3 && d.lanes[0].direction == 0x20);
    send_hex("13 01 00 00 02 00 00 01 00 02");
    hud_data_snapshot(&d);
    assert(d.lane_count == 3 && d.lanes[0].side_count == 1);

    const uint8_t status_off[] = { 0x12, 0, 0, 0, 0 };
    send_bap(0x4C98, status_off, sizeof status_off);
    hud_data_snapshot(&d);
    assert(!(d.valid & V_LANES) && d.lane_count == 0);

    send_hex(active_cases[13]);
    const uint8_t hb_off[] = { 0, 0, 0, 0xFF };
    send_bap(0x3C98, hb_off, sizeof hb_off);
    hud_data_snapshot(&d);
    assert(!(d.valid & V_LANES) && d.lane_count == 0);

    send_hex(active_cases[0]);
    uint8_t rg_off[] = { 0x4C, 0x91, 0 };
    can_decode_frame(0x17333211, true, rg_off, sizeof rg_off);
    hud_data_snapshot(&d);
    assert(!(d.valid & V_LANES) && d.lane_count == 0);

    send_hex(active_cases[0]);
    now_us += 60000000;
    hud_data_snapshot(&d);
    assert(!(d.valid & V_LANES));

    /* LaneGuidance2, синтетические сообщения по Function Catalogue 51.7: реальных кадров 0x3D в логах нет. */
    send_lane2(0, 16, true, 16);
    hud_data_snapshot(&d);
    assert((d.valid & V_LANES) && d.lane_count == 16);
    assert(d.lanes[0].focus == 1 && d.lanes[15].focus == 2);
    assert(d.lanes[0].side_count == 16 && d.lanes[0].attributes == 3 && d.lanes[0].description == 0xFF);
    assert(d.lanes[0].direction2 == 0xFF && d.lanes[15].direction2 == 0xE0);
    assert(d.lanes[1].guidance == 2 && d.lanes[15].guidance == 2 && d.lanes[15].guidance2 == 2);
    assert(d.lanes[2].hidden && !d.lanes[1].hidden);

    send_lane2(4, 16, true, 16);                 /* Pos присутствует; payload > 256 байт */
    hud_data_snapshot(&d);
    assert((d.valid & V_LANES) && d.lane_count == 16 && d.lanes[15].focus == 2);

    send_lane2(4, 16, false, 16);                /* неверный Pos: не портить последнее состояние */
    hud_data_snapshot(&d);
    assert(d.lane_count == 16 && d.lanes[0].attributes == 3);
    send_lane2(4, 17, true, 16);                 /* выше MaxElements = 16 */
    hud_data_snapshot(&d);
    assert(d.lane_count == 16 && d.lanes[0].attributes == 3);

    const uint8_t hb2_on[] = { 1, 0, 0, 0xFF };
    send_bap(0x3CBD, hb2_on, sizeof hb2_on);
    hud_data_snapshot(&d);
    assert((d.valid & V_LANES) && d.lane_count == 16);
    const uint8_t hb2_off[] = { 0, 0, 0, 0xFF };
    send_bap(0x3CBD, hb2_off, sizeof hb2_off);
    hud_data_snapshot(&d);
    assert(!(d.valid & V_LANES) && d.lane_count == 0);

    send_lane2(4, 16, true, 16);
    const uint8_t status2_off[] = { 0x10, 0, 0, 0, 0, 0, 0 };
    send_bap(0x4CBD, status2_off, sizeof status2_off);
    hud_data_snapshot(&d);
    assert(!(d.valid & V_LANES) && d.lane_count == 0);

    send_lane2(4, 16, true, 0);            /* короткие записи без Sidestreets */
    hud_data_snapshot(&d);
    assert((d.valid & V_LANES) && d.lane_count == 16);

    puts("PASS: LaneGuidance 16 real arrays + LaneGuidance2 synthetic 16 lanes, Pos modes, dual directions, off, malformed");
    return 0;
}

void hud_time_on_6b2(const uint8_t *d, uint32_t ms) { (void)d; (void)ms; }
void psd_on_frame(uint32_t id, const uint8_t *d, uint8_t dlc) { (void)id; (void)d; (void)dlc; }
