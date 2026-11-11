/*
  hud_time.h — время машины из кадра CAN 0x6B2 (Diagnose_01: дата, время, пробег; раз в секунду)
  ------------------------------------------------------------------------------
  Время принимается только после HUD_TIME_CONFIRM согласованных кадров подряд (±2 с между кадрами):
  защита от разовых глюков. Часы приборки показываются «как есть» (местное время или UTC — как настроено в машине);
  между кадрами время идёт по внутреннему таймеру.
*/
#ifndef HUD_TIME_H
#define HUD_TIME_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define HUD_TIME_CONFIRM 3

typedef struct { uint16_t year; uint8_t mon, day, hour, min, sec; } hud_dt_t;

/* Кадр 0x6B2 (8 байт). now_ms — монотонное время, мс (hud_now_ms()). */
void     hud_time_on_6b2(const uint8_t *d, uint32_t now_ms);
/* Секунды от 1970-01-01 00:00:00 по часам машины; 0 — время ещё не получено. */
uint32_t hud_time_epoch(uint32_t now_ms);

uint32_t hud_time_join(const hud_dt_t *dt);            /* дата -> секунды */
void     hud_time_split(uint32_t epoch, hud_dt_t *out); /* секунды -> дата */

#ifdef __cplusplus
}
#endif
#endif
