#include "ce_lvgl_sdl3.h"

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct ce_lvgl_sdl3 {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    lv_display_t *display;
    uint32_t *draw_buffer;
    int width;
    int height;
    ce_lvgl_sdl3_key_callback key_callback;
    void *key_context;
};

typedef struct ce_key_binding {
    SDL_Scancode scancode;
    unsigned int row;
    unsigned int column;
} ce_key_binding;

static uint32_t sdl_tick(void)
{
    return (uint32_t)SDL_GetTicks();
}

static void sdl_delay(uint32_t milliseconds)
{
    SDL_Delay(milliseconds);
}

static int key_position(SDL_Scancode scancode, unsigned int *row,
                        unsigned int *column)
{
    static const ce_key_binding map[] = {
        {SDL_SCANCODE_F5, 1U, 0U}, {SDL_SCANCODE_F4, 1U, 1U},
        {SDL_SCANCODE_F3, 1U, 2U}, {SDL_SCANCODE_F2, 1U, 3U},
        {SDL_SCANCODE_F1, 1U, 4U}, {SDL_SCANCODE_TAB, 1U, 5U},
        {SDL_SCANCODE_HOME, 1U, 6U}, {SDL_SCANCODE_DELETE, 1U, 7U},
        {SDL_SCANCODE_F12, 2U, 0U}, {SDL_SCANCODE_BACKSLASH, 2U, 1U},
        {SDL_SCANCODE_S, 2U, 2U}, {SDL_SCANCODE_N, 2U, 3U},
        {SDL_SCANCODE_I, 2U, 4U}, {SDL_SCANCODE_D, 2U, 5U},
        {SDL_SCANCODE_M, 2U, 6U}, {SDL_SCANCODE_APOSTROPHE, 2U, 7U},
        {SDL_SCANCODE_0, 3U, 0U}, {SDL_SCANCODE_1, 3U, 1U},
        {SDL_SCANCODE_4, 3U, 2U}, {SDL_SCANCODE_7, 3U, 3U},
        {SDL_SCANCODE_COMMA, 3U, 4U}, {SDL_SCANCODE_E, 3U, 5U},
        {SDL_SCANCODE_PAGEUP, 3U, 6U}, {SDL_SCANCODE_X, 3U, 7U},
        {SDL_SCANCODE_PERIOD, 4U, 0U}, {SDL_SCANCODE_2, 4U, 1U},
        {SDL_SCANCODE_5, 4U, 2U}, {SDL_SCANCODE_8, 4U, 3U},
        {SDL_SCANCODE_LEFTBRACKET, 4U, 4U}, {SDL_SCANCODE_F, 4U, 5U},
        {SDL_SCANCODE_PAGEDOWN, 4U, 6U}, {SDL_SCANCODE_END, 4U, 7U},
        {SDL_SCANCODE_GRAVE, 5U, 0U}, {SDL_SCANCODE_3, 5U, 1U},
        {SDL_SCANCODE_6, 5U, 2U}, {SDL_SCANCODE_9, 5U, 3U},
        {SDL_SCANCODE_RIGHTBRACKET, 5U, 4U}, {SDL_SCANCODE_G, 5U, 5U},
        {SDL_SCANCODE_INSERT, 5U, 6U},
        {SDL_SCANCODE_RETURN, 6U, 0U}, {SDL_SCANCODE_KP_ENTER, 6U, 0U},
        {SDL_SCANCODE_EQUALS, 6U, 1U}, {SDL_SCANCODE_KP_PLUS, 6U, 1U},
        {SDL_SCANCODE_MINUS, 6U, 2U}, {SDL_SCANCODE_KP_MINUS, 6U, 2U},
        {SDL_SCANCODE_KP_MULTIPLY, 6U, 3U},
        {SDL_SCANCODE_KP_DIVIDE, 6U, 4U}, {SDL_SCANCODE_SLASH, 6U, 4U},
        {SDL_SCANCODE_H, 6U, 5U}, {SDL_SCANCODE_ESCAPE, 6U, 6U},
        {SDL_SCANCODE_DOWN, 7U, 0U}, {SDL_SCANCODE_LEFT, 7U, 1U},
        {SDL_SCANCODE_RIGHT, 7U, 2U}, {SDL_SCANCODE_UP, 7U, 3U}
    };

    for (size_t index = 0U; index < sizeof(map) / sizeof(map[0]); ++index) {
        if (map[index].scancode == scancode) {
            *row = map[index].row;
            *column = map[index].column;
            return 0;
        }
    }
    return -1;
}

static void flush_display(lv_display_t *display, const lv_area_t *area,
                          uint8_t *pixels)
{
    ce_lvgl_sdl3 *platform = lv_display_get_driver_data(display);
    (void)area;

    if ((platform != NULL) && (platform->texture != NULL) &&
        SDL_UpdateTexture(platform->texture, NULL, pixels,
                          platform->width * (int)sizeof(uint32_t)) &&
        SDL_SetRenderDrawColor(platform->renderer, 0U, 0U, 0U, 255U) &&
        SDL_RenderClear(platform->renderer) &&
        SDL_RenderTexture(platform->renderer, platform->texture, NULL, NULL)) {
        SDL_RenderPresent(platform->renderer);
    }
    lv_display_flush_ready(display);
}

static void destroy_partial(ce_lvgl_sdl3 *platform)
{
    if (platform == NULL) {
        return;
    }
    if (platform->display != NULL) {
        lv_display_delete(platform->display);
    }
    SDL_DestroyTexture(platform->texture);
    SDL_DestroyRenderer(platform->renderer);
    SDL_DestroyWindow(platform->window);
    free(platform->draw_buffer);
    SDL_Quit();
    free(platform);
}

ce_lvgl_sdl3 *ce_lvgl_sdl3_create(int width, int height, unsigned int scale,
                                  const char *title,
                                  ce_lvgl_sdl3_key_callback key_callback,
                                  void *key_context)
{
    ce_lvgl_sdl3 *platform;
    size_t pixel_count;

    if ((width <= 0) || (height <= 0) || (scale == 0U) || (title == NULL)) {
        return NULL;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return NULL;
    }

    platform = calloc(1U, sizeof(*platform));
    if (platform == NULL) {
        SDL_Quit();
        return NULL;
    }
    platform->width = width;
    platform->height = height;
    platform->key_callback = key_callback;
    platform->key_context = key_context;

    platform->window = SDL_CreateWindow(title, width * (int)scale,
                                        height * (int)scale, 0);
    platform->renderer = platform->window == NULL ? NULL :
                         SDL_CreateRenderer(platform->window, NULL);
    platform->texture = platform->renderer == NULL ? NULL :
                        SDL_CreateTexture(platform->renderer,
                                          SDL_PIXELFORMAT_XRGB8888,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          width, height);
    if ((platform->window == NULL) || (platform->renderer == NULL) ||
        (platform->texture == NULL)) {
        fprintf(stderr, "SDL display creation failed: %s\n", SDL_GetError());
        destroy_partial(platform);
        return NULL;
    }

    SDL_SetTextureScaleMode(platform->texture, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderLogicalPresentation(platform->renderer, width, height,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    pixel_count = (size_t)width * (size_t)height;
    platform->draw_buffer = calloc(pixel_count, sizeof(*platform->draw_buffer));
    if (platform->draw_buffer == NULL) {
        destroy_partial(platform);
        return NULL;
    }

    platform->display = lv_display_create(width, height);
    if (platform->display == NULL) {
        destroy_partial(platform);
        return NULL;
    }
    lv_display_set_color_format(platform->display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_driver_data(platform->display, platform);
    lv_display_set_flush_cb(platform->display, flush_display);
    lv_display_set_buffers(platform->display, platform->draw_buffer, NULL,
                           (uint32_t)(pixel_count * sizeof(uint32_t)),
                           LV_DISPLAY_RENDER_MODE_FULL);

    lv_tick_set_cb(sdl_tick);
    lv_delay_set_cb(sdl_delay);
    return platform;
}

void ce_lvgl_sdl3_destroy(ce_lvgl_sdl3 *platform)
{
    destroy_partial(platform);
}

int ce_lvgl_sdl3_process_events(ce_lvgl_sdl3 *platform)
{
    SDL_Event event;

    if (platform == NULL) {
        return 0;
    }

    while (SDL_PollEvent(&event)) {
        if ((event.type == SDL_EVENT_QUIT) ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)) {
            return 0;
        }
        if ((event.type == SDL_EVENT_KEY_DOWN) ||
            (event.type == SDL_EVENT_KEY_UP)) {
            unsigned int row;
            unsigned int column;
            if ((event.type == SDL_EVENT_KEY_DOWN) && event.key.repeat) {
                continue;
            }
            if ((platform->key_callback != NULL) &&
                (key_position(event.key.scancode, &row, &column) == 0)) {
                platform->key_callback(platform->key_context, row, column,
                                       event.type == SDL_EVENT_KEY_DOWN);
            }
        }
    }
    return 1;
}

lv_display_t *ce_lvgl_sdl3_display(ce_lvgl_sdl3 *platform)
{
    return platform == NULL ? NULL : platform->display;
}

int ce_lvgl_sdl3_write_ppm(const ce_lvgl_sdl3 *platform, const char *path)
{
    FILE *file;

    if ((platform == NULL) || (path == NULL) ||
        (platform->draw_buffer == NULL)) {
        return -1;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return -1;
    }
    if (fprintf(file, "P6\n%d %d\n255\n", platform->width,
                platform->height) < 0) {
        fclose(file);
        return -1;
    }
    for (int y = 0; y < platform->height; ++y) {
        for (int x = 0; x < platform->width; ++x) {
            uint32_t pixel = platform->draw_buffer[
                (size_t)y * (size_t)platform->width + (size_t)x];
            uint8_t rgb[3] = {
                (uint8_t)(pixel >> 16U),
                (uint8_t)(pixel >> 8U),
                (uint8_t)pixel
            };
            if (fwrite(rgb, sizeof(rgb), 1U, file) != 1U) {
                fclose(file);
                return -1;
            }
        }
    }
    return fclose(file) == 0 ? 0 : -1;
}
