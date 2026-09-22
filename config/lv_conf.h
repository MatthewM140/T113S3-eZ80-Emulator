#ifndef LV_CONF_H
#define LV_CONF_H

/* Project configuration for the pinned LVGL desktop development build. */
#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_XRGB8888
#define LV_DEF_REFR_PERIOD 16

/* The project owns the SDL3 adapter; LVGL's bundled desktop driver is SDL2. */
#define LV_USE_SDL 0

/* This milestone needs only the software renderer and a canvas widget. */
#define LV_USE_DRAW_SW 1
#define LV_USE_CANVAS 1
#define LV_USE_IMAGE 1

#define LV_USE_LOG 0
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0
#define LV_USE_THORVG 0

#endif
