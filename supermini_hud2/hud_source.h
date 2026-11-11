#ifndef HUD_SOURCE_H
#define HUD_SOURCE_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Запускает источник CAN-кадров по HUD_DATA_SOURCE из hud_config.h:
   свой трансивер (TWAI, по умолчанию) или встроенный генератор (FAKE). */
void hud_source_start(void);

/* ---- для модулей-источников ---- */
enum { HUD_SRC_ID_TWAI = 1, HUD_SRC_ID_FAKE = 2 };
void hud_src_set_up(int src, bool up);   /* связь источника есть / нет */

void twai_source_start(void);
void fake_source_start(void);
#ifdef __cplusplus
}
#endif
#endif
