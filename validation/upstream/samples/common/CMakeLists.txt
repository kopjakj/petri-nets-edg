# Shared "block library" for all Event-Driven-Gates samples.
#
# This is add_subdirectory()'d by each sample AFTER pico_sdk_init() and the
# FreeRTOS kernel import, so the pico_stdlib and FreeRTOS-Kernel targets exist.
add_library(edg STATIC ${CMAKE_CURRENT_LIST_DIR}/edg.c)

target_include_directories(edg PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(edg PUBLIC
    pico_stdlib
    FreeRTOS-Kernel-Heap4
)
