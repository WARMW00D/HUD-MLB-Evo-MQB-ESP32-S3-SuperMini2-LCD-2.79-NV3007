#include <Arduino.h>
#include <Preferences.h>
#include "hud_config.h"
#include "user_config.h"
#include "hud_mockup.h"
#include "hud_light.h"
#include "hud_sound.h"
#include "hud_settings.h"

#ifndef HUD_STREET_ON
#define HUD_STREET_ON 1      /* название улицы над стрелкой показывать по умолчанию */
#endif

static Preferences prefs;
static HudSettings cur;
static bool cal_dark_ok, cal_bright_ok;

static bool valid(const HudSettings &s)
{
    return s.psd <= 1 && s.vze <= 1 && s.lang <= 1 && s.units <= 1 && s.tol <= 100 &&
           s.cams <= 1 && s.cam_beep <= 1 && s.volume <= 100 && s.ldr <= 1 && s.street <= 1;
}
static void apply(const HudSettings &s)
{
    hud_set_psd(s.psd); hud_set_vze(s.vze); hud_set_lang(s.lang); hud_set_units(s.units);
    hud_set_overspeed_tol(s.tol); hud_set_cams(s.cams); hud_set_cam_beep(s.cam_beep);
    hud_sound_set_volume(s.volume); hud_light_set_enabled(s.ldr != 0); hud_set_street(s.street);
}
void hud_settings_begin(void)
{
    cur = { HUD_PSD_LIMITS, HUD_VZE_SIGNS, HUD_LANG, HUD_UNITS, HUD_OVERSPEED_TOL_KMH, HUD_CAMS_ON, HUD_CAM_BEEP,
            (uint8_t)((HUD_SOUND_DUTY_PCT > 50 ? 50 : HUD_SOUND_DUTY_PCT) * 2), HUD_LDR_ON, HUD_STREET_ON };
    if (!prefs.begin("hud2", false)) { Serial.println("[settings] NVS недоступен, значения по умолчанию"); apply(cur); return; }
    HudSettings s;
    s.psd = prefs.getUChar("psd", cur.psd);        s.vze = prefs.getUChar("vze", cur.vze);
    s.lang = prefs.getUChar("lang", cur.lang);     s.units = prefs.getUChar("units", cur.units);
    s.tol = prefs.getUChar("tol", cur.tol);        s.cams = prefs.getUChar("cams", cur.cams);
    s.cam_beep = prefs.getUChar("beep", cur.cam_beep); s.volume = prefs.getUChar("vol", cur.volume);
    s.ldr = prefs.getUChar("ldr", cur.ldr);        s.street = prefs.getUChar("street", cur.street);
    if (valid(s)) cur = s;
    cal_dark_ok = prefs.getBool("ldk-ok", false);
    cal_bright_ok = prefs.getBool("ldb-ok", false);
    hud_light_set_cal(prefs.getUShort("ldk", HUD_LDR_DARK_MV), prefs.getUShort("ldb", HUD_LDR_BRIGHT_MV));
    apply(cur);
}
HudSettings hud_settings_get(void) { return cur; }
bool hud_settings_set(const HudSettings &n)
{
    if (!valid(n)) return false;
    bool ok = prefs.putUChar("psd", n.psd) == 1 && prefs.putUChar("vze", n.vze) == 1 &&
              prefs.putUChar("lang", n.lang) == 1 && prefs.putUChar("units", n.units) == 1 &&
              prefs.putUChar("tol", n.tol) == 1 && prefs.putUChar("cams", n.cams) == 1 &&
              prefs.putUChar("beep", n.cam_beep) == 1 && prefs.putUChar("vol", n.volume) == 1 &&
              prefs.putUChar("ldr", n.ldr) == 1 && prefs.putUChar("street", n.street) == 1;
    if (!ok) { Serial.println("[settings] ошибка записи в NVS"); return false; }
    if (n.volume != cur.volume) hud_sound_set_volume(n.volume);
    cur = n; apply(cur);
    if (n.volume != 0) { /* дать услышать новую громкость */ }
    return true;
}
bool hud_settings_calibrate(bool bright)
{
    int dark, br; hud_light_get_cal(&dark, &br);
    hud_light_sample_now();
    const int mv = hud_light_mv();
    if (mv < 0) return false;
    bool ok;
    if (bright) { br = mv;   ok = prefs.putUShort("ldb", (uint16_t)br) == 2 && prefs.putBool("ldb-ok", true) == 1; }
    else        { dark = mv; ok = prefs.putUShort("ldk", (uint16_t)dark) == 2 && prefs.putBool("ldk-ok", true) == 1; }
    if (!ok) return false;
    hud_light_set_cal(dark, br);
    (bright ? cal_bright_ok : cal_dark_ok) = true;
    Serial.printf("[light] calibration %s = %d mV\n", bright ? "bright" : "dark", mv);
    return true;
}
bool hud_settings_cal_dark_set(void)   { return cal_dark_ok; }
bool hud_settings_cal_bright_set(void) { return cal_bright_ok; }
