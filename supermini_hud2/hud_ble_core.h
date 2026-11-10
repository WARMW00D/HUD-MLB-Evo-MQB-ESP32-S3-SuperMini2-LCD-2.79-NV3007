#ifndef HUD_BLE_CORE_H
#define HUD_BLE_CORE_H
/* Инициализация NimBLE только как клиента CAN-сниффера (настроек по BLE в hud2 нет).
   Вызывать один раз до запуска hud_source_start(). */
void hud_ble_core_begin(void);
#endif
