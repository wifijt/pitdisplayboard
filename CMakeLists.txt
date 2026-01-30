# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
add_compile_definitions(USE_GFX_ROOT)
add_compile_definitions(HUB75_FOR_IDF)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(5459_PitBoard)
