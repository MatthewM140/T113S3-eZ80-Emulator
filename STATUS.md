# Emulator Status

> Historical engineering log. For the reconciled current project checkpoint,
> read `PROJECT_STATE.md`. In particular, CEmu is now the primary backend and
> the opening gate below describes the preserved from-scratch backend, not the
> current product path.

## Historical From-Scratch Gate

Phase 1 and the initial boot-driven CPU slice are working on x86-64 with CMake,
CTest, and the private ROM supplied explicitly to `ce_boot_probe`.

The real ROM currently executes from reset through the later fatal HALT at 1742
modeled cycles. The trace
has validated:

- Z80 reset state with `PC=0` and `MBASE=0`
- `DI`, `RSMIX`, `LD E,E`, and the reset jump to `0x000E4F`
- `XOR A`, register loads, conditional jumps, and relative jumps
- `OUT0` writes through the observable machine I/O sink
- `POP DE` reading stack bytes from mapped flash
- 16-bit `SBC HL,BC` and its zero result
- `LD BC,(HL)` in both Z80-width and ADL-width data modes
- `OTIMR` memory-to-port repetition, register updates, flags, and cycle accounting
- The complete standard CB-prefixed rotate/shift, BIT, RES, and SET families
- Mixed-mode absolute stack and HL loads used during RAM initialization
- `IM 1`, unconditional and conditional relative/absolute jumps, `HALT`,
  `OR A`, `CCF`, `EX DE,HL`, `SBC HL,DE`, and related register loads
- IX-prefixed stack/arithmetic/indexed-memory forms, indexed CB operations,
  DAA, `CP D`, `LD A/B/C,(HL)`, `LD C,A`, `ADD HL,BC`, `DEC SP`, and eZ80
  `ED 0F LD (HL),BC`
- Strict probe termination at unsupported instructions and failed bus writes

The current strict stop is `CE_STEP_HALTED` on the step after `HALT` at
`0x001414`, with `PC=0x001415` and `cycles=1742`. No unsupported CPU opcode
is encountered on the executed path. The ROM reaches HALT after `DI`,
`LD A,0x10`, `OUT0 (0x00),A`, and two `NOP`s. At HALT, `IFF1=0`, `IFF2=0`,
and no `EI` has occurred after the last `DI`. This remains an intentional
boot fatal/NMI path, not a timer-wakeup path.

## MBASE Audit

The CPU state includes `MBASE`, and the trace now reports it for every step.
The private ROM has executed no `LD MB,A` (`ED 6D`) before the failure. Its
state immediately before the failing instruction is:

- `MBASE = 0x00`
- control/PC mode: Z80 short
- data mode: Z80 short

Therefore the documented Z80-mode formation is:

```text
(MBASE << 16) | 0x887C = 0x00887C
```

This is a genuine attempted flash write, not an address-formation error. The
emulator does not enable generic flash writes. Opcode fetches, immediate
operand reads, stack reads, and data accesses now all apply their respective
MBASE/ADL rules. Focused tests cover `MBASE=0`, `MBASE=0xD0`, ADL addressing,
mixed-mode short addressing, and the documented ADL-only `LD MB,A` behavior.

## Flash Protection Result

The ROM window around the blocker disassembles as:

```text
000ED4  LD HL,0x8C7C
000ED7  POP DE
000ED8  LD (0x887C),HL
000EDB  POP DE
000EDC  XOR A
000EDD  LD (0x77B7),A
000EE0  LD (0x77BB),A
000EE3  LD (0x0D),A
000EE7  OUT0 (0x0D),A
```

Immediately before the failing store:

```text
AF = 0xD142
BC = 0x00EBB8
DE = 0x0001E5
HL = 0x008C7C
SP = 0x00A882
MBASE = 0x00
PC/control mode = Z80 short
data mode = Z80 short
```

The executed path contains no writes to control ports `0x001D-0x001F`, so no
runtime privileged-range update has occurred. The machine initializes the boot
privileged end at `0x020000`, consistent with the documented boot-code range;
the failing PC is therefore privileged. WikiTI and the CEmu behavioral reference
indicate that privileged flash writes enter the flash-controller command path,
while unprivileged writes trigger NMI without mutating flash. The core now
records the privileged write and stops with `CE_STEP_FLASH_UNIMPLEMENTED`; it
does not mutate the ROM or invent a flash command response.

Earlier output writes reached ports `0x05`, `0x07`, `0x09`, `0x02`, `0x3A`,
`0x3B`, `0x3C`, and `0x0D`; none are the privilege-range ports.

## Revision Policy

The machine requires an explicit revision today. ROM metadata identification
returns unknown until a reliable, validated signature is implemented. The
emulator must never guess a hardware revision from an ambiguous ROM pattern.

## Validation Commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/ce_boot_probe --rom ./ti-84ce.rom --revision i --count 120
```

For this preserved backend, SDL, Buildroot, LVGL, and handheld integration
remain intentionally deferred. The project later added the separate CEmu SDL3
host documented below.

## Interrupt/HALT Pass

The first CE interrupt-controller bank is modeled at `0xF00000`:

- `0x5000`: raw/latching interrupt status
- `0x5004`: interrupt enable mask
- `0x5008`: interrupt acknowledge
- `0x500C`: latch configuration
- `0x5010`: inversion configuration
- `0x5014`: masked interrupt status

The model supports the documented 22 source bits, including ON, timers,
OS-timer, keypad, LCD, RTC, and USB as source IDs. Sources are deterministic
and injectable for tests; no host wall-clock time is used. CPU interrupt
acceptance supports IM 1, EI delayed enable, DI cancellation, HALT release,
and the current 24-bit stack width.

The executed ROM path does not write `0xF00000`-`0xF00017`, enable any source,
execute `EI`, or configure a hardware/OS timer. Its only final control write is
`OUT0 (0x00),0x10`; WikiTI documents port `0x0000` bit 4 as the NMI crash
behavior. Therefore no maskable interrupt or timer wakeup is modeled for this
boot path, and the probe correctly remains halted.

## Fatal-Path Backtrace

The first fatal branch was:

```text
00166F  CALL 000C2E
000C2E  ED 38 28       IN0 A,(0x28)
000C31  CB 5F          BIT 3,A
000C33  CA 0C 14 00    JP Z,0x00140C
```

Port `0x28` is documented flash-protection status. The ROM first wrote `0x04`
to port `0x06`, wrote `0x04` to port `0x28`, and executed the documented unlock
sequence ending in `CB 57`. The core returned only status bit 2 (`0x04`) and
omitted unlock bit 3, so `BIT 3,A` set Z and selected the crash routine. This
was the first state divergence from documented hardware behavior.

The core now models port `0x06` bit 2, port `0x28` bit 2, and the privileged
unlock-sequence endpoint, so the status read returns `0x0C`. The ROM bypasses
the first fatal branch and executes deeper RAM/flash validation. It eventually
reaches the same deliberate fatal routine through `0x00169B JP 0x00140C`.

Audited input reads on this path are `IN0 (0x0D)` for the nibble-mirrored
control port and `IN0 (0x28)` for flash status. Port `0x0D` uses the existing
mirrored readback; port `0x28` was the former placeholder/default and is now
modeled. No interrupt-controller or timer registers are accessed before the
fatal path.

The later fatal selector is:

```text
001A0F  JR Z,0x001A23
001A11  SCF
001A12  RET
000C3B  RET C
001673  ...
00169B  JP 0x00140C
```

The RAM/stack validation helper returns with `Z=0,C=1` (`AF=0xFF01`), so
`RET C` selects the crash path. The first address-formation divergence found
in this helper was HL-indirect access after RSMIX: base `(HL)` operations in
long control mode were incorrectly forced through short `MBASE` data mapping.
HL-indirect accesses now use long physical addressing when control or data ADL
is active, while the established ED/CB/IND data-mode contracts remain intact.
This changes the path and passes all focused tests, but the stack validation
still returns carry; further progress would require resolving that helper's
remaining indexed-stack behavior rather than guessing a success value.

The indexed trace verified `IX=0xD1A869`, `SP=0xD1A856`, and
`DD CB ED 86` targeting `0xD1A856`, followed by the expected indexed stores in
the same 24-bit RAM stack region. The remaining comparison is `HL=0x005BFC`
against `BC=0x00000C`; `SBC HL,BC` produces no carry, so `JR NC` enters the
failure marker. No incorrect indexed effective address or stack-width mismatch
was found. The expected reason for the ROM to find a different scan result is
not established safely from the available documentation and private image.

## Larger Boot Pass

The flash/protection probe is now modeled conservatively. Privileged writes to
locked parallel flash are ignored without mutating the ROM; unprivileged writes
request NMI; unlocked flash command protocol remains a separate future state
machine. The ROM's first flash probe now completes without falsely treating
`0x00887C` as a normal RAM write.

The eZ80 mixed-mode model was corrected: after `RSMIX`, the ROM uses a long
instruction stream with short data accesses and a 24-bit stack. This made
`LD SP,0xD1A87E`, `LD (0xD1887C),HL`, calls, returns, and RST stack frames map
to the documented CE RAM addresses.

Additional implemented instructions/MMIO:

- `LD (BC),A`, `LD B,A`, `LD B/C/H,n`, `INC A`, `INC C`, `DJNZ`
- `CALL`, `RET`, `PUSH`/`POP` register pairs, and `RST`
- `IND` and `IN0 A,(n)`
- CE control-port `0x0D` nibble-mirrored readback
- Mixed-mode 24-bit stack and instruction operand handling
- RAM-priority physical memory decoding

Current strict result:

- Before RET at `0x0006DD`, `SP=0xD1A878`
- RET stack bytes: `F9 15 00`
- Verified RET destination: `0x0015F9` in flash
- Mode: long control/instruction stream, short data mode, `MBASE=0`
- Modeled cycles after RET: `101`
- `ADC A,L` (`0x8D`) is implemented and passes focused flag tests
- Complete CB family is implemented and passes focused register/memory/flag/
	cycle tests
- `CB D7` at `0x00162B` executes successfully
- The first fatal branch at `0x000C33` is now bypassed after fixing flash
    protection status; the later branch at `0x00169B` still selects `0x00140C`
- The ROM reaches the same `HALT` at `0x001414`; the completed instruction
     leaves `PC=0x001415` and `cycles=1742`
- Next blocker: determine the later flash/RAM validation failure at
    `0x00169B` without inventing a wake source

The previous RAM-initialization conclusion was based on a stale RET interpretation.
The verified execution path remains in flash, initializes RAM through absolute
long-mode accesses, and reaches the boot idle HALT without an unsupported CPU
opcode.

## Opcode Coverage

`ce_boot_probe` now prints executed opcode coverage grouped as base, CB
sub-opcode, ED sub-opcode, and unsupported opcode values. The latest run
reported no unsupported opcodes. Executed CB sub-opcodes were `57`, `5F`, and
`D7`; executed ED sub-opcodes included `38`, `39`, `42`, `52`, `56`, `73`,
`79`, and `7E`.

## Return-Frame Provenance

The long-mode `RET` pops three bytes from `0xD1A878`: `F9 15 00`, producing
`PC=0x0015F9`. At `0x00D177`, the ROM byte `0x8D` is the low byte of the
operand for `CALL 0x001C8D` beginning at `0x00D176`; it is not an execution
boundary.

Fetches at low `0x00D177` are explicitly routed through flash and are covered by a regression test.
The RAM backing array is irrelevant for low flash fetches.

## Backend Preservation Checkpoint

At that historical checkpoint the repository directory was not a Git working
tree, so no commit, branch, or tag could be created. It has since been
initialized as the root Git repository. The existing from-scratch backend remains in `core/` with its
original CMake targets and private-ROM probe; the ROM file is not being copied
into any new backend or source distribution.

The from-scratch backend currently builds cleanly and its CTest suite passes.
Its complete architecture, test commands, limitations, and boot discoveries
are documented in `docs/FROM_SCRATCH_EMULATOR.md`.

## CEmu Migration

A separate CEmu reference checkout was used outside this project tree at
commit `fb10bfe` (`/tmp/cemu-reference`) for differential diagnostics. Its
headless core trace matched the reset prefix through the early boot sequence.
An earlier differential at `0x000E54` showed the from-scratch core consuming a
three-byte `LD BC,nn` operand and reaching `0x000E58`, while CEmu correctly
reached `0x000E57`. That finding is now superseded: the current implementation
and focused regression test use the 16-bit operand and the ROM probe reaches
`0x000E57`.

The CEmu-derived backend plan, source commit, required modules, wrapper design,
and current headless boot status are tracked in `docs/CEMU_BACKEND.md`.

The first CEmu-backed headless probe now boots the same private ROM with
explicit revision I. At a 1,000,000-tick probe window it reaches a halted CPU
state around `PC=0x08575D`, `SP=0xD1A854`, with `ADL=1`, `MADL=0`, `MBASE=0xD0`,
`IM=2`, and approximately 4,000,000 CEmu cycles. The custom wrapper exposes
ROM loading, reset, run, pause/resume, debug state, framebuffer rendering, and
keypad events. Qt, SDL, LVGL, USB hardware, and physical-device integration
remain excluded.

The SDL3 host is `ce_host`:

```sh
./build/ce_host --rom ./ti-84ce.rom --revision i
```

It uses CEmu's existing `emu_run()` scheduler/event loop, displays the
320x240 native-CEmu-format (`ARGB8888`) framebuffer with nearest-neighbor
integer scaling, and maps temporary PC keyboard controls through the public
keypad API. `--scale N` selects the initial integer window scale.

A dummy-video bounded run (`--frames 600`, i.e. 10 emulated seconds) now
produces a screenshot showing the real TI-OS boot screen ("TI-84 Plus CE", OS
version, "RAM Cleared"), verified rigorously with `identify -verbose`
(multiple unique colors, nonzero mean, max channel value 255) and confirmed to
change across different `--frames` counts. An earlier status here claimed this
was already working based on an invalid "nonzero bytes in the file" check,
which also matched the PPM's ASCII header; that check was wrong, and the real
root cause (the SDL host requesting ~4.6 emulated *hours* of CEmu time per
video frame via a misunderstood tick unit, plus an `ARGB8888`/`RGBA8888`
pixel-format mismatch, plus a PPM channel-extraction bug, plus a screenshot
taken on the wrong frame) is fixed and detailed in `docs/CEMU_BACKEND.md`. Per
correct upstream pacing (CEmu's own Qt frontend advances 1 tick per loop
iteration), the SDL host now advances exactly 1 `CLOCK_RUN` tick (1/60
emulated second) per video frame.

The mapping is documented in `docs/CEMU_BACKEND.md` and covers navigation, numeric,
arithmetic, parentheses, 2nd, Alpha, Mode, Graph, Trace, Zoom, Window, Y=,
Stat, Math, Apps, Prgm, and Vars.

## LVGL x86 Development Frontend

The first LVGL milestone was implemented on 2026-09-21 without changing the
known-good `host/ce_host.c` or CEmu hardware emulation.

LVGL v9.6.0, commit `80ca777e37a2b176770726a02e07a6fb79ef0b39`,
is vendored unmodified under `third_party/lvgl/` and built from source with the
project's `config/lv_conf.h`. LVGL is MIT licensed. The pinned stable release's
built-in SDL driver uses SDL2, so it is disabled; the project supplies a thin
SDL3 LVGL display/input adapter instead of bringing two SDL major versions into
one process.

The new path is:

```text
CEmu -> ce/emulator.h -> ce_lvgl_app -> LVGL canvas/software renderer
     -> ce_lvgl_sdl3 -> SDL3 desktop window and keyboard events
```

`ce_lvgl_app` directly wraps the emulator-owned 320x240 `0xAARRGGBB` buffer as
an LVGL ARGB8888 canvas on the little-endian x86 host. LVGL composites that
canvas into a separate full-frame XRGB8888 buffer. The SDL3 adapter uploads
the composited buffer to an XRGB8888 streaming texture with nearest-neighbor
integer scaling. Keyboard press/release events are translated to calculator
matrix positions and forwarded only through `ce_emulator_key`; `X` now maps to
the TI `X,T,theta,n` key and Backslash maps to `Sto->`, avoiding the reference
host's duplicate-X defect.

The frontend loop uses a real-time accumulator at 60 CEmu scheduler ticks per
second. Every call advances exactly one tick with
`ce_emulator_run(emulator, 1U)`, while SDL events and `lv_timer_handler()` keep
running in the same loop. It does not recreate CEmu scheduling.

Validation completed:

- CEmu/LVGL Debug build passed; `ce_host`, `ce_lvgl_host`, both probes, and all
  libraries built.
- CTest passed 1/1 with CEmu/LVGL enabled.
- A fresh `CE_ENABLE_CEMU=OFF` build and its CTest passed, preserving the
  from-scratch targets.
- `ce_cemu_boot_probe --ticks 600` completed through the existing API.
- Bounded dummy-video runs of both `ce_host` and `ce_lvgl_host` completed.
- The LVGL-composited PPM was 320x240 with five colors and visually showed the
  TI-84 Plus CE OS 5.3.0.0037 `RAM Cleared` screen.
- The reference and LVGL captures were byte-identical, confirming that the
  LVGL path preserved orientation and RGB channel interpretation for that
  frame.

The user subsequently confirmed the desktop window, live updates, keyboard
input, and TI-OS calculations. See `docs/LVGL_FRONTEND.md` for the architecture,
controls, commands, and limitations.

## Standalone Application and ARMv7-A Readiness

The repository role is now explicitly the standalone TI emulator application
for the larger calculator system. The launcher/home UI, CAS, AI, camera, and
networking applications are separate future projects.

The new stable executable is `calculator-emulator`. It links only the
CEmu-backed project API and target libc, not SDL. Its CLI requires an external
`--rom PATH`, accepts `--revision pre-a|i|m`, provides `--help`, diagnoses
invalid options and ROM failures, and never embeds the private ROM. Normal
operation is honestly headless until a target display/input adapter exists. It
runs at a wall-clock-paced 60 scheduler ticks per second, handles `SIGINT` and
`SIGTERM`, and exits cleanly.

`--benchmark-ticks N` runs N separate `ce_emulator_run(emulator, 1U)` calls
without wall-clock throttling and reports elapsed monotonic time, scheduler
ticks/second, emulated seconds, and real-time factor. A 600-tick x86 sanity run
completed in approximately 0.879 seconds, about 682.5 ticks/second or 11.38x
real time. This is not a T113 prediction.

Build coupling was corrected with `CE_ENABLE_DESKTOP_FRONTENDS`. SDL3 and
`pkg-config` are now discovered only for `ce_host` and the SDL-backed LVGL
frontend. Headless CEmu, the standalone process, and cross-builds no longer
require desktop SDL. The portable LVGL application layer can still be built
without the SDL adapter. `cmake --install` installs the stable executable under
the selected binary prefix.

A Release CTest run exposed that the preserved from-scratch test harness used
`assert()` for setup calls, which `NDEBUG` removed. The test target now keeps
assertions active in Release builds; no emulator behavior was changed, and
both Debug and Release CTest runs pass.

The configurable toolchain file
`cmake/toolchains/armv7a-linux-gnueabihf.cmake` targets Cortex-A7 with
`-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard`. Validation used the
checksum-verified Bootlin 2026.08 ARMv7-EABI hard-float toolchain with GCC
15.3.0. A Release build succeeded for the from-scratch core/probe, CEmu core
and probe, `calculator-emulator`, LVGL, and `ce_lvgl_app`; desktop frontends
were disabled.

The produced `calculator-emulator` was verified as:

```text
ELF 32-bit LSB pie executable, ARM, EABI5, hard-float ABI
interpreter: /lib/ld-linux-armhf.so.3
```

`readelf -A` reported ARMv7 application profile, VFPv4, NEON, and VFP-register
arguments. The only dynamic runtime requirements were the target libc and
hard-float loader. This verifies compilation and binary ABI, not execution on
the physical T113-S3.

The root is now a Git repository, CEmu and LVGL are proper clean submodules at
their recorded commits, and root ROM files remain ignored. Full reproducible
commands, Buildroot integration guidance, deployment paths, benchmark
procedure, and the exact first-board checklist are in
`docs/ARM_T113_DEPLOYMENT.md`.
