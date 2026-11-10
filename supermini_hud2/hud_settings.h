#ifndef HUD_SETTINGS_H
#define HUD_SETTINGS_H
#include <stdbool.h>
#include <stdint.h>

/* Настройки, которые меняются с портала. Хранятся в NVS (пространство "hud2"), применяются сразу. */
struct HudSettings {
    uint8_t psd;        /* знаки PSD                       0/1   */
    uint8_t vze;        /* знаки VZE_01                    0/1   */
    uint8_t lang;       /* 0 RU, 1 EN                            */
    uint8_t units;      /* 0 км/ч, 1 mph                         */
    uint8_t tol;        /* допуск превышения, 0..100 км/ч        */
    uint8_t cams;       /* камеры показывать               0/1   */
    uint8_t cam_beep;   /* сигнал камеры                   0/1   */
    uint8_t volume;     /* громкость, 0..100 %                   */
    uint8_t ldr;        /* фоторезистор                    0/1   */
};

void        hud_settings_begin(void);                    /* загрузить из NVS и применить */
HudSettings hud_settings_get(void);
/* Проверить, сохранить в NVS, применить. false — значение вне диапазона или ошибка записи (ничего не изменено). */
bool        hud_settings_set(const HudSettings &next);
/* Запомнить текущее напряжение фоторезистора как точку «темно» (bright=false) или «ярко» (true). */
bool        hud_settings_calibrate(bool bright);
bool        hud_settings_cal_dark_set(void);
bool        hud_settings_cal_bright_set(void);
#endif
