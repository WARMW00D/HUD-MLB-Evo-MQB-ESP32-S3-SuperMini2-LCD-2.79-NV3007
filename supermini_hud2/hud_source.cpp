/*
  hud_source.cpp — запуск источника CAN-кадров
  ------------------------------------------------------------------------------
  HUD_SRC_TWAI — свой трансивер на шине I-CAN (единственный «боевой» источник);
  HUD_SRC_FAKE — генератор кадров для проверки без машины.
*/
#include <Arduino.h>
#include "hud_config.h"
#include "hud_data.h"
#include "hud_log.h"
#include "hud_source.h"

static volatile bool s_up[3] = { false, false, false };

void hud_src_set_up(int src, bool up)
{
    if (src < 0 || src > 2) return;
    s_up[src] = up;
    hud_data_set_link(s_up[0] || s_up[1] || s_up[2]);
}

void hud_source_start(void)
{
    hud_data_set_link(false);
#if   HUD_DATA_SOURCE == HUD_SRC_TWAI
    twai_source_start();
#elif HUD_DATA_SOURCE == HUD_SRC_FAKE
    fake_source_start();
#else
#error "HUD_DATA_SOURCE: HUD_SRC_TWAI или HUD_SRC_FAKE"
#endif
}
