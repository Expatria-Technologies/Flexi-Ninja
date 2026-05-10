cmake_minimum_required(VERSION 3.12)

# Try common SDK paths
if(DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../pico-sdk)
    set(PICO_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../pico-sdk)
elseif(EXISTS ${HOME}/pico-sdk)
    set(PICO_SDK_PATH ${HOME}/pico-sdk)
else()
    set(PICO_SDK_PATH "/opt/pico-sdk")
endif()

set(PICO_BOARD pico2 CACHE STRING "Pico board type")
set(CMAKE_BUILD_TYPE Release)

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(spi-slave-test C CXX ASM)

pico_sdk_init()

add_executable(spi-slave-test main.c)

target_include_directories(spi-slave-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(spi-slave-test PRIVATE
    pico_stdlib
    hardware_spi
    hardware_gpio
)

pico_add_extra_outputs(spi-slave-test)
