cmake_minimum_required(VERSION 3.20)
project(IkigaiCore LANGUAGES CXX)

# Production libraries that must build without graphics SDKs or local test trees.
include("${CMAKE_CURRENT_LIST_DIR}/../Task.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../Coroutines.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../RenderCommands.cmake")
