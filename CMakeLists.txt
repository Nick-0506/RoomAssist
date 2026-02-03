
# (Automatically converted from project Makefile by convert_to_cmake.py.)

# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

if(DEFINED ENV{HOMEKIT_PATH})
  set(HOMEKIT_PATH $ENV{HOMEKIT_PATH})
else()
  # NICK_FIX If move the folder form homekit-sdk to esp root, need to change the path
  set(HOMEKIT_PATH ${CMAKE_CURRENT_LIST_DIR}/../esp-homekit-sdk)
endif(DEFINED ENV{HOMEKIT_PATH})

# NICK_FIX If move the folder form homekit-sdk to esp root, need to change the path
# set(EXTRA_COMPONENT_DIRS ${HOMEKIT_PATH}/components ${HOMEKIT_PATH}/components/homekit ${HOMEKIT_PATH}/examples/common)
set(EXTRA_COMPONENT_DIRS ${HOMEKIT_PATH}/components ${HOMEKIT_PATH}/components/homekit ${HOMEKIT_PATH}/examples/common ../esp-idf/components/ssd1306 ../esp-idf/components/dht)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define version and build time
set(BUILD_VERSION "2.1.8")
execute_process(
    COMMAND date +%Y-%m-%d_%H:%M:%S
    OUTPUT_VARIABLE BUILD_TIME
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Pass these definitions to the compiler
add_compile_definitions(BUILD_VERSION=\"${BUILD_VERSION}\")
add_compile_definitions(BUILD_TIME=\"${BUILD_TIME}\")
add_definitions(-DBUILD_VERSION=\"${BUILD_VERSION}\")
add_definitions(-DBUILD_TIME=\"${BUILD_TIME}\")			
project(emulator)
spiffs_create_partition_image(spiffs ${CMAKE_CURRENT_LIST_DIR}/spiffs FLASH_IN_PROJECT)
