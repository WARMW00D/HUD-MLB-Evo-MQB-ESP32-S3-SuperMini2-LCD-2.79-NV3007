/* Сгенерировано tools/gen_min_native.py — не править вручную */
#ifndef HUD_NAV_IMAGES_H
#define HUD_NAV_IMAGES_H
#include "lvgl.h"

#define NAV_AREA_W 142
#define NAV_AREA_H 96

typedef struct { const lv_img_dsc_t *img; int16_t x, y; } NavImg;   /* x,y — смещение картинки в области стрелки */
typedef struct { const NavImg *turn, *round, *exit, *fork, *uturn, *arrived; } NavSet;

extern const NavSet nav_big;   /* turn[16] — сектор 22.5 град против часовой от «прямо»; round[16]; exit/fork/uturn: [0] налево, [1] направо */

#endif
