cmake_minimum_required(VERSION 3.20)

project(ce_emulator VERSION 0.1.0 LANGUAGES C)

include(CTest)
include(GNUInstallDirs)

option(CE_ENABLE_CEMU "Build the CEmu-derived backend" ON)
option(CE_ENABLE_LVGL "Build the portable LVGL application layer" ON)
option(CE_ENABLE_DESKTOP_FRONTENDS
       "Build SDL3 x86 development frontends" ON)
option(CE_BUILD_TARGET_APP
       "Build the standalone calculator-emulator Linux application" ON)

add_library(ce_core STATIC
    core/src/machine.c
    core/src/rom.c
)

target_compile_features(ce_core PUBLIC c_std_11)
target_include_directories(ce_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core/include
)

target_compile_options(ce_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
)

if(BUILD_TESTING)
    add_executable(ce_rom_tests
        core/tests/test_rom.c
    )
    target_link_libraries(ce_rom_tests PRIVATE ce_core)
    target_compile_features(ce_rom_tests PRIVATE c_std_11)
    # This historical test harness uses assert() for both setup calls and
    # checks, so keep assertions active even in Release configurations.
    target_compile_options(ce_rom_tests PRIVATE -UNDEBUG)
    add_test(NAME ce_rom_tests COMMAND ce_rom_tests)
endif()

add_executable(ce_boot_probe
    host/ce_boot_probe.c
)
target_link_libraries(ce_boot_probe PRIVATE ce_core)
target_compile_features(ce_boot_probe PRIVATE c_std_11)

if(CE_ENABLE_CEMU)
    file(GLOB CEMU_CORE_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cemu/core/*.c
    )
    list(APPEND CEMU_CORE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cemu/core/os/os-linux.c
    )
    list(FILTER CEMU_CORE_SOURCES EXCLUDE REGEX "/usb/")
    list(FILTER CEMU_CORE_SOURCES EXCLUDE REGEX "/os/os-(emscripten|win32)\\.c$")

    add_library(ce_cemu_core STATIC
        ${CEMU_CORE_SOURCES}
        third_party/cemu_headless.c
        core/src/cemu_backend.c
    )
    target_compile_features(ce_cemu_core PUBLIC c_std_11)
    target_compile_definitions(ce_cemu_core PRIVATE CEMU_BYTE_ORDER=1 CEMU_BITFIELD_ORDER=1)
    target_include_directories(ce_cemu_core
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cemu
    )
    target_compile_options(ce_cemu_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_libraries(ce_cemu_core PRIVATE ce_core)

    add_executable(ce_cemu_boot_probe host/cemu_boot_probe.c)
    target_link_libraries(ce_cemu_boot_probe PRIVATE ce_cemu_core)
    target_compile_features(ce_cemu_boot_probe PRIVATE c_std_11)
    target_include_directories(ce_cemu_boot_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/include)

    if(CE_BUILD_TARGET_APP)
        add_executable(calculator-emulator app/calculator_emulator.c)
        target_link_libraries(calculator-emulator PRIVATE ce_cemu_core)
        target_compile_features(calculator-emulator PRIVATE c_std_11)
        target_compile_options(calculator-emulator PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
        target_include_directories(calculator-emulator PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/core/include)
        install(TARGETS calculator-emulator
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        if(BUILD_TESTING)
            add_test(NAME calculator_emulator_help
                COMMAND calculator-emulator --help)
        endif()
    endif()

    if(CE_ENABLE_DESKTOP_FRONTENDS)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)

        add_executable(ce_host host/ce_host.c)
        target_link_libraries(ce_host PRIVATE ce_cemu_core PkgConfig::SDL3)
        target_compile_features(ce_host PRIVATE c_std_11)
        target_include_directories(ce_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/include)
    endif()

    if(CE_ENABLE_LVGL)
        # LVGL is pinned under third_party/lvgl. Keep its desktop backend out
        # of the portable application layer: this project supplies a small
        # SDL3 display/input adapter which can later be replaced on the T113.
        set(LV_BUILD_CONF_PATH
            ${CMAKE_CURRENT_SOURCE_DIR}/config/lv_conf.h
            CACHE FILEPATH "LVGL configuration used by ce_lvgl_host" FORCE)
        set(LV_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
        set(LV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(LV_BUILD_SET_CONFIG_OPTS OFF CACHE BOOL "" FORCE)
        set(LV_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
        set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
        set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
        set(CONFIG_LV_USE_SDL OFF CACHE BOOL "" FORCE)
        add_subdirectory(third_party/lvgl EXCLUDE_FROM_ALL)

        add_library(ce_lvgl_app STATIC app/ce_lvgl_app.c)
        target_compile_features(ce_lvgl_app PUBLIC c_std_11)
        target_compile_options(ce_lvgl_app PRIVATE -Wall -Wextra -Wpedantic)
        target_include_directories(ce_lvgl_app
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/app
                ${CMAKE_CURRENT_SOURCE_DIR}/core/include)
        target_link_libraries(ce_lvgl_app PUBLIC lvgl::lvgl PRIVATE ce_cemu_core)
    endif()

    if(CE_ENABLE_DESKTOP_FRONTENDS AND CE_ENABLE_LVGL)
        add_library(ce_lvgl_sdl3 STATIC platform/sdl3/ce_lvgl_sdl3.c)
        target_compile_features(ce_lvgl_sdl3 PUBLIC c_std_11)
        target_compile_options(ce_lvgl_sdl3 PRIVATE -Wall -Wextra -Wpedantic)
        target_include_directories(ce_lvgl_sdl3
            PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/platform/sdl3)
        target_link_libraries(ce_lvgl_sdl3 PUBLIC lvgl::lvgl PRIVATE PkgConfig::SDL3)

        add_executable(ce_lvgl_host host/ce_lvgl_host.c)
        target_compile_features(ce_lvgl_host PRIVATE c_std_11)
        target_compile_options(ce_lvgl_host PRIVATE -Wall -Wextra -Wpedantic)
        target_include_directories(ce_lvgl_host PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/core/include)
        target_link_libraries(ce_lvgl_host PRIVATE
            ce_cemu_core ce_lvgl_app ce_lvgl_sdl3)
    endif()
endif()
