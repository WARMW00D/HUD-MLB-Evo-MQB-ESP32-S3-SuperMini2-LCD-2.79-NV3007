/*
  hud_portal.cpp — Wi-Fi портал настроек supermini_hud2
  ------------------------------------------------------------------------------
  * Точка доступа HUD_PORTAL_SSID (WPA2, пароль HUD_PORTAL_PASS), адрес http://192.168.4.1/ (DNS отвечает всем именам).
  * Работает HUD_PORTAL_MS после включения, затем Wi-Fi выключается совсем (BLE-клиент сниффера продолжает работать).
    Пока идёт загрузка файла, портал не закрывается.
  * Мощность передатчика ограничена HUD_PORTAL_TX_QDBM (единицы 0.25 дБм; 40 = 10 дБм).
  * Страницы: / (настройки), /api/state, /api/set, /api/cal, /update (OTA прошивки), /cams (cams.bin).
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include "hud_config.h"
#include "user_config.h"
#include "hud_mockup.h"
#include "hud_cams.h"
#include "hud_light.h"
#include "hud_sound.h"
#include "hud_settings.h"
#include "hud_portal.h"
#include "hud_portal_page.h"

static_assert(HUD_PORTAL_TX_QDBM > 0 && HUD_PORTAL_TX_QDBM <= 40, "Мощность Wi-Fi: не выше 10 дБм (40 единиц по 0.25 дБм).");
static_assert(sizeof(HUD_PORTAL_PASS) > 8, "Пароль точки доступа: минимум 8 символов.");

static WebServer server(80);
static DNSServer dns;
static bool active = false;
static uint32_t deadline = 0, restart_at = 0;
static bool upload_busy = false;

/* ----- OTA прошивки (Update.h: пишет в соседний слот, выбор слота — только после успешной проверки) ----- */
static bool    ota_err = false;
static String  ota_msg;
static uint32_t ota_total = 0, ota_got = 0;

static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void ota_upload()
{
    HTTPUpload &u = server.upload();
    if (u.status == UPLOAD_FILE_START) {
        upload_busy = true; ota_err = false; ota_msg = ""; ota_got = 0;
        ota_total = (uint32_t)server.arg("size").toInt();
        if (!Update.begin(ota_total ? ota_total : UPDATE_SIZE_UNKNOWN, U_FLASH)) { ota_err = true; ota_msg = "no space"; }
    } else if (u.status == UPLOAD_FILE_WRITE && !ota_err) {
        if (ota_got == 0) {
            /* сырой образ приложения ESP32-S3: magic 0xE9, chip id 9, app descriptor — иначе не пишем вообще */
            const uint8_t *p = u.buf;
            if (u.currentSize < 36 || p[0] != 0xE9 || p[12] != 9 || p[13] != 0 || le32(p + 32) != 0xABCD5432) {
                ota_err = true; ota_msg = "not an ESP32-S3 app image"; Update.abort(); return;
            }
        }
        if (Update.write(u.buf, u.currentSize) != u.currentSize) { ota_err = true; ota_msg = "write failed"; Update.abort(); return; }
        ota_got += u.currentSize;
        hud_ota_ui_status(1, ota_got, ota_total ? ota_total : ota_got);
    } else if (u.status == UPLOAD_FILE_END) {
        if (!ota_err && !Update.end(true)) { ota_err = true; ota_msg = Update.errorString(); }
        hud_ota_ui_status(ota_err ? 4 : 3, ota_got, ota_total ? ota_total : ota_got);
    } else if (u.status == UPLOAD_FILE_ABORTED) {
        Update.abort(); ota_err = true; ota_msg = "aborted";
        hud_ota_ui_status(0, 0, 0);
    }
}
static void ota_done()
{
    const bool ok = !ota_err && ota_got > 0;
    server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : String("{\"ok\":false,\"err\":\"") + ota_msg + "\"}");
    upload_busy = false;
    if (ok) restart_at = millis() + 1500;
}

/* ----- cams.bin во внешнюю флешь W25Q128 (адреса с 0; первая страница с сигнатурой пишется последней) ----- */
class CamWriter {
    mbedtls_sha256_context sha = {};
    uint8_t page[256], hold0[256], digest[32];
    uint32_t pos = 0, total = 0;
    bool hashing = false, active = false;
    bool upd(const uint8_t *d, size_t n) {
#if MBEDTLS_VERSION_MAJOR >= 3
        return mbedtls_sha256_update(&sha, d, n) == 0;
#else
        return mbedtls_sha256_update_ret(&sha, d, n) == 0;
#endif
    }
    bool flush(uint32_t rel) {
        if (rel == 0) memcpy(hold0, page, 256);
        else {
            if (!(rel & 4095) && !hud_cams_flash_erase(rel)) return false;
            if (!hud_cams_flash_write(rel, page, 256)) return false;
        }
        memset(page, 0xFF, sizeof page);
        return true;
    }
    /* первая страница: "CAM1", count, размер файла = 16 + 12*count; стираем первый сектор — старая сигнатура пропадает сразу */
    bool detect() {
        if (memcmp(page, "CAM1", 4) != 0 || total != 16u + 12u * le32(page + 4) || total > hud_cams_max_db_bytes()) return false;
        return hud_cams_flash_erase(0);
    }
public:
    bool begin(uint32_t size) {
        abort();
        if (!size || size < 16 || size > hud_cams_max_db_bytes()) return false;
        memset(page, 0xFF, sizeof page); memset(hold0, 0xFF, sizeof hold0);
        mbedtls_sha256_init(&sha); hashing = true;
#if MBEDTLS_VERSION_MAJOR >= 3
        const int r = mbedtls_sha256_starts(&sha, 0);
#else
        const int r = mbedtls_sha256_starts_ret(&sha, 0);
#endif
        if (r) { abort(); return false; }
        pos = 0; total = size; active = true;
        return true;
    }
    bool write(const uint8_t *d, size_t n) {
        if (!active || !hashing || !upd(d, n)) return false;
        while (n) {
            const uint32_t in_page = pos & 255;
            size_t k = 256 - in_page; if (k > n) k = n;
            memcpy(page + in_page, d, k);
            pos += k; d += k; n -= k;
            if (!(pos & 255)) {
                if (pos == 256 && !detect()) return false;
                if (!flush(pos - 256)) return false;
            }
        }
        return true;
    }
    bool finish_and_commit() {
        if (!active || !hashing || pos != total) return false;
        if (pos < 256 && !detect()) return false;
        if ((pos & 255) && !flush(pos & ~255u)) return false;
#if MBEDTLS_VERSION_MAJOR >= 3
        const int r = mbedtls_sha256_finish(&sha, digest);
#else
        const int r = mbedtls_sha256_finish_ret(&sha, digest);
#endif
        mbedtls_sha256_free(&sha); hashing = false;
        if (r) return false;
        if (!hud_cams_flash_write(0, hold0, 256)) return false;      /* сигнатура и заголовок — последними */
        return hud_cams_reload(digest, total);                        /* перечитать из флеши, сверить SHA-256, включить */
    }
    void abort() {
        if (hashing) mbedtls_sha256_free(&sha);
        hashing = active = false; pos = total = 0;
    }
};
static CamWriter cam;
static bool cam_err = false;
static String cam_msg;
static uint32_t cam_total = 0, cam_got = 0;

static void cams_upload()
{
    HTTPUpload &u = server.upload();
    if (u.status == UPLOAD_FILE_START) {
        upload_busy = true; cam_err = false; cam_msg = ""; cam_got = 0;
        cam_total = (uint32_t)server.arg("size").toInt();
        if (!cam.begin(cam_total)) { cam_err = true; cam_msg = "bad size or no flash"; }
    } else if (u.status == UPLOAD_FILE_WRITE && !cam_err) {
        if (!cam.write(u.buf, u.currentSize)) { cam_err = true; cam_msg = "not a CAM1 file or flash error"; cam.abort(); return; }
        cam_got += u.currentSize;
    } else if (u.status == UPLOAD_FILE_END) {
        if (!cam_err && (cam_got != cam_total || !cam.finish_and_commit())) { cam_err = true; cam_msg = "verification failed"; cam.abort(); }
        else if (!cam_err) Serial.println("[portal] cams.bin записан, база активна");
    } else if (u.status == UPLOAD_FILE_ABORTED) {
        cam.abort(); cam_err = true; cam_msg = "aborted";
    }
}
static void cams_done()
{
    const bool ok = !cam_err && cam_got > 0;
    server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : String("{\"ok\":false,\"err\":\"") + cam_msg + "\"}");
    upload_busy = false;
}

/* ----- API ----- */
static uint32_t seconds_left()
{
    const uint32_t now = millis();
    return (int32_t)(deadline - now) > 0 ? (deadline - now) / 1000 : 0;
}
/* Состояние читаем у самого HUD (hud_get_*): то, что реально использует экран. */
static String state_json(bool ok)
{
    String j; j.reserve(300);
    j += "{\"ok\":"; j += ok ? "true" : "false";
    j += ",\"ver\":\"" HUD_FW_VERSION_STRING "\",\"left\":"; j += seconds_left();
    j += ",\"psd\":"; j += hud_get_psd() ? 1 : 0; j += ",\"vze\":"; j += hud_get_vze() ? 1 : 0;
    j += ",\"lang\":"; j += hud_get_lang(); j += ",\"units\":"; j += hud_get_units();
    j += ",\"tol\":"; j += hud_get_overspeed_tol(); j += ",\"cams\":"; j += hud_get_cams() ? 1 : 0;
    j += ",\"cam_beep\":"; j += hud_get_cam_beep() ? 1 : 0; j += ",\"volume\":"; j += hud_sound_volume();
    j += ",\"ldr\":"; j += hud_light_enabled() ? 1 : 0; j += ",\"mv\":"; j += hud_light_mv(); j += "}";
    return j;
}
static void send_json(int code, const String &j)
{
    server.sendHeader("Cache-Control", "no-store");
    server.send(code, "application/json", j);
}
static void api_state() { send_json(200, state_json(true)); }
static bool arg_u8(const char *k, uint8_t &out, uint8_t max)
{
    if (!server.hasArg(k)) return true;                 /* поле не передано — оставить как есть */
    const String v = server.arg(k);
    if (!v.length() || v.length() > 3) return false;
    for (unsigned i = 0; i < v.length(); ++i) if (!isdigit((unsigned char)v[i])) return false;
    const long n = v.toInt();
    if (n < 0 || n > max) return false;
    out = (uint8_t)n; return true;
}
static void api_set()
{
    HudSettings s = hud_settings_get();
    Serial.printf("[portal] POST /api/set, аргументов: %d\n", server.args());
    for (int i = 0; i < server.args(); ++i) Serial.printf("[portal]   %s=%s\n", server.argName(i).c_str(), server.arg(i).c_str());
    const bool ok = arg_u8("psd", s.psd, 1) && arg_u8("vze", s.vze, 1) && arg_u8("lang", s.lang, 1) &&
                    arg_u8("units", s.units, 1) && arg_u8("tol", s.tol, 100) && arg_u8("cams", s.cams, 1) &&
                    arg_u8("cam_beep", s.cam_beep, 1) && arg_u8("volume", s.volume, 100) && arg_u8("ldr", s.ldr, 1) &&
                    hud_settings_set(s);
    Serial.printf("[portal] set -> %s; HUD сейчас: lang=%u units=%u psd=%d vze=%d tol=%u cams=%d beep=%d vol=%u ldr=%d\n", ok ? "ok" : "ОШИБКА",
                  hud_get_lang(), hud_get_units(), hud_get_psd(), hud_get_vze(), hud_get_overspeed_tol(), hud_get_cams(), hud_get_cam_beep(),
                  hud_sound_volume(), hud_light_enabled());
    send_json(ok ? 200 : 400, state_json(ok));
}
static void api_cal()
{
    const String p = server.arg("p");
    const bool ok = (p == "dark" || p == "bright") && hud_settings_calibrate(p == "bright");
    send_json(ok ? 200 : 400, state_json(ok));
}

/* ----- жизненный цикл ----- */
static void stop_portal()
{
    server.stop(); dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    active = false;
    Serial.println("[portal] Wi-Fi выключен");
}
void hud_portal_begin(void)
{
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(HUD_PORTAL_SSID, HUD_PORTAL_PASS, HUD_PORTAL_CHANNEL, 0, HUD_PORTAL_MAX_CLIENTS)) {
        Serial.println("[portal] не удалось поднять точку доступа"); WiFi.mode(WIFI_OFF); return;
    }
    /* потолок мощности: 40 единиц по 0.25 дБм = 10 дБм. Ставим после старта AP и проверяем. */
    esp_wifi_set_max_tx_power(HUD_PORTAL_TX_QDBM);
    int8_t p = 0;
    if (esp_wifi_get_max_tx_power(&p) != ESP_OK || p > HUD_PORTAL_TX_QDBM) {
        Serial.printf("[portal] мощность %d (x0.25 дБм) выше лимита — Wi-Fi выключен\n", (int)p);
        WiFi.softAPdisconnect(true); WiFi.mode(WIFI_OFF); return;
    }
    dns.start(53, "*", WiFi.softAPIP());
    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", HUD_PORTAL_PAGE); });
    server.on("/api/state", HTTP_GET, api_state);
    server.on("/api/set", HTTP_POST, api_set);
    server.on("/api/cal", HTTP_POST, api_cal);
    server.on("/update", HTTP_POST, ota_done, ota_upload);
    server.on("/cams", HTTP_POST, cams_done, cams_upload);
    server.onNotFound([] { server.sendHeader("Location", "http://192.168.4.1/"); server.send(302, "text/plain", ""); });
    server.begin();
    deadline = millis() + HUD_PORTAL_MS; active = true;
    Serial.printf("[portal] SSID %s, http://%s/, TX %.2f дБм, закроется через %u с\n", HUD_PORTAL_SSID,
                  WiFi.softAPIP().toString().c_str(), p * 0.25f, (unsigned)(HUD_PORTAL_MS / 1000));
}
void hud_portal_poll(void)
{
    if (restart_at && (int32_t)(millis() - restart_at) >= 0) { Serial.println("[portal] перезагрузка после OTA"); ESP.restart(); }
    if (!active) return;
    dns.processNextRequest();
    server.handleClient();
    /* пока идёт загрузка файла, срок не истекает; после неё портал закроется через HUD_PORTAL_GRACE_MS */
    if (upload_busy) { const uint32_t d = millis() + HUD_PORTAL_GRACE_MS; if ((int32_t)(d - deadline) > 0) deadline = d; return; }
    if ((int32_t)(millis() - deadline) >= 0) stop_portal();
}
bool hud_portal_active(void) { return active; }
