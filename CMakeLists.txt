cmake_minimum_required(VERSION 3.16)
set(CMAKE_C_STANDARD 23)

if(NOT DEFINED BOARD)
    message(FATAL_ERROR
        "Must specify the board, ex.\n"
        "idf.py -DBOARD=panda_v3 build flash\n"
        "All boards are found in the boards/ directory"
    )
endif()

set(BOARD_CONFIG "${CMAKE_SOURCE_DIR}/boards/${TARGET_BOARD}/esp_config.json")
set(BOARD_MAPPING "${CMAKE_SOURCE_DIR}/boards/${TARGET_BOARD}/esp_mapping.json")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ctl-control-node LANGUAGES C)