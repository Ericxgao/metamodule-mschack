cmake_minimum_required(VERSION 3.24)

if (DEFINED ENV{METAMODULE_SDK_DIR})
    set(METAMODULE_SDK_DIR "$ENV{METAMODULE_SDK_DIR}")
elseif("${METAMODULE_SDK_DIR}" STREQUAL "")
    set(METAMODULE_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../metamodule-plugin-sdk")
endif()
include(${METAMODULE_SDK_DIR}/plugin.cmake)

project(mscHack VERSION 2.0.1 DESCRIPTION "mscHack Plugin Pack" LANGUAGES C CXX)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define source directories
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mschack-VCV-Rack-Plugins)
set(DSP_DIR ${SOURCE_DIR}/dsp)

add_library(mscHack STATIC)

target_compile_definitions(mscHack PRIVATE 
    __V1x 
    __PLUGIN
)

# Add compile options for better control over symbols
target_compile_options(mscHack PRIVATE
    -fdata-sections
    -ffunction-sections
    -fno-exceptions
    -fno-threadsafe-statics
    -fno-use-cxa-atexit
    $<$<CXX_COMPILER_ID:GNU>:-fvisibility=hidden>
    $<$<CXX_COMPILER_ID:GNU>:-fvisibility-inlines-hidden>
)

# Add linker flags for static linking
target_link_options(mscHack PRIVATE
    -Wl,--gc-sections
    -static-libgcc
    -static-libstdc++
)

# Add source files
target_sources(mscHack PRIVATE
    # ${SOURCE_DIR}/3Ch_Osc.cpp
    # ${SOURCE_DIR}/ARP700.cpp
    # ${SOURCE_DIR}/ASAF8.cpp
    # ${SOURCE_DIR}/Alienz.cpp
    ${SOURCE_DIR}/src/Compressor.cpp
    ${SOURCE_DIR}/src/mscHack.cpp
    ${SOURCE_DIR}/src/MasterClockx4.cpp
    ${SOURCE_DIR}/src/PingPong.cpp
    ${SOURCE_DIR}/src/StepDelay.cpp
    ${SOURCE_DIR}/src/OSC_WaveMorph_3.cpp
    ${SOURCE_DIR}/src/mscHack_Control_EnvelopeEdit.cpp
    ${SOURCE_DIR}/src/mscHack_Control_EnvelopeData.cpp
    ${SOURCE_DIR}/src/3Ch_Osc.cpp
    ${SOURCE_DIR}/src/ARP700.cpp
    ${SOURCE_DIR}/src/Mixer_4_0_4.cpp
    ${SOURCE_DIR}/src/ASAF8.cpp
    ${SOURCE_DIR}/src/Alienz.cpp
    ${SOURCE_DIR}/src/Dronez.cpp
    ${SOURCE_DIR}/src/Lorenz.cpp
    ${SOURCE_DIR}/src/Mixer_16_4_4.cpp
    ${SOURCE_DIR}/src/Mixer_24_4_4.cpp
    ${SOURCE_DIR}/src/Mixer_9_3_4.cpp
    ${SOURCE_DIR}/src/Maude221.cpp
    ${SOURCE_DIR}/src/Morze.cpp
    ${SOURCE_DIR}/src/Windz.cpp
    ${SOURCE_DIR}/src/SynthDrums.cpp
    ${SOURCE_DIR}/src/StepDelay.cpp
)

# Add include directories
target_include_directories(mscHack PRIVATE 
    ${SOURCE_DIR}
)

# Create the plugin file
create_plugin(
    SOURCE_LIB      mscHack
    PLUGIN_NAME     mscHack
    PLUGIN_JSON     ${SOURCE_DIR}/plugin.json
    SOURCE_ASSETS   ${CMAKE_CURRENT_LIST_DIR}/assets
    DESTINATION     ${CMAKE_CURRENT_LIST_DIR}/metamodule-plugins
) 