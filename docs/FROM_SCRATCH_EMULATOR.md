# From-Scratch Emulator

## Purpose

This is the original portable C TI-84 Plus CE emulator core. It executes the
private raw ROM directly and follows strict boot-driven development: execute
until the first unsupported or incorrect behavior, implement that behavior,
test it, and continue.

## Architecture

- `core/src/rom.c`: fixed-size 4 MiB raw flash loading and conservative revision policy.
- `core/src/machine.c`: CPU, address formation, flash/RAM/VRAM decoding, MMIO sink,
  interrupt-controller model, and framebuffer storage.
- `core/include/ce/machine.h` and `rom.h`: portable public core types.
- `core/tests/test_rom.c`: focused instruction, address, protection, and interrupt tests.
- `host/ce_boot_probe.c`: strict private-ROM execution probe and opcode coverage report.

The current CMake targets are `ce_core`, `ce_rom_tests`, and `ce_boot_probe`.
The ROM is always supplied externally with `--rom`; it is not embedded or
redistributed.

## Implemented CPU Scope

The decoder includes the boot-required base forms: reset/control flow, loads,
calls/returns, stack pairs, RST, conditional branches, arithmetic/compare
forms, `OR A`, `AND n`, `SCF`, `CCF`, `DAA`, `HALT`, and selected register and
memory forms.

The complete standard CB register/(HL) family is implemented: RLC, RRC, RL,
RR, SLA, SRA, SRL, BIT, RES, and SET. The boot-required DD/IX subset includes
stack operations, IX arithmetic, indexed stores, and DD CB displacement forms.
Selected ED forms include RSMIX, OUT0/IN0, SBC/ADC-related forms, IM 1,
absolute stack transfers, ED 0F, and block I/O used by the ROM.

## Addressing and Stack Work

The CPU tracks control/instruction ADL, data ADL, MBASE, and 24-bit stack
behavior. After RSMIX the private ROM uses long control/instruction execution
with short ordinary data accesses and a 24-bit stack. The implementation has
focused tests for MBASE formation, mixed-mode stack frames, flash fetches,
RAM-priority decoding, and address-width behavior.

## Flash, Protection, and MMIO

- Locked privileged flash writes are recorded and ignored rather than mutating
  the ROM; unprivileged flash writes request NMI.
- Port `0x0D` has nibble-mirrored readback.
- The documented port `0x06`/`0x28` flash-unlock status path is modeled,
  including the unlock-sequence endpoint used by this ROM.
- The first interrupt-controller bank at `0xF00000` models raw/latch/invert,
  enable, acknowledge, and masked status registers with deterministic test
  source injection.
- CPU interrupt delivery supports IM 1, EI delay, DI, HALT release, and
  24-bit interrupt stack frames.
- No timer or other interrupt source is connected to this boot path because
  the ROM never configures or enables one before its deliberate fatal HALT.

## Current Boot Progress

The ROM reaches the first flash-protection check, bypasses the original fatal
branch after the port `0x28` fix, and proceeds through RAM/stack validation.
The later validation helper returns `AF=0xFF01` (`Z=0`, `C=1`), so `RET C` at
`0x000C3B` selects the later fatal path and eventually reaches:

```text
00140C  DI
00140D  LD A,0x10
00140F  OUT0 (0x00),A
001412  NOP
001413  NOP
001414  HALT
```

Current last trustworthy state is `PC=0x001415` after 1742 modeled cycles.
No unsupported opcode is encountered on the current path.

The indexed-stack helper was audited. With `IX=0xD1A869` and `SP=0xD1A856`,
`DD CB ED 86` targets `0xD1A856`, and subsequent indexed stores remain in the
same 24-bit RAM stack region. No indexed effective-address, IX preservation,
or stack-width mismatch was found.

## Differential Discovery (Resolved)

A separate CEmu headless trace was generated from the same ROM using CEmu
commit `fb10bfe` and an explicit ASIC revision-I reset callback. It logs PC,
opcode, registers, ADL/mixed mode, interrupt state, and cycles at the internal
CPU fetch boundary.

The first sequence divergence originally found at `0x000E54` was operand
width: CEmu treated `LD BC,nn` as a 16-bit operand and reached `0x000E57`, while
this backend consumed three operand bytes and reached `0x000E58`. That issue is
resolved in the current source. The private-ROM probe now reaches `0x000E57`,
and `test_ld_bc_immediate_is_16_bit` protects the correction.

## Build and Run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/ce_boot_probe --rom ./ti-84ce.rom --revision i --count 120
```

## Limitations

The from-scratch backend is not a complete CE hardware implementation. It does
not yet provide a production LCD/keypad/timer/USB/DMA/RTC frontend, complete
revision behavior, flash command state machines, or a complete eZ80 decoder.
SDL, LVGL, Buildroot, and T113 integration remain intentionally deferred.
