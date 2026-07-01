cmake_minimum_required(VERSION 3.28)
set(CMAKE_C_STANDARD 23)

if(NOT DEFINED BOARD_CONFIG OR NOT DEFINED BOARD_MAPPING)
    message(FATAL_ERROR
        "Must use a board cmake preset, ex.\n"
        "idf.py --preset panda-v3 build flash"
    )
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ctl-control-node LANGUAGES C)