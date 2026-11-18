cmake_minimum_required(VERSION 3.13)

# --- Default SDK / FreeRTOS-Kernel locations -------------------------------
# By default the build expects a "pico/" directory NEXT TO this repository:
#
#     <workspace>/
#       event-driven-gates/   (this repo)
#       pico/
#         pico-sdk/
#         FreeRTOS-Kernel/     (the Raspberry Pi fork: RP2040 + RP2350 ports)
#
# i.e. ../pico/pico-sdk and ../pico/FreeRTOS-Kernel relative to the repo root.
# Override any time with an environment variable or -DPICO_SDK_PATH=... etc.
# (An env var or -D value always wins over these defaults.)
get_filename_component(EDG_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT DEFINED ENV{PICO_SDK_PATH} AND NOT PICO_SDK_PATH)
    foreach(_cand "${EDG_REPO_ROOT}/../pico/pico-sdk" "${EDG_REPO_ROOT}/pico/pico-sdk")
        if(EXISTS "${_cand}")
            set(PICO_SDK_PATH "${_cand}" CACHE PATH "Path to the Raspberry Pi Pico SDK")
            break()
        endif()
    endforeach()
endif()
if(NOT DEFINED ENV{FREERTOS_KERNEL_PATH} AND NOT FREERTOS_KERNEL_PATH)
    foreach(_cand "${EDG_REPO_ROOT}/../pico/FreeRTOS-Kernel" "${EDG_REPO_ROOT}/pico/FreeRTOS-Kernel")
        if(EXISTS "${_cand}")
            set(FREERTOS_KERNEL_PATH "${_cand}" CACHE PATH "Path to the FreeRTOS-Kernel")
            break()
        endif()
    endforeach()
endif()

# --- picotool: a BUILD-TIME tool, nothing you install -------------------------
# The Pico SDK needs picotool to turn the .elf into the .uf2 that Wokwi loads and
# that the BOOTSEL drive accepts (with PICO_NO_PICOTOOL=1 you get no .uf2 at all).
# If it is not already installed, the SDK clones and compiles it. Point every
# sample and every target at ONE shared directory so that happens exactly once
# instead of once per build directory. Override with -DPICOTOOL_FETCH_FROM_GIT_PATH
# or the environment variable of the same name; an installed picotool wins over both.
if(NOT DEFINED ENV{PICOTOOL_FETCH_FROM_GIT_PATH} AND NOT PICOTOOL_FETCH_FROM_GIT_PATH)
    foreach(_cand "${EDG_REPO_ROOT}/../pico" "${EDG_REPO_ROOT}/pico")
        if(EXISTS "${_cand}")
            set(PICOTOOL_FETCH_FROM_GIT_PATH "${_cand}/picotool"
                CACHE PATH "Shared picotool source/build directory")
            break()
        endif()
    endforeach()
endif()

# --- Which target are we building for? ---------------------------------------
#
#   -DEDG_TARGET=sim   Raspberry Pi Pico, RP2040        <- the Wokwi simulator
#   -DEDG_TARGET=hw    Raspberry Pi Pico 2 W, RP2350    <- real hardware (Arm M33)
#
# Wokwi emulates the RP2040 only, so every diagram.json here uses the
# `wokwi-pi-pico` board part and every wokwi.toml points at build-sim/.
# Keep the two builds in separate directories; see ../BUILDING.md.
#
# Both must be set before project() / pico_sdk_init().
set(EDG_TARGET sim CACHE STRING "sim = RP2040 for Wokwi, hw = RP2350 Pico 2 W")
set_property(CACHE EDG_TARGET PROPERTY STRINGS sim hw)

if(EDG_TARGET STREQUAL "sim")
    set(_edg_platform rp2040)
    set(_edg_board    pico)
elseif(EDG_TARGET STREQUAL "hw")
    set(_edg_platform rp2350-arm-s)
    set(_edg_board    pico2_w)
else()
    message(FATAL_ERROR "EDG_TARGET must be 'sim' or 'hw', got '${EDG_TARGET}'")
endif()

# An explicit -DPICO_PLATFORM / -DPICO_BOARD still wins (the RISC-V appendix in
# ../BUILDING.md relies on this). PICO_PLATFORM is also already in the cache on
# a re-configure, which is what makes the mismatch warning below worth having:
# EDG_TARGET cannot retarget an existing build directory -- delete it instead.
if(NOT PICO_PLATFORM)
    set(PICO_PLATFORM ${_edg_platform})
endif()
if(NOT PICO_BOARD)
    set(PICO_BOARD ${_edg_board})
endif()
if(NOT PICO_PLATFORM STREQUAL _edg_platform)
    message(WARNING "PICO_PLATFORM=${PICO_PLATFORM} overrides the EDG_TARGET=${EDG_TARGET} "
                    "default of ${_edg_platform}. If that is not deliberate, delete this "
                    "build directory and re-run cmake.")
endif()
message(STATUS "EDG_TARGET=${EDG_TARGET} -> PICO_PLATFORM=${PICO_PLATFORM} PICO_BOARD=${PICO_BOARD}")

# --- Pull in the Raspberry Pi Pico SDK (needs PICO_SDK_PATH). ---
include(${CMAKE_CURRENT_LIST_DIR}/../common/pico_sdk_import.cmake)

project(edg02_batch_mixer C CXX ASM)

pico_sdk_init()

# --- Pull in the FreeRTOS kernel (the port matching PICO_PLATFORM: RP2040 or
#     RP2350_ARM_NTZ). Needs FREERTOS_KERNEL_PATH,
#     or a FreeRTOS-Kernel sibling of PICO_SDK_PATH. Must come after
#     pico_sdk_init() so PICO_PLATFORM is known, and the config dir must be set
#     before the kernel is added so its headers are found. ---
set(FREERTOS_CONFIG_FILE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../common CACHE STRING "")
include(${CMAKE_CURRENT_LIST_DIR}/../common/FreeRTOS_Kernel_import.cmake)

# --- Shared event-driven-gates block library. ---
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common ${CMAKE_BINARY_DIR}/common)

# --- Petri-net layer built on the block library (../../petri). ---
add_library(edg_pn STATIC ${EDG_REPO_ROOT}/petri/edg_pn.c)
target_include_directories(edg_pn PUBLIC ${EDG_REPO_ROOT}/petri)
target_link_libraries(edg_pn PUBLIC edg)

add_executable(${PROJECT_NAME} main.c)

target_link_libraries(${PROJECT_NAME}
    edg_pn
    edg
    pico_stdlib
    FreeRTOS-Kernel-Heap4
)

# Serial output on UART0 (GP0=TX, GP1=RX) at 115200 baud, on both targets.
# USB-CDC stdio is intentionally OFF: TinyUSB is not serviced under FreeRTOS in
# the simulator, where a USB printf would block the tasks. Wokwi's serial monitor
# is wired to GP0/GP1 in diagram.json.
pico_enable_stdio_usb(${PROJECT_NAME} 0)
pico_enable_stdio_uart(${PROJECT_NAME} 1)

# .uf2 / .hex / .elf / .bin / map + disassembly
pico_add_extra_outputs(${PROJECT_NAME})
