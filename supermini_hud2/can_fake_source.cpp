/*
  can_fake_source.cpp — встроенный генератор CAN-кадров для проверки HUD без машины (HUD_DATA_SOURCE = HUD_SRC_FAKE)
*/
#include <Arduino.h>
#include <string.h>
#include "hud_config.h"
#include "hud_data.h"
#include "hud_source.h"
#include "hud_log.h"

#if HUD_DATA_SOURCE == HUD_SRC_FAKE


static void put_sig(uint8_t *d, int start, int len, uint32_t v)
{
    uint64_t raw = 0;
    for (int i = 7; i >= 0; i--) raw = (raw << 8) | d[i];
    uint64_t m = ((1ULL << len) - 1) << start;
    raw = (raw & ~m) | (((uint64_t)v << start) & m);
    for (int i = 0; i < 8; i++) d[i] = (raw >> (8 * i)) & 0xFF;
}

static void bap_send_single(uint8_t fct, const uint8_t *p, uint8_t n)   /* n <= 6 */
{
    uint8_t f[8] = { 0x4C, (uint8_t)(0x80 | fct) };   /* opcode 4, LSG 0x32 */
    memcpy(f + 2, p, n);
    can_decode_frame(0x17333210, true, f, 2 + n);
}

static void bap_send_multi(uint8_t ch, uint8_t fct, const uint8_t *p, uint8_t n) /* n <= 255 */
{
    uint8_t f[8] = { (uint8_t)(0x80 | (ch << 4)), n, 0x4C, (uint8_t)(0x80 | fct) };
    uint8_t c = n < 4 ? n : 4;
    memcpy(f + 4, p, c);
    can_decode_frame(0x17333210, true, f, 4 + c);
    uint8_t seq = 0;
    while (c < n) {
        uint8_t k = (n - c) < 7 ? (n - c) : 7;
        f[0] = 0xC0 | (ch << 4) | (seq++ & 0x0F);
        memcpy(f + 1, p + c, k);
        can_decode_frame(0x17333210, true, f, 1 + k);
        c += k;
    }
}

static void fake_task(void *)
{
    uint32_t t0 = millis(), last_nav = 0;
    for (;;) {
        uint32_t t = millis() - t0;
        uint8_t d[8];

        /* скорость: пила 0..160 км/ч за 32 с (KBI_angez_Geschw — с шагом 0.32 км/ч) */
        uint32_t v_kmh = (t / 200) % 161;
        memset(d, 0, 8); put_sig(d, 24, 9, v_kmh); put_sig(d, 48, 10, (t % 32200) / 64);   /* та же пила, 0.32 км/ч */
        can_decode_frame(0x30B, false, d, 8);

        /* название улицы (BAP fct 0x14): короткое влезает, длинные — бегущая строка */
        {
            static const char *const streets[] = { "ул. Ленина", "Крылатская улица", "МКАД 39-й км Внутренняя Сторона", "просп. Маршала Жукова" };
            static int idx = -1; static uint32_t last = 0;
            const int i = (int)((t / 12000) % 4);
            if (i != idx || t - last > 10000) {
                idx = i; last = t;
                uint8_t p[66]; const size_t L = strlen(streets[i]);
                p[0] = (uint8_t)L; memcpy(p + 1, streets[i], L); p[1 + L] = 0;
                bap_send_multi(1, 0x14, p, (uint8_t)(L + 2));
            }
        }

        /* счётчик топлива: 6 л/100 км + 2 л/ч на холостых; > 20 л/100 км на разгоне с места */
        {
            static uint32_t kvs_ul = 0; static float frac = 0;
            float lph = 2.0f + 6.0f * v_kmh / 100.0f + (v_kmh < 20 ? 6.0f : 0.0f);
            frac += lph * 1e6f / 3600.0f / 20.0f;                /* мкл за 50 мс */
            kvs_ul += (uint32_t)frac; frac -= (uint32_t)frac;
            memset(d, 0, 8); put_sig(d, 48, 15, kvs_ul & 0x7FFF);
            can_decode_frame(0x107, false, d, 8);
        }

        /* ACC: 120 км/ч (raw 375), статус по кругу: активен / пассив / выкл */
        /* ACC_Tempolimit по кругу раз в 6 с: 60, 120, конец ограничения, нет */
        static const uint8_t tl_seq[4] = { 13, 23, 31, 0 };
        /* цикл 60 с: 0-40 ACC (20-30 с — режим пробки), 40-50 лимитер 80 км/ч, 50-60 лимитер без скорости */
        uint32_t c60 = t % 60000;
        bool lim = c60 >= 40000;
        memset(d, 0, 8); put_sig(d, 12, 10, lim ? 1023 : 375); put_sig(d, 0, 5, tl_seq[(t / 6000) % 4]);
        put_sig(d, 45, 2, (t / 3000) % 2);                              /* машина впереди есть/нет */
        put_sig(d, 37, 3, 3);
        put_sig(d, 62, 2, (c60 >= 20000 && c60 < 30000) ? 2 : 0);      /* STA активен */
        can_decode_frame(0x2A6, false, d, 8);
        memset(d, 0, 8); put_sig(d, 12, 10, !lim ? 1022 : (c60 < 50000 ? 250 : 1023));
        can_decode_frame(0x31E, false, d, 8);

        /* VZE: неизвестный код 0x5A (-> берётся Tempolimit), превышение, если скорость > 100 */
        memset(d, 0, 8); put_sig(d, 11, 8, 0x5A); put_sig(d, 35, 1, ((t / 200) % 161) > 100);
        can_decode_frame(0x181, false, d, 8);

        /* LKA: зелёный / жёлтый / предупреждение справа, по 5 с */
        uint32_t lp = (t / 5000) % 3;
        memset(d, 0, 8); d[1] = 0x40;
        if (lp == 0) put_sig(d, 62, 1, 1);
        if (lp == 1) put_sig(d, 61, 1, 1);
        if (lp == 2) { put_sig(d, 62, 1, 1); put_sig(d, 57, 1, 1); }
        put_sig(d, 38, 2, 2); put_sig(d, 36, 2, 2);
        can_decode_frame(0x397, false, d, 8);
        static const uint8_t acc_seq[3] = { 3, 2, 0 };
        memset(d, 0, 8); put_sig(d, 16, 3, lim ? 0 : acc_seq[(t / 8000) % 3]);
        can_decode_frame(0x2A8, false, d, 8);

        /* поворотники: 0-10 с левый, 10-20 правый, 20-30 аварийка, 30-40 выкл */
        uint32_t ph = (t / 10000) % 4;
        bool takt = (t % 1000) < 500;
        memset(d, 0, 8);
        if (ph == 0 && takt) put_sig(d, 27, 1, 1);
        if (ph == 1 && takt) put_sig(d, 28, 1, 1);
        if (ph == 2) { put_sig(d, 20, 1, 1); if (takt) { put_sig(d, 27, 1, 1); put_sig(d, 28, 1, 1); } }
        can_decode_frame(0x366, false, d, 8);

        /* двери: 40-60 с цикла по очереди FL, FR, RL, RR, капот, багажник, все */
        {
            static const uint8_t dseq[7] = { DOOR_FL, DOOR_FR, DOOR_RL, DOOR_RR, DOOR_HOOD, DOOR_TRUNK, 0x3F };
            uint32_t c = t % 60000;
            hud_data_fake_doors(c >= 40000 ? dseq[((c - 40000) / 3000) % 7] : 0);
        }

        /* pre sense: 30-32 с — предупреждение, 32-34 с — острое */
        memset(d, 0, 8);
        { uint32_t c = t % 60000; put_sig(d, 16, 3, (c >= 30000 && c < 32000) ? 2 : (c >= 32000 && c < 34000) ? 3 : 0); }
        can_decode_frame(0x2A9, false, d, 8);

        /* яркость: «уровень света» по кругу за 60 с: темно (колёсико 10..100),
           день (до 253), солнце (FW в потолке, Boost 15 -> 0) */
        memset(d, 0, 8);
        { uint32_t c = t % 60000; uint32_t xd = c < 20000 ? 10 + c * 90 / 20000 : c < 40000 ? 100 + (c - 20000) * 153 / 20000 : 253;
          put_sig(d, 0, 8, xd); put_sig(d, 15, 1, xd <= 100);
          uint8_t r[8] = {0}; uint32_t fw = xd <= 100 ? 0 : (xd - 99) * 1021 / 154; uint32_t boost = c < 40000 ? 15 : 15 - (c - 40000) * 15 / 20000;
          put_sig(r, 8, 10, fw > 1021 ? 1021 : fw); put_sig(r, 35, 4, boost);
          can_decode_frame(0x5A0, false, r, 8);
          uint8_t w[8] = {0}; put_sig(w, 25, 7, 1 + (t / 100) % 100);          /* колёсико 1..100 за 10 с */
          can_decode_frame(0x64F, false, w, 8); }
        can_decode_frame(0x5F0, false, d, 8);

        /* BAP_BC «с момента запуска» раз в 4 с: 18 байт, многокадровое, LSG 0x0F */
        {
            static uint32_t last_bc = 0;
            if (t - last_bc >= 4000) {
                last_bc = t;
                uint8_t bc[18] = {0};
                uint16_t avg = 75 + (t / 4000) % 20;                    /* 7.5..9.4 л/100 */
                uint16_t dist = (uint16_t)(t / 1000);                   /* 0.1 км в секунду */
                bc[0] = avg & 0xFF; bc[1] = avg >> 8; bc[6] = dist & 0xFF; bc[7] = dist >> 8;
                bc[11] = (uint8_t)(t / 60000); bc[15] = 0x68; bc[16] = 0x01;   /* 36.0 км/ч */
                uint8_t f[8] = { 0x80, 18, 0x43, (uint8_t)(0xC0 | 0x18), bc[0], bc[1], bc[2], bc[3] };
                can_decode_frame(0x17330F10, true, f, 8);
                for (uint8_t seq = 0, c = 4; c < 18; seq++) {
                    uint8_t k = (18 - c) < 7 ? (18 - c) : 7;
                    f[0] = 0xC0 | (seq & 0x0F); memcpy(f + 1, bc + c, k);
                    can_decode_frame(0x17330F10, true, f, 1 + k);
                    c += k;
                }
                uint8_t fl[3] = { 0x43, (uint8_t)(0xC0 | 0x1C), (uint8_t)(84 - (t / 20000) % 80) };   /* уровень топлива, % */
                can_decode_frame(0x17330F10, true, fl, 3);
            }
        }

        /* Side Assist: слева машина 10-20 с цикла 30 с, на 15-17 с — попытка перестроения */
        {
            uint32_t c = t % 30000;
            memset(d, 0, 8);
            put_sig(d, 26, 1, c >= 10000 && c < 20000); put_sig(d, 27, 1, c >= 15000 && c < 17000);
            can_decode_frame(0x30F, false, d, 8);
        }

        /* часы: 1 октября 2026, 14:05:00 + время работы генератора */
        {
            uint32_t sec = 14 * 3600 + 5 * 60 + t / 1000;
            memset(d, 0, 8);
            put_sig(d, 28, 7, 26); put_sig(d, 35, 4, 10); put_sig(d, 39, 5, 1);
            put_sig(d, 44, 5, (sec / 3600) % 24); put_sig(d, 49, 6, (sec / 60) % 60); put_sig(d, 55, 6, sec % 60);
        }

        /* PSD: сегмент 10 (загородная дорога), явный знак 70 с 0-й до 40-й с цикла,
           с 20-й по 40-ю — ещё и запрет обгона; потом конец ограничения -> 90 по правилам */
        {
            uint32_t c = t % 60000;
            memset(d, 0, 8); put_sig(d, 0, 6, 10); put_sig(d, 6, 6, 9); put_sig(d, 12, 7, 120);
            put_sig(d, 19, 3, 3); put_sig(d, 38, 1, 1);
            can_decode_frame(0x462, false, d, 8);
            memset(d, 0, 8); put_sig(d, 0, 3, 2); put_sig(d, 3, 6, 10); put_sig(d, 16, 5, c < 40000 ? 13 : 23);
            put_sig(d, 21, 2, 1); put_sig(d, 39, 5, 25); put_sig(d, 44, 5, 25);
            put_sig(d, 49, 2, (c >= 20000 && c < 40000) ? 1 : 0);
            can_decode_frame(0x464, false, d, 8);
            memset(d, 0, 8); put_sig(d, 0, 6, 10); put_sig(d, 6, 7, 50);
            can_decode_frame(0x463, false, d, 8);
        }

        /* передача: P R N D S M по 4 с */
        static const uint8_t gseq[6] = { 1, 2, 3, 4, 5, 6 };
        memset(d, 0, 8); put_sig(d, 12, 4, gseq[(t / 4000) % 6]);
        put_sig(d, 24, 4, 1 + (t / 1000) % 7);                          /* передачи 1..7 */
        can_decode_frame(0x394, false, d, 8);

        /* навигация раз в секунду */
        if (t - last_nav >= 1000) {
            last_nav = t;
            uint8_t rg = 1;
            bap_send_single(0x11, &rg, 1);

            uint32_t md = 5000 - ((t / 100) % 5000);          /* 500.0 -> 0 м */
            uint8_t m[8] = { (uint8_t)md, (uint8_t)(md >> 8), (uint8_t)(md >> 16), (uint8_t)(md >> 24),
                             0, 1, (uint8_t)(md * 100 / 5000), 1 };
            bap_send_multi(0, 0x12, m, 8);

            uint32_t dd = 235;                                 /* 23.5 км */
            uint8_t ds[6] = { (uint8_t)dd, (uint8_t)(dd >> 8), 0, 0, 1, 0x01 };
            bap_send_single(0x15, ds, 6);

            uint8_t tt[7] = { 0x00, 34, 0, 0, 0, 0, 0x06 };   /* 0:34 в пути */
            bap_send_multi(1, 0x16, tt, 7);

            /* манёвры по кругу, по 3 с: все повороты, кольцо, съезды, развилки, разворот, финиш */
            static const uint8_t man_seq[][2] = {
                {0x0B,0x00},{0x0D,0x20},{0x0D,0x40},{0x0D,0x60},{0x0D,0xA0},{0x0D,0xC0},{0x0D,0xE0},
                {0x15,0x00},{0x15,0x40},{0x15,0xC0},{0x15,0x80},{0x0F,0x00},{0x10,0x00},
                {0x13,0x30},{0x14,0xD0},{0x19,0x40},{0x03,0x00},{0x09,0x00},{0x2A,0x50} };
            const uint8_t *mm = man_seq[(t / 3000) % (sizeof(man_seq) / 2)];
            /* как у MIB: 3 манёвра по 4 байта, второй — следующий по списку */
            const uint8_t *m2 = man_seq[((t / 3000) + 1) % (sizeof(man_seq) / 2)];
            uint8_t mn[12] = { mm[0], mm[1], 0, 0,  m2[0], m2[1], 0, 0,  0, 0, 0, 0 };
            bap_send_multi(2, 0x17, mn, 12);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void fake_source_start(void)
{
    hud_log_write("[FAKE] генератор кадров вместо CAN\n");
    hud_src_set_up(HUD_SRC_ID_FAKE, true);
    xTaskCreatePinnedToCore(fake_task, "fake_can", 4096, nullptr, 3, nullptr, 1);
}

#endif  /* HUD_SRC_FAKE */
