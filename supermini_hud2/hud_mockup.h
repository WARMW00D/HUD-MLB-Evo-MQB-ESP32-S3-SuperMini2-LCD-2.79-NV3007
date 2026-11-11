#ifndef HUD_MOCKUP_H
#define HUD_MOCKUP_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Строит экран и запускает lv_timer обновления (50 мс).
   Вызывается из lvgl_port_init() под мьютексом LVGL. */
void build_hud_mockup(void);

/* Статус обновления ПО по Wi-Fi (портал): 0 нет, 1 загрузка, 2 проверка, 3 готово, 4 ошибка. Можно из любой задачи. */
void hud_ota_ui_status(uint8_t state, uint32_t received, uint32_t total);

/* Язык и единицы во время работы (HUD_LANG_* / HUD_UNITS_* из hud_config.h).
   Можно вызывать из любой задачи: применяются на следующем тике экрана. */
void    hud_set_lang(uint8_t lang);
void    hud_set_units(uint8_t units);
uint8_t hud_get_lang(void);
uint8_t hud_get_units(void);
void    hud_set_psd(bool on);          /* знаки из PSD (прогноз маршрута MIB) вкл/выкл  */
bool    hud_get_psd(void);
void    hud_set_vze(bool on);          /* знаки из VZE_01 (камера / карта приборки)     */
bool    hud_get_vze(void);
void    hud_set_overspeed_tol(uint8_t kmh);
uint8_t hud_get_overspeed_tol(void);
void    hud_set_cams(bool on);         /* камеры: показ (HUD_CAMS) */
bool    hud_get_cams(void);
void    hud_set_cam_beep(bool on);     /* камеры: сигнал */
bool    hud_get_cam_beep(void);
void    hud_set_street(bool on);       /* название улицы над стрелкой */
bool    hud_get_street(void);


#ifdef __cplusplus
}
#endif
#endif
