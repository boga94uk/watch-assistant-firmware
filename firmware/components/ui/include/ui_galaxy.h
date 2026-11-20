#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_galaxy_create(lv_obj_t *parent, int w, int h);
void ui_galaxy_set_visible(bool on);
void ui_galaxy_pause(bool pause);

#ifdef __cplusplus
}
#endif
