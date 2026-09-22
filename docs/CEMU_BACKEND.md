# CEmu Backend

## Status

The private ROM boots through CEmu's mature core on x86-64 with explicit
revision I and reaches flash-resident TI-OS code (not just early boot).
The SDL3 host (`ce_host`) now visibly renders the real TI-OS boot screen
("TI-84 Plus CE", OS version, "RAM Cleared") -- see "Root Cause: Black
Framebuffer" below for the fix that made this actually work, after an earlier
uninstrumented check incorrectly reported this as already confirmed. A private
ROM may exist locally as external runtime data, but it is not embedded in the
source or build and must never be committed or redistributed.

## Reference Version

The reference checkout is CEmu commit `fb10bfe` (`gui: fix backward scrolling
for addresses with labels`) from the upstream repository:

```text
https://github.com/CE-Programming/CEmu
```

The pinned source is vendored under `third_party/cemu/`; the application
wrapper and headless platform glue remain outside CEmu sources. The separate
`/tmp/cemu-reference` checkout was used for differential diagnostics.

CEmu is GPLv3. Any CEmu-derived component must preserve its GPLv3 license and
copyright notices, and distribution of a combined CEmu-derived component must
follow the GPLv3 requirements.

## CEmu Core Findings

CEmu's actual calculator implementation is in `core/`, including:

- `cpu.c`/`cpu.h`: eZ80 execution, ADL/mixed-mode state, interrupts, and stepping.
- `mem.c`/`mem.h`: flash, RAM, memory mapping, protection, and bus accesses.
- `asic.c`/`asic.h`: ASIC/revision selection and reset wiring.
- `control.c`: CE control ports and protection ranges.
- `interrupt.c`: interrupt controller.
- `timers.c`: hardware timers and scheduler events.
- `lcd.c`/`panel.c`: LCD and framebuffer-related state.
- `keypad.c`: keypad matrix behavior.
- `emu.c`: ROM loading, reset, and execution scheduling.
- `registers.c` and `schedule.c`: CPU register representation and deterministic
  scheduling/cycle progression.

The Qt GUI is not required by the core. The project CMake target builds the
Linux-compatible CEmu core modules and excludes physical USB implementations.
`third_party/cemu_headless.c` supplies GUI callbacks, explicit ASIC revision
selection, and no-op USB callbacks for headless mode.

## Differential Trace

The separate reference harness supplies reset callbacks, selects ASIC revision
I, loads the same external ROM, and logs CPU state at CEmu's internal fetch
boundary. The comparison matched the reset prefix through:

```text
F3, ED 7E, 5B, C3, AF, ED 39, 40
```

The first divergence originally found at `0x000E54 LD BC,nn` was that CEmu
consumed a 16-bit operand and reached `0x000E57`, while the from-scratch backend
reached `0x000E58`. That from-scratch bug is now fixed and covered by
`test_ld_bc_immediate_is_16_bit`; the current ROM probe also reaches
`0x000E57`. The CEmu trace exposes separate data-width and instruction-width
flags (`L` and `IL`), which remain more precise than the from-scratch public
mode fields.

## Wrapper Direction

The eventual application-facing API will own no CEmu internal types. It will
provide ROM loading, explicit revision selection, reset, run slices, pause/
resume, keypad events, framebuffer access, and basic state/debug inspection.
The initial wrapper should link the CEmu core only, with Qt, SDL, LVGL,
Buildroot, Linux display types, and physical-device dependencies excluded.

## Wrapper API

The custom API lives in `core/include/ce/emulator.h` and
`core/src/cemu_backend.c`. It exposes ROM loading, reset, run, pause/resume,
debug state, a 320x240 native-CEmu-format framebuffer, and keypad events
without exposing CEmu internal types.

The CEmu probe reaches deep into flash-resident TI-OS code (well past boot),
for example:

```text
PC=0x08575D SP=0xD1A854 AF=0xCC54 BC=0x000715
DE=0x000138 HL=0xD00587 IY=0xD00080 MBASE=0xD0
ADL=1 MADL=0 IFF1=1 IFF2=1 IM=2 halted=1 cycles=4000000
```

This PC value confirms boot genuinely completes under CEmu; the boot probe's
large `--ticks` value is fine for a CPU-state probe (it never samples the
LCD), but see below for why the same tick count is wrong for the SDL host.

## Root Cause: Black Framebuffer (resolved)

A previous status here incorrectly claimed the framebuffer showed confirmed
TI-generated pixel output. That claim was based on an invalid check ("contains
nonzero bytes" over the whole PPM file, which also matches the ASCII PPM
header bytes such as `P6`). A rigorous check (ImageMagick `identify -verbose`)
later showed the real screenshot was uniformly `#000000`, exposing four real
bugs in `host/ce_host.c`, all now fixed:

1. **Pathological tick count.** `ce_emulator_run(emulator, 1000000U)` was
   called once per host video frame. The CEmu backend's `ticks` parameter is
   in `CLOCK_RUN` units, which run at 60 Hz by default
   (`third_party/cemu/core/schedule.c` `sched_reset()`'s `def_rates[]`). CEmu's
   own Qt frontend never does this: `gui/qt/emuthread.cpp`'s `EmuThread::run()`
   calls `emu_run(1u)` once per loop iteration (one 60 Hz tick, i.e. 1/60
   emulated second), then throttles to real time. Requesting 1,000,000 ticks
   per call requested about 4.6 emulated *hours* per single host frame. This
   alone explained the `--frames` hang, and gave TI-OS's own auto-power-down
   more than enough emulated idle time (with zero key input) to blank the LCD
   before any screenshot was taken -- clearing LCD control bit 11 (the
   PL111-style panel power-enable bit) that `emu_lcd_drawframe()` in
   `third_party/cemu/core/lcd.c` checks before drawing anything. Fixed by
   advancing exactly 1 tick per host frame, matching upstream's emulated-time
   step size.
2. **Pixel format mismatch.** CEmu's LCD output helpers
   (`lcd_argb8888out`/`lcd_rgb565out`/`lcd_rgb888out` in `core/lcd.c`) always
   produce `0xAARRGGBB` (alpha in the top byte, then red, green, blue) --
   `SDL_PIXELFORMAT_ARGB8888`, not `SDL_PIXELFORMAT_RGBA8888`. The host
   texture was created as `RGBA8888`; fixed to `ARGB8888`.
3. **PPM channel extraction bug.** `write_ppm()` extracted
   `pixel>>24/16/8` as R/G/B, which (given bug 2's real layout) actually wrote
   alpha/red/green and silently dropped blue. Fixed to extract
   `pixel>>16/8/0` as R/G/B.
4. **Screenshot taken on the wrong frame.** `--screenshot` wrote the PPM after
   the *first* frame with a non-null path, then cleared the path -- i.e. it
   captured the very first emulated frame (1/60 second into boot) regardless of
   `--frames`. Fixed so a bounded run (`--frames N`) writes the screenshot once,
   after the final frame.

With all four fixed, a bounded headless run shows the real TI-OS boot screen
(status bar plus "TI-84 Plus CE", OS version, and "RAM Cleared"), confirmed
with `identify -verbose` (5 unique colors, mean well above 0, max channel
value 255) and confirmed to change between different `--frames` counts (not a
frozen buffer). No CEmu LCD/panel logic was reimplemented; the fix only
corrected how the host calls into CEmu's existing, unmodified
`emu_run()`/`emu_lcd_drawframe()` machinery.

## Execution and Host

The wrapper calls CEmu's existing `emu_run()` entry point. CEmu advances its
run-clock scheduler, processes pending timer/LCD/interrupt/DMA events, and then
lets `cpu_execute()` run until the scheduled boundary. A CPU HALT is not
treated as application shutdown; scheduler time continues and CEmu can wake
the CPU when an eligible event or interrupt occurs. No parallel scheduler was
added to the wrapper.

`ce_emulator_run`'s `ticks` argument is in CEmu `CLOCK_RUN` units (60 Hz by
default). A video host should advance 1 tick per frame
(`ce_emulator_run(emulator, 1U)`), not a large batch -- see "Root Cause: Black
Framebuffer" above for what goes wrong otherwise. The current SDL host does
not add a wall-clock throttle or explicitly request vsync, so the one-tick
step fixes emulated-time progression but does not itself guarantee 60 frames
per real second. The boot probe intentionally uses a much larger tick count
because it only inspects CPU debug state, not the LCD.

The SDL3 executable is `ce_host`:

```sh
./build/ce_host --rom ./ti-84ce.rom --revision i
```

It presents the existing 320x240 native-CEmu-format (`ARGB8888`) framebuffer
using nearest-neighbor scaling and integer logical presentation where
possible. For a headless check:

```sh
SDL_VIDEODRIVER=dummy ./build/ce_host \
  --rom ./ti-84ce.rom --revision i --frames 600 \
  --screenshot /tmp/ti84ce.ppm
```

`--frames N` runs for `N` emulated 1/60-second ticks and writes the screenshot
once, after the final frame. The resulting PPM shows the real TI-OS boot
screen ("TI-84 Plus CE", OS version, "RAM Cleared") -- verified with
`identify -verbose` (multiple unique colors, nonzero mean, max channel value
255), not just "nonzero bytes somewhere in the file".

Use `--scale N` to choose the initial integer window scale, for example
`--scale 4`. The framebuffer remains 320x240 regardless of window scale.

Temporary PC keyboard mapping:

- Arrows: directional keypad entries
- Enter: Enter
- Escape: Clear
- Delete: Delete
- Tab: 2nd
- Apostrophe: Alpha
- Home: Mode
- F5: Graph, F4: Trace, F3: Zoom, F2: Window, F1: Y=
- End: Stat, M: Math, PageUp: Apps, PageDown: Prgm, Insert: Vars
- `0`-`9`: numeric entries
- Period: decimal point
- Equals: `+`, Minus: `-`, keypad `*`: `*`, keypad `/`: `/`
- Left bracket: `(`, Right bracket: `)`

The path is host keyboard event -> `ce_emulator_key` -> CEmu
`emu_keypad_event`.

## Milestones

1. Vendor/pin CEmu source and preserve GPLv3 notices. **Complete.**
2. Build a headless CEmu core without Qt or physical USB dependencies. **Complete.**
3. Add the custom emulator C API wrapper. **Initial slice complete.**
4. Boot the private ROM with explicit revision I. **Complete.**
5. Expose CEmu framebuffer and keypad through the wrapper. **Initial accessors complete.**
6. Add backend selection while retaining the from-scratch targets. **An initial runtime backend enum and wrapper path exist, and the standalone from-scratch targets remain. The wrapper is still compiled only as part of `ce_cemu_core`, its framebuffer/key input remain CEmu-only, and its run units differ by backend.**

## LVGL Consumer

The first application-layer consumer now lives in `app/ce_lvgl_app.c`, with
the x86 entry point in `host/ce_lvgl_host.c`. It depends only on this public
emulator wrapper and LVGL; it does not include CEmu internals or SDL types.
The replaceable desktop boundary is `platform/sdl3/ce_lvgl_sdl3.c`.

The CEmu framebuffer is wrapped by an LVGL ARGB8888 canvas on the little-endian
x86-64 development host. LVGL composites that source into the desktop
adapter's XRGB8888 display buffer. The frontend targets 60 scheduler ticks per
wall-clock second and advances CEmu only through one-tick calls. Keyboard
events return through `ce_emulator_key` and `emu_keypad_event`; no alternate
keypad or scheduler implementation was added.

The bounded dummy-video LVGL path has rendered the expected 320x240 TI-OS
screen. User validation of the live desktop window and keyboard interaction is
still pending. Dependency pinning, build/run commands, mappings, and the later
T113 replacement boundary are documented in `docs/LVGL_FRONTEND.md`.
