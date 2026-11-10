/* HUD Super Mini 2: ESP32-S3 Super Mini, NV3007 428x142, минимальный режим, портал настроек по Wi-Fi (5 минут после включения).
   Версия: HUD_FW_VERSION_STRING (hud_config.h). CAN-данные: свой TWAI-трансивер и/или BLE-сниффер (HUD_DATA_SOURCE). */
#include "user_config.h"
#include "lvgl_port.h"
#include "lvgl.h"
#include "hud_config.h"
#include "hud_source.h"
#include "hud_settings.h"
#include "hud_ble_core.h"
#include "hud_portal.h"
#include "hud_log.h"
#include "hud_sound.h"
#include "src/lcd_bl_bsp/lcd_bl_pwm_bsp.h"

void lvgl_log_cb(const char *buf)
{
    if (HUD_LOG_LVGL) { hud_log_write("[LVGL] "); hud_log_write(buf); }
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("[fw] HUD Super Mini 2 v" HUD_FW_VERSION_STRING " / Wi-Fi portal");
    /* Legacy duty convention: 255 = dark, 0 = full brightness. */
    lcd_bl_pwm_bsp_init(255);
    hud_log_write("=== Super Mini 2 / NV3007 HUD ===\n");
#if LV_USE_LOG
    lv_log_register_print_cb(lvgl_log_cb);
#endif
    hud_sound_begin();
    hud_settings_begin();       /* NVS -> применить настройки */
    hud_ble_core_begin();       /* NimBLE как клиент сниффера */
    lvgl_port_init();
    setUpduty(255 - HUD_BRIGHT_NO_DATA);
    hud_source_start();
    hud_portal_begin();         /* точка доступа на HUD_PORTAL_MS, мощность <= 10 дБм */
#if HUD_SOUND_STARTUP
    hud_sound_play(HUD_SND_RISING);
#endif
}

void loop() { hud_portal_poll(); vTaskDelay(pdMS_TO_TICKS(20)); }
