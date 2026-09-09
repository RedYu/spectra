# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(spectra)

# Keep the LVGL TLSF pool in PSRAM without modifying managed sources.
idf_component_get_property(spectra_lvgl_target lvgl__lvgl COMPONENT_LIB)
target_sources(${spectra_lvgl_target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/components/lvgl_port/lvgl_psram_pool.c"
)
target_include_directories(${spectra_lvgl_target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/components/lvgl_port"
)
target_compile_definitions(${spectra_lvgl_target} PRIVATE
    LV_MEM_POOL_INCLUDE="lvgl_psram_pool.h"
    LV_MEM_POOL_ALLOC=lvgl_psram_pool_get
)
