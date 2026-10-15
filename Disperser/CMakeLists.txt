cmake_minimum_required(VERSION 3.15)

project(Disperser)

# Allows building this folder on its own as well as from the SDK's master CMakeLists.txt
if(NOT DEFINED se_sdk_folder)
    set(CMAKE_CXX_STANDARD 17)
    set(se_sdk_folder ${CMAKE_CURRENT_SOURCE_DIR}/../se_sdk3)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../plugin_helper.cmake)
endif()

include_directories(${se_sdk_folder})

build_gmpi_plugin(
    PROJECT_NAME ${PROJECT_NAME}
    HAS_DSP
    SOURCE_FILES
        Disperser.cpp
        Disperser.h
        ../shared/FilterBase.h
)
