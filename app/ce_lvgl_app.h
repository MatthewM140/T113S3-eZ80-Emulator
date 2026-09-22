#ifndef CE_LVGL_APP_H
#define CE_LVGL_APP_H

#include "ce/emulator.h"

#include <lvgl/lvgl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CE_LVGL_LCD_WIDTH 320
#define CE_LVGL_LCD_HEIGHT 240

typedef struct ce_lvgl_app {
    ce_emulator *emulator;
    lv_obj_t *canvas;
    const uint32_t *framebuffer;
} ce_lvgl_app;

int ce_lvgl_app_init(ce_lvgl_app *app, ce_emulator *emulator);

/* Advance exactly one backend scheduler tick and invalidate the LCD canvas. */
int ce_lvgl_app_tick(ce_lvgl_app *app);

#ifdef __cplusplus
}
#endif

#endif
