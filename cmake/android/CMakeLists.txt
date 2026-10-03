cmake_minimum_required(VERSION 3.22)

project(IkigaiEngineAndroid LANGUAGES C CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "cmake/android must be configured with the Android NDK toolchain (Gradle externalNativeBuild).")
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../IkigaiEngineAndroid.cmake")
