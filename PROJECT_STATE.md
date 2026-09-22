# Calculator Emulator Project State

Last verified: 2026-09-22

This is the master checkpoint for the project. `STATUS.md` is the detailed
historical engineering log; `docs/FROM_SCRATCH_EMULATOR.md` and
`docs/CEMU_BACKEND.md` give backend-specific detail. When their historical
language sounds like the current project direction, this file takes
precedence.

## Current State in One Page

This repository is the standalone TI emulator application for a larger Linux
calculator system. It is not the calculator launcher/home application, CAS,
AI/camera application, or complete product UI. Those are separate future
projects which may launch and manage this process. The final emulator hardware
target is an Allwinner T113-S3 (dual-core ARM Cortex-A7) running a lightweight
32-bit Linux system, likely Buildroot, with a custom display and keypad.

TI-84 Plus CE compatibility is currently provided by a headless integration
of CEmu behind a small project-owned C API. This path is the primary emulator
backend. It has booted the user's private ROM into the real TI-OS UI, rendered
the 320x240 LCD through the project's SDL3 host, and accepted keyboard input.
The user previously verified the complete interactive path by entering
`69 + 420` in TI-OS and receiving `489`.

The original from-scratch eZ80/CE emulator remains present, buildable, tested,
and useful as historical and diagnostic work. It is not the production
backend and should not be resumed as a full CEmu replacement unless explicitly
requested.

The first LVGL application path now exists on x86 Linux. `ce_lvgl_host` wraps
the live CEmu framebuffer in an LVGL canvas and presents LVGL's composited
output through a project-owned SDL3 adapter. Its bounded output and live
desktop-window/input path have both been manually validated by the user. The
existing `ce_host` remains unchanged as the minimal known-good emulator
frontend.

The stable deployable process is `calculator-emulator`. It loads an external
ROM, runs as a well-behaved headless Linux process, handles `SIGINT`/`SIGTERM`,
and provides a portable one-tick-granularity benchmark. SDL3 is no longer
required for headless builds. A Cortex-A7 hard-float toolchain file now produces
a verified ELF32 ARM EABI5 executable, and the portable LVGL layer also
cross-compiles. No T113 runtime, display, keypad, thermals, or performance
claim has been made without hardware.

## Product and Hardware Goal

The emulator is one separately launched process in the intended device. The
larger system is expected eventually to contain:

- this TI emulator application;
- a separate launcher/home and settings application;
- separate future CAS and advanced-calculator applications;
- separate future AI/camera/networking applications; and
- physical display and keypad integration on the T113-S3 device.

The current development host is x86-64 Arch Linux. The intended migration is a
native ARM build; x86 itself will not be emulated on the T113.

## Architecture

Current x86 validation path:

```text
private 4 MiB TI ROM
        |
        v
vendored CEmu core (headless, no Qt, physical USB stubbed)
        |
        v
core/include/ce/emulator.h + core/src/cemu_backend.c
        |
        +---- debug state
        +---- keypad matrix events
        +---- 320x240 0xAARRGGBB framebuffer
        |
        v
host/ce_host.c (SDL3 development/debug frontend)
        |
        v
desktop window or dummy-video screenshot
```

Current LVGL application architecture on x86:

```text
CEmu -> project emulator API -> portable LVGL application/canvas
                            -> project SDL3 LVGL display/input adapter
                            -> desktop window
```

Standalone/deployment architecture:

```text
future launcher (separate process/project)
        |
        v
calculator-emulator --rom ABSOLUTE_PATH
        |
        v
project emulator API -> headless CEmu core
        |
        +---- current: real-time headless run / portable benchmark
        +---- future: LVGL plus target display/input adapters
```

Planned hardware architecture:

```text
private ROM -> CEmu -> project emulator API -> application -> LVGL
                                                       |
                                                       v
                                      T113/Linux display + physical keypad
```

SDL, LVGL, Linux display drivers, SPI, and physical keypad details must remain
outside the emulator core and its public API.

## Repository Layout

| Path | Purpose |
| --- | --- |
| `README.md` | Repository role and native-development quick start. |
| `CMakeLists.txt` | Build options, targets, and standalone-app install rule. |
| `cmake/toolchains/armv7a-linux-gnueabihf.cmake` | Configurable Cortex-A7/ARMv7-A hard-float Linux cross-toolchain definition. |
| `core/include/ce/emulator.h` | Backend-neutral application-facing emulator API. |
| `core/src/cemu_backend.c` | Current implementation of that API for both backend choices; links directly to CEmu only inside this translation unit. |
| `core/include/ce/machine.h`, `core/src/machine.c` | Preserved from-scratch machine, CPU subset, memory/MMIO, and interrupt model. |
| `core/include/ce/rom.h`, `core/src/rom.c` | Strict raw 4 MiB ROM loader and conservative revision types. |
| `core/tests/test_rom.c` | Focused tests for the from-scratch core. |
| `host/ce_boot_probe.c` | Strict instruction-by-instruction probe for the from-scratch core. |
| `host/cemu_boot_probe.c` | Headless CEmu debug-state probe. |
| `host/ce_host.c` | SDL3 reference frontend, keyboard bridge, and PPM screenshot writer. |
| `host/ce_lvgl_host.c` | LVGL frontend entry point, CLI, and real-time one-tick scheduler loop. |
| `app/calculator_emulator.c` | Standalone target process, CLI, signals, real-time headless loop, and benchmark. |
| `app/ce_lvgl_app.*` | Portable LVGL canvas/application layer above the emulator API. |
| `platform/sdl3/ce_lvgl_sdl3.*` | Replaceable desktop LVGL display, clock, screenshot, and keyboard adapter. |
| `config/lv_conf.h` | Project LVGL configuration. |
| `third_party/cemu/` | GPLv3 CEmu checkout pinned at the commit below, including its unused Qt/SDL frontends and tests. |
| `third_party/lvgl/` | Unmodified MIT-licensed LVGL v9.6.0 checkout pinned at the commit below. |
| `third_party/cemu_headless.c` | Project-owned GUI callback, revision-selection, and no-physical-USB glue. |
| `docs/ARM_T113_DEPLOYMENT.md` | Cross-build, ABI, install, Buildroot, benchmark, and first-board procedure. |
| `docs/` | Backend and platform-specific project documentation. |
| `STATUS.md` | Detailed chronological investigation log; useful but no longer the best current-state entry point. |

The project root is a Git working tree. CEmu and LVGL are proper Git submodules
at the pinned commits recorded below. Root `/*.rom` is ignored; the private ROM
must remain untracked external runtime data.

## Build System and Targets

The project requires CMake 3.20 or newer and a C11 compiler. The vendored
LVGL CMake target also enables C++ and assembly languages, so the normal LVGL
build needs the corresponding host toolchain. `CTest` is enabled through
CMake's standard `BUILD_TESTING` option.

`CE_ENABLE_CEMU`, `CE_ENABLE_LVGL`, `CE_ENABLE_DESKTOP_FRONTENDS`, and
`CE_BUILD_TARGET_APP` default to `ON`. `CE_ENABLE_LVGL` builds the portable
LVGL application layer. `CE_ENABLE_DESKTOP_FRONTENDS` exclusively controls
SDL3 discovery and the two desktop hosts, allowing target/headless builds to
remain SDL-independent.

Always-built targets:

| Target | Type | Purpose |
| --- | --- | --- |
| `ce_core` | static library | From-scratch `machine.c` and `rom.c`. |
| `ce_rom_tests` | executable/CTest test | From-scratch focused tests; only built with `BUILD_TESTING=ON`. |
| `ce_boot_probe` | executable | External-ROM from-scratch boot trace and opcode coverage. |

Additional targets with `CE_ENABLE_CEMU=ON`:

| Target | Type | Purpose |
| --- | --- | --- |
| `ce_cemu_core` | static library | Selected CEmu core sources, Linux OS glue, headless callbacks, and the project emulator wrapper. |
| `ce_cemu_boot_probe` | executable | Runs CEmu for a requested number of scheduler ticks and prints CPU state. |
| `ce_host` | executable | SDL3 framebuffer/input frontend. |
| `ce_lvgl_app` | static library | Portable LVGL canvas/application integration. |
| `ce_lvgl_sdl3` | static library | Replaceable SDL3 desktop adapter for LVGL. |
| `ce_lvgl_host` | executable | First x86 LVGL frontend. |
| `calculator-emulator` | executable/install target | Standalone Linux emulator process and portable benchmark; no SDL dependency. |

SDL3 and `pkg-config` are required only with
`CE_ENABLE_DESKTOP_FRONTENDS=ON`. They are not configured or linked for the
standalone application or headless ARM build. With CEmu disabled, only the
from-scratch targets are built.

Normal build and test:

```sh
cmake -S . -B build -DCE_ENABLE_CEMU=ON -DCE_ENABLE_LVGL=ON \
  -DCE_ENABLE_DESKTOP_FRONTENDS=ON \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Preservation build without CEmu:

```sh
cmake -S . -B build-no-cemu -DCE_ENABLE_CEMU=OFF \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build-no-cemu --parallel
ctest --test-dir build-no-cemu --output-on-failure
```

CTest registers `ce_rom_tests` and a no-ROM `calculator_emulator_help` CLI
test. ROM probes and SDL/LVGL end-to-end runs remain manual validations because
they require the private ROM.

## Public Emulator API

The public abstraction is `core/include/ce/emulator.h`. It deliberately
contains no SDL, LVGL, Linux display, or CEmu internal types.

Types:

- `ce_backend`: `CE_BACKEND_FROM_SCRATCH` or `CE_BACKEND_CEMU`.
- `ce_emulator_config`: selected backend and explicit `ce_revision`.
- `ce_emulator_debug_state`: PC, SP, AF/BC/DE/HL/IX/IY, MBASE, cycle count,
  ADL/MADL-style flags, interrupt flip-flops/mode, and HALT state.
- `ce_emulator`: opaque handle.

Functions:

| Function | Current behavior |
| --- | --- |
| `ce_emulator_create` | Allocates a handle, stores the config, allocates the CEmu framebuffer, and selects the requested CEmu ASIC revision. |
| `ce_emulator_destroy` | Frees the selected backend and wrapper storage. |
| `ce_emulator_load_rom` | Loads an external ROM into the selected backend. |
| `ce_emulator_reset` | Resets a loaded backend. |
| `ce_emulator_run` | Advances the selected backend unless paused. The meaning of `ticks` is backend-dependent; see below. |
| `ce_emulator_pause`, `ce_emulator_resume`, `ce_emulator_is_paused` | Wrapper-level execution gate. |
| `ce_emulator_framebuffer` | Refreshes and returns CEmu's `uint32_t` framebuffer. It currently returns `NULL` for the from-scratch backend. |
| `ce_emulator_get_debug_state` | Copies a backend-neutral CPU/debug snapshot. |
| `ce_emulator_key` | Sends a row/column press or release to CEmu. It is currently a no-op for the from-scratch backend. |

Important API limitations:

- For CEmu, `ticks` means `CLOCK_RUN` scheduler ticks (60 per emulated second
  by default). For the from-scratch backend, it means a maximum number of CPU
  instructions. Callers cannot treat the units as interchangeable.
- `ce_emulator_run` returns `void`; from-scratch stop/error status is not
  surfaced through the wrapper.
- The framebuffer size and pixel format are not declared in `emulator.h`.
  Consumers currently rely on the documented 320x240 ARGB contract.
- CEmu uses global core state. The wrapper should be treated as supporting one
  active CEmu instance per process; multi-instance safety is not implemented.
- Revision validation is permissive in this wrapper: an unknown/non-M/non-pre-A
  CEmu revision falls back to revision I. The strict from-scratch machine
  rejects `CE_REVISION_UNKNOWN`.
- The wrapper implementation is compiled into `ce_cemu_core`, so the public
  abstraction itself is not available as a separate library when
  `CE_ENABLE_CEMU=OFF`, despite containing a from-scratch code path.

Application and future LVGL code should still use this boundary, then improve
the boundary deliberately when a concrete application need exposes one of
these limitations. It should not reach into CEmu globals directly.

## Primary Backend: CEmu

The vendored checkout is exactly:

```text
fb10bfea6f5557a9d3696db6f822a72806b60cc8
gui: fix backward scrolling for addresses with labels
```

This was verified both from `third_party/cemu/CEMU_COMMIT` and with
`git -C third_party/cemu rev-parse HEAD`. The nested checkout reports
`CEMU_COMMIT` itself as untracked and its `core/debug/zdis` submodule as
uninitialized. The current project build does not compile CEmu's debug/zdis
subdirectory and is unaffected, but a future full-CEmu/vendor refresh must
handle the submodule explicitly.

CEmu is GPLv3. `third_party/cemu/LICENSE` must be preserved, and distribution
of the combined CEmu-derived program must comply with GPLv3. The ROM is not
part of CEmu and must never be distributed with it.

The project builds the top-level C files under `third_party/cemu/core/` plus
`core/os/os-linux.c`. It does not build CEmu's Qt GUI, its SDL2 frontend, or
physical USB implementation. `third_party/cemu_headless.c` provides:

- no-op console callbacks;
- explicit pre-A/I/M ASIC revision selection through `gui_handle_reset`; and
- a safe disconnected/no-op USB port implementation and callbacks.

CEmu supplies the mature eZ80 CPU, memory/flash, ASIC revision behavior,
interrupts, timers, DMA, keypad, LCD/panel, scheduling, and related CE hardware
models. These should be reused rather than recreated in project code.

### Scheduler Contract

`emu_run(ticks)` advances CEmu's existing event scheduler. `CLOCK_RUN` defaults
to 60 Hz, and CEmu's Qt frontend calls `emu_run(1u)` per emulator loop. The
project host correspondingly calls `ce_emulator_run(emulator, 1U)` once per
rendered host frame.

One tick therefore represents 1/60 of an emulated second. The SDL host does
not currently add its own wall-clock throttle or explicitly request a vsynced
renderer, so “one tick per frame” fixes the emulated-time unit without
guaranteeing exactly 60 frames per real second.

A halted eZ80 CPU is not an application error. CEmu must keep advancing its
scheduler so timers, LCD, DMA, events, and eligible interrupts can progress and
wake the CPU.

## Preserved Backend: From Scratch

The original backend is a portable C, boot-driven partial TI-84 Plus CE
implementation. It includes substantial eZ80 Z80/ADL/mixed-mode work, MBASE
addressing, 24-bit control flow and stacks, many base/ED/CB/DD forms, complete
standard CB operations, selected indexed-DD-CB operations, flash protection
behavior, CE memory mapping, an interrupt-controller slice, IM1, EI delay, DI,
HALT release, and deterministic tests.

Its current private-ROM path executes 653 instructions through the later
RAM/stack validation failure, executes `HALT` at `0x001414`, and stops on the
next step with `CE_STEP_HALTED`, `PC=0x001415`, and 1,742 modeled cycles. No
unsupported opcode is encountered on this path. The HALT belongs to TI boot
code's deliberate crash path after the validation helper returns carry; it is
not a normal timer-wait state.

An older differential report said this backend consumed a three-byte operand
for `LD BC,nn` at `0x000E54` and diverged from CEmu. That issue is now resolved:
the current source and focused test use a 16-bit operand and the ROM probe
advances to `0x000E57`, matching CEmu at that point.

This backend remains incomplete: it is not a production CE hardware model and
does not provide the finished LCD, keypad, timer, DMA, RTC, USB, full flash
command, revision, or full eZ80 behavior needed to boot TI-OS normally. Keep
it buildable and tested, but use CEmu for the product path.

## ROM Handling Rules

The development ROM is the user's own private dump and is external runtime
data. In this workspace it is named `ti-84ce.rom` and is exactly 4 MiB, as
required by the from-scratch loader.

Rules:

- never commit, upload, redistribute, embed, copy into build artifacts, or
  include the ROM in a release;
- always pass it explicitly with `--rom PATH`;
- do not publish its bytes or identifying digest; and
- keep root-level `*.rom` files ignored by version control.

The root currently has no Git metadata, so the ignore file is a safeguard for
future repository initialization rather than proof that the ROM is untracked.

## Framebuffer Contract

The CEmu-backed API returns a row-major 320x240 array of native `uint32_t`
pixels. Each numeric pixel is:

```text
0xAARRGGBB
```

This is ARGB8888, not RGBA8888. `ce_host` therefore creates an
`SDL_PIXELFORMAT_ARGB8888` streaming texture with a pitch of `320 * 4`. The PPM
writer extracts red from bits 16-23, green from bits 8-15, and blue from bits
0-7. Alpha is not written to PPM. On the little-endian development host the
same values are byte-for-byte LVGL `LV_COLOR_FORMAT_ARGB8888`. The LVGL canvas
wraps the emulator-owned source buffer, and LVGL composites it into the
separate XRGB8888 display buffer consumed by the SDL3 adapter.

The from-scratch `ce_machine` owns an internal 16-bit framebuffer, but the
application-facing framebuffer accessor does not expose it today.

## SDL3 Reference Host

`ce_host` is a development/debugging frontend, not the future product UI. Keep
it small and preserve it after LVGL work begins because it is the simplest
known-good end-to-end CEmu frontend.

Typical interactive launch:

```sh
./build/ce_host --rom ./ti-84ce.rom --revision i --scale 2
```

Accepted options in the current parser:

| Option | Meaning |
| --- | --- |
| `--rom PATH` | Required external ROM path. |
| `--revision pre-a\|i\|m` | ASIC revision; defaults to I. Unrecognized text also currently falls back to I. |
| `--scale N` | Initial integer window scale; defaults to 3 and clamps 0 to 1. |
| `--frames N` | Stop after N one-tick frames. Zero or omission means unbounded. |
| `--screenshot PATH` | Write a binary 320x240 PPM. A bounded run writes after its final frame; an unbounded run rewrites it every frame. |

Unknown/malformed options are not diagnosed robustly, and there is no help
option. Those are frontend limitations, not emulator failures.

Headless visual validation:

```sh
SDL_VIDEODRIVER=dummy ./build/ce_host \
  --rom ./ti-84ce.rom --revision i --frames 600 \
  --screenshot /tmp/ti84ce.ppm
```

### Current Keyboard Mapping

The host uses SDL physical scancodes and sends raw CEmu keypad matrix
row/column events. Modifiers are not interpreted.

| PC key | TI key | PC key | TI key |
| --- | --- | --- | --- |
| F1 | Y= | F2 | Window |
| F3 | Zoom | F4 | Trace |
| F5 | Graph | Tab | 2nd |
| Home | Mode | Delete | Del |
| F12 | On | X | Sto-> |
| S | ln | N | log |
| I | x^2 | D | x^-1 |
| M | Math | Apostrophe | Alpha |
| 0-9 | digits | Comma | comma |
| E | sin | F | cos |
| G | tan | Page Up | Apps |
| Page Down | Prgm | End | Stat |
| Grave/backtick | negative | Insert | Vars |
| Period | decimal point | `[` | `(` |
| `]` | `)` | Enter | Enter |
| Equals | `+` | Minus | `-` |
| Keypad `*` | multiply | Keypad `/` | divide |
| H | power | Escape | Clear |
| Arrow keys | directions | | |

There is a second `X` entry intended for the TI `X,T,theta,n` key, but the
earlier `X -> Sto->` entry wins the linear lookup, making the later entry
unreachable. This is a known provisional-mapping defect, not corrected during
this discovery-only pass.

The intended future input route is:

```text
physical keypad -> project logical calculator input API -> emulator backend
```

## LVGL x86 Frontend

The reproducibly vendored dependency is LVGL v9.6.0 at commit:

```text
80ca777e37a2b176770726a02e07a6fb79ef0b39
```

LVGL is MIT licensed. Its unmodified checkout, license, and copyright notices
are under `third_party/lvgl/`; `third_party/LVGL_COMMIT` records the pin.
CMake builds that checkout directly using `config/lv_conf.h`. LVGL's bundled
SDL driver is disabled because the pinned stable release uses SDL2; the
project-owned adapter uses the same SDL3 dependency as `ce_host` and avoids
mixing SDL major versions.

`ce_lvgl_host` owns CLI parsing and real-time coordination. It targets 60 CEmu
ticks per wall-clock second with a bounded accumulator and makes every
emulator call with exactly one tick. `ce_lvgl_app` owns the portable LVGL
canvas above `ce/emulator.h`; `ce_lvgl_sdl3` owns desktop display, timing,
screenshots, and PC keyboard mapping. The SDL3 boundary can later be replaced
without modifying CEmu or the public emulator API.

The LVGL host supports the required `--rom`, `--revision`, and `--scale`
options, plus bounded `--frames` and `--screenshot` validation. Its provisional
keyboard map is based on the reference host but maps `X` only to the TI
`X,T,theta,n` key and uses Backslash for `Sto->`, removing the duplicate-X
defect. Full details are in `docs/LVGL_FRONTEND.md`.

Interactive launch:

```sh
./build/ce_lvgl_host --rom ./ti-84ce.rom --revision i --scale 2
```

The user has now manually confirmed the LVGL desktop window, live TI-OS
updates, keyboard input, and calculations. This validates the complete x86
development path but does not validate a T113 display backend.

## Standalone Linux Application

`calculator-emulator` is the stable executable intended for eventual launch by
the separate calculator launcher. It links the CEmu-backed project API but no
SDL library. Its required `--rom PATH` accepts an absolute or caller-relative
external ROM path; there is no repository or current-working-directory
assumption. `--revision pre-a|i|m` defaults to I, and `--help` succeeds without
a ROM. Invalid CLI values and ROM load failures produce useful messages and
distinct nonzero exit statuses.

Until a real target display/input adapter is selected, normal operation is
explicitly headless. It advances at 60 one-tick CEmu scheduler calls per
wall-clock second and exits cleanly on `SIGINT` or `SIGTERM`. The portable
benchmark removes pacing but retains one-call-per-tick granularity:

```sh
./build/calculator-emulator --rom ./ti-84ce.rom --revision i \
  --benchmark-ticks 3600
```

It reports emulated seconds, elapsed monotonic time, scheduler ticks/second,
and real-time factor. `1.0` means the required 60 ticks/second. CPU, RSS,
temperature, frequency, and throttling should be recorded with target Linux
tools when hardware arrives.

## ARMv7-A Cross-Build Status

`cmake/toolchains/armv7a-linux-gnueabihf.cmake` supplies a configurable 32-bit
Linux toolchain for the T113-S3 Cortex-A7. The validated settings are
`-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard`. A checksum-verified
Bootlin GCC 15.3.0 ARMv7-EABI hard-float toolchain successfully built the CEmu
core, standalone executable, probes, LVGL, and portable `ce_lvgl_app` layer.
Desktop frontends were disabled and SDL3 was not linked.

The standalone Release binary was verified as ELF32, little-endian ARM,
EABI5, hard-float, PIE, using `/lib/ld-linux-armhf.so.3`; its only dynamic
library dependency is the target libc. `readelf -A` reports ARMv7 application
profile, VFPv4, NEON, and VFP-register arguments. This is a successful
cross-compilation and ABI inspection only. It has not executed on T113
hardware. Exact commands, install layout, Buildroot guidance, and first-board
steps are in `docs/ARM_T113_DEPLOYMENT.md`.

## Resolved Black-Screen Incident

The CEmu LCD was not defective. Four host integration bugs compounded:

1. The host called `ce_emulator_run(..., 1000000)` per video frame. At 60
   scheduler ticks per emulated second, that advanced roughly 4.6 emulated
   hours per frame and let TI-OS auto-power-down blank the LCD.
2. CEmu produced ARGB8888 while the SDL texture was declared RGBA8888.
3. The PPM writer extracted alpha/red/green instead of red/green/blue.
4. Bounded screenshots were captured after the first frame instead of after
   the requested final frame.

The host now advances one CEmu scheduler tick per frame, uses ARGB8888,
extracts the correct PPM channels, and captures a bounded screenshot at the
end. No replacement LCD or panel implementation was needed.

A nonzero file or nonzero pixel byte is not sufficient visual validation:
PPM headers are nonzero, and a black ARGB framebuffer can have nonzero alpha.
Validate RGB extrema/diversity and visually inspect representative output.

## Verified Results on 2026-09-21

Environment observed during this checkpoint:

- x86-64 Arch Linux workspace;
- GCC 16.2.1 (`/usr/bin/cc`);
- CMake Debug build; and
- SDL3 3.4.16 through `pkg-config`.

Results:

- Clean `CE_ENABLE_CEMU=ON` configure/build: passed. GCC emitted four warnings
  inside vendored CEmu (`keypad.c`, `cpu.c`, `flash.c`, and `mem.c`); project
  sources built without a reported warning.
- CTest with CEmu enabled: both the core suite and standalone CLI help test
  passed.
- Clean `CE_ENABLE_CEMU=OFF` configure/build: passed.
- CTest with CEmu disabled: 1/1 passed.
- `ce_boot_probe --count 120`: passed and reported no unsupported opcodes.
- Full from-scratch strict probe: reached the deliberate fatal HALT described
  above; its nonzero exit is expected because the next step returns
  `CE_STEP_HALTED`.
- `ce_cemu_boot_probe --ticks 1000000`: passed with
  `PC=0x08575D`, `SP=0xD1A854`, `MBASE=0xD0`, `ADL=1`, `MADL=0`, `IM=2`,
  `halted=1`, and 4,000,000 CPU cycles.
- Dummy-video `ce_host --frames 600`: passed and produced a 320x240 PPM with
  five unique colors. Visual inspection showed the real TI-84 Plus CE screen,
  OS `5.3.0.0037`, and `RAM Cleared`.
- LVGL v9.6.0 and the new application/SDL3 adapter targets built successfully.
- Dummy-video `ce_lvgl_host --frames 600`: passed in about 10 wall-clock
  seconds and produced LVGL's final composited 320x240 PPM with five unique
  colors. Visual inspection showed the same TI-84 Plus CE OS 5.3.0.0037
  `RAM Cleared` screen. Its PPM matched the reference host capture byte for
  byte, independently confirming orientation and RGB interpretation.
- The user manually confirmed the LVGL desktop window, live updates, TI-OS
  keyboard interaction, and calculations.
- A 120-frame capture differed byte-for-byte from the 600-frame capture,
  confirming that bounded runs honor their requested endpoint and the LCD is
  not a frozen buffer.
- The earlier manual interactive milestone remains the strongest input test:
  TI-OS evaluated `69 + 420` as `489` through this wrapper and SDL host.
- `calculator-emulator --help`, invalid CLI handling, missing-ROM handling,
  clean signal shutdown, install staging, and a 600-tick x86 benchmark passed.
  That sample achieved about 682.5 ticks/second (11.38x real time); it is only
  a host sanity measurement, not a T113 forecast.
- Debug and Release CTest runs pass. The historical test program uses
  `assert()` for setup as well as checks, so its target explicitly keeps
  assertions enabled under Release instead of allowing `NDEBUG` to remove
  required setup calls.
- A Release cross-build using Bootlin GCC 15.3.0 succeeded for CEmu, the
  standalone app, probes, LVGL, and `ce_lvgl_app` with desktop/SDL targets off.
- `file` and `readelf` confirmed the target app is ELF32 little-endian ARM,
  ARMv7 application profile, EABI5 hard-float, VFPv4/NEON, dynamically linked
  only to the target libc/loader.
- CEmu and LVGL submodule pins remained exactly `fb10bfea...` and
  `80ca777e...`, with clean submodule worktrees.

The 1,000,000-tick CEmu probe is intentionally a debug-state probe. It must not
be copied into a framebuffer host loop.

## Current Known-Good Functionality

- Both backends compile on the x86-64 development machine.
- The from-scratch focused unit suite passes.
- The from-scratch external-ROM probe reaches its documented strict stop with
  no unsupported opcode on that path.
- The pinned CEmu core builds headlessly without Qt or physical USB access.
- Explicit pre-A, I, and M revision values are routed to CEmu; revision I is
  the known-good ROM configuration.
- CEmu boots the private ROM into real TI-OS.
- Scheduler, HALT handling, LCD rendering, framebuffer transport, SDL display,
  screenshot output, and keypad events work together end to end.
- The portable LVGL application, SDL3 adapter, and bounded LVGL compositor path
  build and render the expected TI-OS framebuffer on x86-64.
- The standalone SDL-independent Linux process builds, installs, handles its
  CLI and termination signals, and benchmarks CEmu on x86-64.
- The standalone process and portable LVGL application layer cross-compile for
  Cortex-A7 as 32-bit ARM hard-float code.
- Pause/resume and backend-neutral debug-state access exist in the wrapper.

## Current Limitations and Risks

- The LVGL application is deliberately only the first TI framebuffer canvas;
  final application UI design has not started.
- The ARM binary has not run on T113 hardware; runtime correctness,
  performance, CPU/RAM use, thermals, and throttling remain untested.
- The standalone target is deliberately headless until an actual T113 Linux
  display and physical-keypad stack is selected and implemented.
- There is no T113 display/keypad adapter or final Buildroot package/image.
  Camera, networking, launcher, CAS, and AI belong to separate projects.
- Physical USB is intentionally stubbed in the headless CEmu build.
- CEmu probe/host tests and the benchmark require the private ROM and remain
  manual; CTest covers the from-scratch core and no-ROM CLI help.
- The public API has the unit, framebuffer-contract, error-reporting,
  single-instance, and CMake-coupling limitations listed above.
- The SDL host has no explicit real-time throttle, robust CLI diagnostics, or
  fully coherent keyboard map.
- The from-scratch backend is incomplete and follows a fatal TI boot path.
- The vendored CEmu checkout has an uninitialized zdis submodule, though it is
  not needed by the current target set.
- GPLv3 obligations must be accounted for in product distribution planning.

## x86-to-T113 Portability Plan

The intended port is a native C build for ARM Cortex-A7 Linux:

1. Keep CEmu and all application callers behind `ce/emulator.h`. **Done.**
2. Cross-compile the headless process, benchmark, and portable LVGL layer for
   Cortex-A7 hard-float. **Done with a generic toolchain.**
3. Rebuild with the final Buildroot-generated toolchain and deploy the binary
   plus the separately copied private ROM when hardware arrives.
4. Run the documented performance gate before optimizing or committing to UI
   budgets.
5. Select display/input adapters from actual board, panel, kernel, and keypad
   facts, keeping those types outside the emulator API.

Do not emulate x86 on ARM and do not fork a second emulator scheduler.

### T113 Performance Gate

As soon as hardware is available, measure a representative steady-state TI-OS
workload on one Cortex-A7 core:

- emulated seconds completed per wall-clock second (real time requires at least
  60 `CLOCK_RUN` ticks per wall-clock second);
- CPU utilization and whether one core can sustain emulation;
- resident and peak RAM;
- frame production/copy cost;
- thermals and throttling over a sustained run; and
- remaining headroom for LVGL, camera, networking, and system services.

Measure first. Optimize CEmu or the wrapper only in response to evidence.

## Development Rules

- CEmu is the primary TI backend; preserve the from-scratch backend.
- Never commit or redistribute the private ROM.
- Application code depends on the project emulator API, not CEmu internals.
- Reuse CEmu hardware models and scheduler; do not rebuild them in the wrapper.
- Keep SDL/LVGL/platform display and physical input outside emulator code.
- Preserve `ce_host` as the known-good minimal frontend.
- Keep this repository scoped to the standalone TI emulator process; launcher,
  home UI, CAS, AI, and camera work belong elsewhere.
- Treat `0xAARRGGBB` as ARGB8888 and validate actual RGB output.
- One CEmu host frame advances one `CLOCK_RUN` tick unless a deliberately
  designed scheduler policy says otherwise.
- HALT alone is not an emulator failure.
- Prefer end-to-end behavior and visual/input tests over weak proxy checks.
- Keep historical findings, but mark superseded conclusions clearly.
- Do not optimize for T113 before measuring on the hardware.

## High-Level History

1. A from-scratch portable CE/eZ80 emulator was developed by following the
   private ROM from reset and implementing CPU/MMIO behavior as encountered.
2. Substantial instruction, mixed-mode, stack, flash-protection, and interrupt
   work made the backend a valuable diagnostic reference, but full TI-OS boot
   remained expensive and uncertain.
3. The project pivoted to CEmu's mature GPLv3 core while retaining its own API
   and application architecture.
4. CEmu was pinned and built headlessly with project-owned revision and USB
   glue; the wrapper exposed execution, debug state, LCD, and keypad access.
5. The SDL3 host initially appeared black because of scheduler-unit,
   pixel-format, screenshot-channel, and screenshot-timing bugs.
6. Those host bugs were fixed without replacing CEmu LCD logic. The real TI-OS
   UI and an interactive calculation were then verified end to end.
7. LVGL v9.6.0 was vendored, and a portable canvas layer plus replaceable SDL3
   adapter established the first x86 LVGL application path. Bounded output and
   user desktop interaction were validated.
8. The repository role was fixed as the standalone emulator application. SDL
   became optional for headless builds, `calculator-emulator` added process and
   benchmark behavior, and the target plus portable LVGL layer cross-compiled
   successfully for Cortex-A7 hard-float.

## Exact Next Milestone

When the physical T113-S3 arrives, rebuild with its exact Buildroot toolchain,
inspect and install `calculator-emulator`, copy the private ROM separately,
run the 600- and 3,600-tick benchmarks, and record CPU/RSS/thermal/frequency
data plus clean signal shutdown. Do not optimize CEmu or implement target
display/keypad support until those measurements and hardware interfaces are
known. Follow `docs/ARM_T113_DEPLOYMENT.md` exactly.
