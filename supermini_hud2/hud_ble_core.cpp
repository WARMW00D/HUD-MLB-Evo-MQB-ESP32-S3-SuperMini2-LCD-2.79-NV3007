/* hud_ble_core.cpp — NimBLE в роли central для CAN-сниффера. Никаких GATT-сервисов, рекламы и привязки телефона. */
#include <Arduino.h>
#include "hud_config.h"
#include "user_config.h"
#include "hud_ble_core.h"

#if HUD_DATA_SOURCE != HUD_SRC_TWAI && HUD_DATA_SOURCE != HUD_SRC_FAKE
#include <NimBLEDevice.h>
void hud_ble_core_begin(void)
{
    NimBLEDevice::init("");                                   /* только клиент, без имени и рекламы */
    NimBLEDevice::setPower(HUD_BLE_TX_DBM);                   /* потолок мощности BLE, дБм (не выше 10) */
    NimBLEDevice::setSecurityAuth(true, true, true);          /* bonding + MITM + secure connections */
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_ONLY);  /* код сниффера вводит HUD (BLE_PASSKEY из secrets.h) */
    NimBLEDevice::setMTU(247);
    Serial.println("[ble] client ready");
}
#else
void hud_ble_core_begin(void) {}
#endif
