#ifndef CE_LVGL_SDL3_H
#define CE_LVGL_SDL3_H

#include <lvgl/lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ce_lvgl_sdl3 ce_lvgl_sdl3;

typedef void (*ce_lvgl_sdl3_key_callback)(void *context,
                                          unsigned int row,
                                          unsigned int column,
                                          int pressed);

ce_lvgl_sdl3 *ce_lvgl_sdl3_create(int width, int height, unsigned int scale,
                                  const char *title,
                                  ce_lvgl_sdl3_key_callback key_callback,
                                  void *key_context);
void ce_lvgl_sdl3_destroy(ce_lvgl_sdl3 *platform);

/* Returns zero after the user requests that the window close. */
int ce_lvgl_sdl3_process_events(ce_lvgl_sdl3 *platform);

lv_display_t *ce_lvgl_sdl3_display(ce_lvgl_sdl3 *platform);

/* Writes LVGL's 320x240 composited draw buffer, not CEmu's source buffer. */
int ce_lvgl_sdl3_write_ppm(const ce_lvgl_sdl3 *platform, const char *path);

#ifdef __cplusplus
}
#endif

#endif
