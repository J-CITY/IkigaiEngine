# Builds the shared service registry used by engine modules and standalone consumers.
include_guard(GLOBAL)
get_filename_component(IKIGAI_SERVICES_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(IKIGAI_SERVICES_DIR "${IKIGAI_SERVICES_ROOT}/src/engine/utilsModule")

add_library(ikigai_services STATIC
    "${IKIGAI_SERVICES_DIR}/serviceManager.cpp"
    "${IKIGAI_SERVICES_DIR}/serviceManager.h"
    "${IKIGAI_SERVICES_DIR}/singletonService.h")
target_compile_features(ikigai_services PUBLIC cxx_std_20)
target_include_directories(ikigai_services PUBLIC "${IKIGAI_SERVICES_ROOT}/src/engine")
set_target_properties(ikigai_services PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(ikigai_services PRIVATE /W4 /utf-8)
    target_compile_options(ikigai_services PUBLIC /EHsc)
else()
    target_compile_options(ikigai_services PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(NOT EMSCRIPTEN)
    find_package(Threads REQUIRED)
    target_link_libraries(ikigai_services PUBLIC Threads::Threads)
endif()
