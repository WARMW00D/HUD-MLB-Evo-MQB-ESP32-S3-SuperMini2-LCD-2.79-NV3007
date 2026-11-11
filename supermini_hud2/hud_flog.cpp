/*
  hud_flog.cpp — см. hud_flog.h
  Очередь в RAM (кольцо 16 КБ) -> фоновая задача раз в 250 мс дописывает во флешь через ядро hud_flog_core.
  Метки времени ставятся в момент вызова hud_log_write(), а не при записи во флешь.
*/
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "user_config.h"
#include "hud_config.h"
#include "hud_data.h"
#include "hud_cams.h"
#include "hud_flog.h"
#include "hud_flog_core.h"
#include "hud_time.h"

#if HUD_FLASH_LOG

#define RING_BYTES 16384u

static uint8_t s_ring[RING_BYTES];
static volatile uint32_t s_head = 0, s_tail = 0;     /* head — запись, tail — чтение (индексы без обрезки) */
static volatile uint32_t s_dropped = 0;
static bool s_bol = true;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static flog_t *s_f;                                   /* в куче: внутри 4 КБ буфер */
static SemaphoreHandle_t s_mtx;
static volatile bool s_ready = false;
static flog_tar_t s_tar;
static bool s_tar_open = false;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* ---- очередь ---- */
extern "C" void hud_flog_push(const char *s)
{
    if (!s || !*s) return;
    const uint32_t ms = now_ms();
    const uint32_t ep = hud_time_epoch(ms);
    char pre[48];
    int pn = 0;
    if (ep) {
        hud_dt_t t; hud_time_split(ep, &t);
        pn = snprintf(pre, sizeof pre, "%04u-%02u-%02u %02u:%02u:%02u ", t.year, t.mon, t.day, t.hour, t.min, t.sec);
    }
    pn += snprintf(pre + pn, sizeof pre - pn, "[%lu.%03lu] ", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));

    portENTER_CRITICAL(&s_mux);
    for (; *s; s++) {
        if (s_bol) {
            for (int i = 0; i < pn; i++) {
                if (s_head - s_tail >= RING_BYTES) { s_dropped++; continue; }
                s_ring[s_head++ % RING_BYTES] = (uint8_t)pre[i];
            }
        }
        if (s_head - s_tail >= RING_BYTES) s_dropped++;
        else s_ring[s_head++ % RING_BYTES] = (uint8_t)*s;
        s_bol = (*s == '\n');
    }
    portEXIT_CRITICAL(&s_mux);
}

static size_t ring_take(uint8_t *dst, size_t n)
{
    size_t k = 0;
    portENTER_CRITICAL(&s_mux);
    while (k < n && s_tail != s_head) dst[k++] = s_ring[s_tail++ % RING_BYTES];
    portEXIT_CRITICAL(&s_mux);
    return k;
}

/* ---- ввод-вывод ядра: драйвер флеши из hud_cams.cpp ---- */
static bool io_read(uint32_t a, uint8_t *d, size_t n, void *) { return hud_cams_flash_read(a, d, n); }
static bool io_erase(uint32_t a, void *)                       { return hud_cams_flash_erase(a); }
static bool io_write(uint32_t a, const uint8_t *s, size_t n, void *) { return hud_cams_flash_write(a, s, n); }

static bool mount(uint32_t base)
{
    static const flog_io_t io = { io_read, io_erase, io_write, nullptr };
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    bool ok = flog_mount(s_f, &io, base);
    xSemaphoreGive(s_mtx);
    return ok;
}

static void put_line(const char *s) { hud_flog_push(s); }

static void flog_task(void *)
{
    const uint32_t cap = hud_cams_flash_capacity();
    if (cap < 2u * FLOG_TOTAL_BYTES) { Serial.println("[flog] нет флеши или она мала — журнал во флеше выключен"); vTaskDelete(nullptr); }
    const uint32_t base = cap - FLOG_TOTAL_BYTES;
    while (!mount(base)) {
        Serial.println("[flog] не удалось смонтировать журнал, повтор через 5 с");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    s_ready = true;
    {
        char b[96];
        snprintf(b, sizeof b, "[flog] hud.log.%04u, продолжаем с позиции %u/%u\n", (unsigned)s_f->seq, (unsigned)s_f->sector, (unsigned)s_f->off);
        put_line(b);
    }

    static uint8_t buf[512];
    bool time_logged = false;
    uint32_t last_hb = now_ms();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(250));
        const uint32_t ms = now_ms();
        const uint32_t ep = hud_time_epoch(ms);

        if (ep && !time_logged) {                           /* время впервые получено по CAN */
            time_logged = true;
            hud_dt_t t; hud_time_split(ep, &t);
            char b[80];
            snprintf(b, sizeof b, "[time] время машины по CAN: %04u-%02u-%02u %02u:%02u:%02u\n", t.year, t.mon, t.day, t.hour, t.min, t.sec);
            put_line(b);
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            flog_set_epoch_if_unknown(s_f, ep);             /* время начала файла (mtime в .tar) */
            xSemaphoreGive(s_mtx);
        }
#if HUD_FLASH_LOG_HEARTBEAT_S > 0
        if ((uint32_t)(ms - last_hb) >= HUD_FLASH_LOG_HEARTBEAT_S * 1000u) {
            last_hb = ms;
            HudData d; hud_data_snapshot(&d);
            char b[120];
            snprintf(b, sizeof b, "[hb] heap=%u psram=%u v=%d кадров=%lu cams=%d\n",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (d.valid & V_SPEED) ? (int)d.speed_kmh : -1, (unsigned long)d.frames_rx, hud_cams_ready() ? 1 : 0);
            put_line(b);
        }
#endif
        if (s_dropped) {
            char b[64];
            const uint32_t n = s_dropped; s_dropped = 0;
            snprintf(b, sizeof b, "[flog] потеряно байт (очередь): %lu\n", (unsigned long)n);
            put_line(b);
        }
        size_t k;
        while ((k = ring_take(buf, sizeof buf)) != 0) {
            xSemaphoreTake(s_mtx, portMAX_DELAY);
            const size_t w = flog_append(s_f, buf, k, ep);
            xSemaphoreGive(s_mtx);
            if (w != k) {                                   /* ошибка флеши: перемонтировать и продолжить */
                Serial.println("[flog] ошибка записи, перемонтирование");
                s_ready = false;
                int tries = 0;
                while (!mount(base) && ++tries < 3) vTaskDelay(pdMS_TO_TICKS(500));
                s_ready = tries < 3;
                if (!s_ready) vTaskDelay(pdMS_TO_TICKS(5000));
                break;
            }
        }
    }
}

extern "C" void hud_flog_start(void)
{
    s_mtx = xSemaphoreCreateMutex();
    s_f = (flog_t *)heap_caps_malloc(sizeof(flog_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_mtx || !s_f) { Serial.println("[flog] нет памяти"); return; }
    xTaskCreatePinnedToCore(flog_task, "hud_flog", 4096, nullptr, 1, nullptr, 0);
}

extern "C" bool hud_flog_ready(void) { return s_ready; }

extern "C" bool hud_flog_tar_open(uint32_t *total)
{
    if (!s_ready || !s_mtx) return false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    flog_tar_begin(s_f, &s_tar);
    xSemaphoreGive(s_mtx);
    s_tar_open = flog_tar_total(&s_tar) != 0;
    if (s_tar_open && total) *total = flog_tar_total(&s_tar);
    return s_tar_open;
}

extern "C" size_t hud_flog_tar_read(uint8_t *dst, size_t n)
{
    if (!s_tar_open) return 0;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    const size_t k = flog_tar_read(s_f, &s_tar, dst, n);
    xSemaphoreGive(s_mtx);
    if (!k) s_tar_open = false;
    return k;
}

#else   /* HUD_FLASH_LOG == 0 */
extern "C" void   hud_flog_start(void) {}
extern "C" void   hud_flog_push(const char *) {}
extern "C" bool   hud_flog_ready(void) { return false; }
extern "C" bool   hud_flog_tar_open(uint32_t *) { return false; }
extern "C" size_t hud_flog_tar_read(uint8_t *, size_t) { return 0; }
#endif
