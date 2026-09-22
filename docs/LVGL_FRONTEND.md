# LVGL x86 Development Frontend

## Status and Scope

`ce_lvgl_host` is the first LVGL-based application path for the x86-64
development machine. It intentionally presents only the live 320x240 TI LCD.
It does not implement the future calculator home screen, menus, settings,
camera, networking, AI features, Buildroot integration, or T113 hardware
drivers.

The bounded dummy-video path has been built and exercised with the private ROM.
Its LVGL-composited screenshot is 320x240, contains five colors, and visibly
shows the TI-84 Plus CE OS 5.3.0.0037 `RAM Cleared` screen. The user has also
manually confirmed the desktop window, live updates, keyboard input, and TI-OS
calculations. This is x86 desktop validation only; it does not validate a T113
display or keypad backend.

## LVGL Dependency

LVGL is vendored as an unmodified nested Git checkout in `third_party/lvgl/`:

```text
version: v9.6.0
commit: 80ca777e37a2b176770726a02e07a6fb79ef0b39
upstream: https://github.com/lvgl/lvgl
license: MIT
```

The exact pin is also recorded in `third_party/LVGL_COMMIT`. Preserve
`third_party/lvgl/LICENCE.txt` and `third_party/lvgl/COPYRIGHTS.md` when
redistributing it.

CMake adds the vendored LVGL source with `add_subdirectory`; it does not use a
system LVGL package. The project configuration is `config/lv_conf.h`. LVGL's
built-in SDL driver is disabled because the v9.6.0 stable desktop driver uses
SDL2, while this project and its known-good reference host use SDL3. The thin
project-owned adapter in `platform/sdl3/` therefore connects LVGL to SDL3
without modifying the vendored dependency or loading SDL2 and SDL3 together.

SDL3 development headers and `pkg-config` metadata are required whenever
`CE_ENABLE_CEMU=ON`. LVGL itself is enabled by the `CE_ENABLE_LVGL` CMake
option, which defaults to `ON` and is considered only when CEmu is enabled.
The upstream LVGL CMake target enables C, C++, and assembly sources, so the
matching host compiler toolchain is required even though the project-owned
frontend code is C11.

## Architecture and Platform Boundary

```text
private ROM
    -> vendored headless CEmu core
    -> ce/emulator.h API
    -> app/ce_lvgl_app.c
    -> LVGL canvas and software renderer
    -> platform/sdl3/ce_lvgl_sdl3.c
    -> SDL3 window, renderer, and keyboard events
```

`app/ce_lvgl_app.c` is the portable application layer. It depends on LVGL and
the project emulator API, but contains no SDL or Linux display types.
`platform/sdl3/ce_lvgl_sdl3.c` owns all desktop window, renderer, texture,
clock, delay, and keyboard-event details. A later T113 display/input adapter
can replace that file without changing the public emulator API or reaching
into CEmu internals.

The existing `host/ce_host.c` remains the unchanged, minimal SDL3/CEmu
reference frontend.

## Framebuffer and Scheduling

The emulator API returns an emulator-owned row-major 320x240 `uint32_t`
framebuffer whose numeric format is `0xAARRGGBB`. On the little-endian x86-64
development host, its in-memory B,G,R,A byte order is exactly LVGL's
`LV_COLOR_FORMAT_ARGB8888`, so the LVGL canvas directly wraps that buffer.
LVGL then composites the canvas into its own full-frame XRGB8888 display
buffer. The SDL3 adapter uploads that composited buffer to an
`SDL_PIXELFORMAT_XRGB8888` streaming texture and uses nearest-neighbor,
integer logical scaling.

The host uses a real-time accumulator targeting 60 CEmu scheduler ticks per
wall-clock second. Every emulator call is exactly:

```c
ce_emulator_run(emulator, 1U);
```

No replacement CEmu scheduler is introduced. LVGL's timer handler and SDL's
event pump run in the same responsive loop, and elapsed-time catch-up is
bounded after a debugger stop or desktop stall.

## Desktop Input

SDL3 keyboard events are translated in the desktop adapter to generic TI
keypad matrix row/column press and release events. The callback in
`ce_lvgl_host` forwards them through `ce_emulator_key`, which is the existing
public API; no SDL or LVGL types enter that API.

| PC key | TI key | PC key | TI key |
| --- | --- | --- | --- |
| F1-F5 | Y=, Window, Zoom, Trace, Graph | Tab | 2nd |
| Home | Mode | Delete | Del |
| F12 | On | Backslash | Sto-> |
| S, N, I, D | ln, log, x^2, x^-1 | M | Math |
| Apostrophe | Alpha | X | X,T,theta,n |
| 0-9 | digits | Comma | comma |
| E, F, G | sin, cos, tan | Page Up | Apps |
| Page Down | Prgm | End | Stat |
| Insert | Vars | Grave/backtick | negative |
| Period | decimal point | `[`, `]` | `(`, `)` |
| Enter/keypad Enter | Enter | Equals/keypad + | `+` |
| Minus/keypad - | `-` | Keypad `*` | multiply |
| Slash/keypad / | divide | H | power |
| Escape | Clear | Arrow keys | directions |

Unlike the provisional reference-host map, `X` has one reachable meaning:
the TI `X,T,theta,n` key. Backslash is used for `Sto->`, avoiding the duplicate
mapping.

## Build and Run

```sh
cmake -S . -B build -DCE_ENABLE_CEMU=ON -DCE_ENABLE_LVGL=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure

./build/ce_lvgl_host \
  --rom ./ti-84ce.rom \
  --revision i \
  --scale 2
```

Supported frontend options are `--rom PATH` (required),
`--revision pre-a|i|m` (default I), `--scale 1..16` (default 3), and `--help`.
The validation-only options `--frames N` and `--screenshot PATH` provide a
bounded run and a PPM of LVGL's final composited draw buffer.

Headless validation example:

```sh
SDL_VIDEODRIVER=dummy ./build/ce_lvgl_host \
  --rom ./ti-84ce.rom --revision i --scale 1 --frames 600 \
  --screenshot /tmp/ti84ce-lvgl.ppm
```

## Known Limitations

- The UI is deliberately only a framebuffer canvas; it is not the final
  calculator application design.
- Desktop-window visuals and input are validated on x86; no target display or
  physical-keypad adapter exists yet.
- The keyboard map is provisional and based on physical scancodes; it is not a
  configurable logical-key input system.
- The application-facing API does not yet declare framebuffer dimensions or
  format, so this layer records the established 320x240 ARGB8888 contract.
- CEmu remains single-instance/global-state oriented and requires the private
  external ROM at runtime.
- The SDL3 adapter is x86 development infrastructure. T113 display and
  physical-keypad adapters have not started.
