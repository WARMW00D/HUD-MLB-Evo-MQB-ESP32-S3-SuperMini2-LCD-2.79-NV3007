#ifndef HUD_BLE_CORE_H
#define HUD_BLE_CORE_H
/* Инициализация NimBLE только как клиента CAN-сниффера (настроек по BLE в hud2 нет).
   Вызывать один раз до запуска hud_source_start(). */
void hud_ble_core_begin(void);
/* Пауза BLE-клиента: пока на Wi-Fi портал подключён клиент, BLE отключается от сниффера и не сканирует
   (Wi-Fi и BLE делят одно радио). Снятие паузы — клиент подключится заново. */
void hud_ble_pause(bool on);
bool hud_ble_paused(void);
#endif
