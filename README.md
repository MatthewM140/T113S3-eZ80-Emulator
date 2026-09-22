# Calculator Emulator

This repository is the standalone TI emulator application for a larger Linux
calculator system. It is not the launcher, home screen, CAS application, or
AI/camera application. Those are separate future projects which may start and
manage this process.

The production emulator path uses the pinned CEmu core behind the
project-owned `ce/emulator.h` API. The original from-scratch emulator is
preserved for tests and historical reference. `ce_host` and `ce_lvgl_host`
remain x86 desktop development tools.

## Native x86 Development Build

```sh
git submodule update --init --recursive
cmake -S . -B build -DCE_ENABLE_CEMU=ON -DCE_ENABLE_LVGL=ON \
  -DCE_ENABLE_DESKTOP_FRONTENDS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Launch the manually validated LVGL desktop frontend:

```sh
./build/ce_lvgl_host --rom /path/to/ti84ce.rom --revision i --scale 2
```

Run the standalone application's portable emulator benchmark:

```sh
./build/calculator-emulator --rom /path/to/ti84ce.rom \
  --revision i --benchmark-ticks 3600
```

The ROM is private external runtime data. It is never embedded in the binary,
must not be committed or redistributed, and is always selected with `--rom`.

See [docs/ARM_T113_DEPLOYMENT.md](docs/ARM_T113_DEPLOYMENT.md) for the verified
ARMv7-A hard-float cross-build, installation, Buildroot guidance, first-board
benchmark procedure, and current display/input limitations.

