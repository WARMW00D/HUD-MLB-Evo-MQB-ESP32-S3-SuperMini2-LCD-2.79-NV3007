/*
  hud_flog.h — журнал во флеше W25Q128 (hud.log.0000 … hud.log.9999, по кругу)
  ------------------------------------------------------------------------------
  Всё, что идёт в hud_log_write() / arduino_printf(), дублируется в Serial и во флешь.
  Файлы: до ~500 КБ каждый (512 КБ слот), хранятся последние 4; новый файл начинается, когда
  текущий заполнен (самый старый при этом исчезает). Имена hud.log.NNNN, после 9999 снова 0000.
  Перед каждой строкой: [секунды с включения.мс], а если по CAN (кадр 0x6B2) получено время машины —
  ещё и дата-время: "2026-11-11 10:15:03 [123.456] текст".
  Скачивание — .tar со всеми файлами, страница /logs.tar портала (hud_portal.cpp).
  Формат и ядро: hud_flog_core.[ch]; драйвер флеши — hud_cams.cpp.
*/
#ifndef HUD_FLOG_H
#define HUD_FLOG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void   hud_flog_start(void);                    /* запустить фоновую запись (после hud_cams_start) */
void   hud_flog_push(const char *s);            /* добавить текст в очередь (из hud_log_write) */
bool   hud_flog_ready(void);                    /* журнал смонтирован */

/* Выгрузка .tar: open возвращает false, если файлов нет; total — длина потока. Одновременно — один поток. */
bool   hud_flog_tar_open(uint32_t *total);
size_t hud_flog_tar_read(uint8_t *dst, size_t n);   /* 0 — конец */
#ifdef __cplusplus
}
#endif
#endif
