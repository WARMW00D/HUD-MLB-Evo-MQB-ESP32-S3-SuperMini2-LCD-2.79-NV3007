#ifndef HUD_PORTAL_H
#define HUD_PORTAL_H
/* Wi-Fi портал настроек: точка доступа поднимается при включении и закрывается через HUD_PORTAL_MS
   (если идёт загрузка файла — после её завершения). Мощность передатчика не выше HUD_PORTAL_TX_QDBM. */
void hud_portal_begin(void);
void hud_portal_poll(void);      /* из loop(), каждые 2..20 мс */
bool hud_portal_active(void);
#endif
