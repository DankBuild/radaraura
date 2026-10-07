cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-Wno-format)
add_compile_options(-Wno-ignored-qualifiers)
project(radaraura)

# Optional spoken alerts: if tools/tts_output/ exists (voice1/ .. voice4/ with
# MP3s, see tools/generate_tts.py) it is packed into the SPIFFS "storage"
# partition and flashed with the firmware. Without it the device beeps only.
set(TTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../tools/tts_output)
if(EXISTS ${TTS_DIR})
    spiffs_create_partition_image(storage ${TTS_DIR} FLASH_IN_PROJECT)
else()
    message(STATUS "No tools/tts_output - building without voice alerts")
endif()
