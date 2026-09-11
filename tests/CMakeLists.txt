cmake_minimum_required(VERSION 3.16)

set(SPECTRA_TEST_BUILD ON)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../components"
    "${CMAKE_CURRENT_LIST_DIR}/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(spectra_tests)