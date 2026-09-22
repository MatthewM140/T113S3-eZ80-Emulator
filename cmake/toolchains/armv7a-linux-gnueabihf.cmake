# Generic 32-bit ARM Linux hard-float toolchain for Cortex-A7/T113-S3.
#
# Override CE_ARM_TOOLCHAIN_PREFIX for a Bootlin, distribution, or Buildroot
# toolchain. The value is the compiler command prefix without the trailing
# "-gcc", and may include a path:
#
#   -DCE_ARM_TOOLCHAIN_PREFIX=/opt/toolchain/bin/arm-buildroot-linux-gnueabihf
#
# A Buildroot-generated toolchain normally supplies its own sysroot through
# the compiler driver. CE_ARM_SYSROOT is available for toolchains that do not.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7-a)

set(CE_ARM_TOOLCHAIN_PREFIX "arm-linux-gnueabihf" CACHE STRING
    "ARM Linux compiler prefix, without the trailing -gcc")
set(CE_ARM_SYSROOT "" CACHE PATH
    "Optional ARM Linux sysroot (normally inferred by the compiler)")

set(CMAKE_C_COMPILER "${CE_ARM_TOOLCHAIN_PREFIX}-gcc")
set(CMAKE_CXX_COMPILER "${CE_ARM_TOOLCHAIN_PREFIX}-g++")
set(CMAKE_ASM_COMPILER "${CE_ARM_TOOLCHAIN_PREFIX}-gcc")

set(CE_CORTEX_A7_FLAGS
    "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard" CACHE STRING
    "Compiler architecture flags for the T113-S3 Cortex-A7")
set(CMAKE_C_FLAGS_INIT "${CE_CORTEX_A7_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${CE_CORTEX_A7_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${CE_CORTEX_A7_FLAGS}")

if(CE_ARM_SYSROOT)
    set(CMAKE_SYSROOT "${CE_ARM_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH "${CE_ARM_SYSROOT}")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
