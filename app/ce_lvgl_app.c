#include "ce_lvgl_app.h"

#include <stddef.h>
#include <stdint.h>

static void configure_screen(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(screen, false);
}

int ce_lvgl_app_init(ce_lvgl_app *app, ce_emulator *emulator)
{
    const uint32_t *framebuffer;

    if ((app == NULL) || (emulator == NULL)) {
        return -1;
    }

    framebuffer = ce_emulator_framebuffer(emulator);
    if (framebuffer == NULL) {
        return -1;
    }

    configure_screen();
    app->emulator = emulator;
    app->framebuffer = framebuffer;
    app->canvas = lv_canvas_create(lv_screen_active());
    if (app->canvas == NULL) {
        return -1;
    }

    /*
     * CEmu exposes numeric pixels as 0xAARRGGBB. On the little-endian x86
     * development host that is byte-for-byte LVGL ARGB8888 (B, G, R, A).
     * The canvas only reads this emulator-owned buffer.
     */
    lv_canvas_set_buffer(app->canvas, (void *)(uintptr_t)framebuffer,
                         CE_LVGL_LCD_WIDTH, CE_LVGL_LCD_HEIGHT,
                         LV_COLOR_FORMAT_ARGB8888);
    lv_obj_center(app->canvas);
    lv_obj_set_scrollable(app->canvas, false);
    lv_obj_invalidate(app->canvas);
    return 0;
}

int ce_lvgl_app_tick(ce_lvgl_app *app)
{
    const uint32_t *framebuffer;

    if ((app == NULL) || (app->emulator == NULL) || (app->canvas == NULL)) {
        return -1;
    }

    ce_emulator_run(app->emulator, 1U);
    framebuffer = ce_emulator_framebuffer(app->emulator);
    if (framebuffer == NULL) {
        return -1;
    }

    if (framebuffer != app->framebuffer) {
        app->framebuffer = framebuffer;
        lv_canvas_set_buffer(app->canvas, (void *)(uintptr_t)framebuffer,
                             CE_LVGL_LCD_WIDTH, CE_LVGL_LCD_HEIGHT,
                             LV_COLOR_FORMAT_ARGB8888);
    }
    lv_obj_invalidate(app->canvas);
    return 0;
}
