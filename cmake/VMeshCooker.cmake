include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/VMeshFormat.cmake")
get_filename_component(IKIGAI_VMESH_COOKER_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(IKIGAI_VMESH_COOKER_DIR "${IKIGAI_VMESH_COOKER_ROOT}/src/tools/vmeshCooker")
set(IKIGAI_VMESH_COOKER_THIRD_PARTY "${IKIGAI_VMESH_COOKER_ROOT}/3rd")

file(GLOB MESHOPTIMIZER_SRC "${IKIGAI_VMESH_COOKER_THIRD_PARTY}/meshoptimizer/src/*.cpp"
    "${IKIGAI_VMESH_COOKER_THIRD_PARTY}/meshoptimizer/src/meshoptimizer.h")
add_library(meshoptimizer STATIC ${MESHOPTIMIZER_SRC})
target_include_directories(meshoptimizer PUBLIC "${IKIGAI_VMESH_COOKER_THIRD_PARTY}/meshoptimizer/src")
set_property(TARGET meshoptimizer PROPERTY FOLDER "3rd/meshoptimizer")

add_library(ikigai_vmesh_cooker STATIC
    "${IKIGAI_VMESH_COOKER_DIR}/vmeshCooker.cpp"
    "${IKIGAI_VMESH_COOKER_DIR}/vmeshCooker.h"
    "${IKIGAI_VMESH_COOKER_DIR}/vmeshCookSettings.cpp"
    "${IKIGAI_VMESH_COOKER_DIR}/vmeshCookSettings.h"
    "${IKIGAI_VMESH_COOKER_DIR}/vmeshCookTopology.cpp"
    "${IKIGAI_VMESH_COOKER_DIR}/vmeshCookTopology.h")
target_include_directories(ikigai_vmesh_cooker PUBLIC "${IKIGAI_VMESH_COOKER_DIR}")
target_include_directories(ikigai_vmesh_cooker SYSTEM PUBLIC
    "${IKIGAI_VMESH_COOKER_THIRD_PARTY}/serdepp/include"
    "${IKIGAI_VMESH_COOKER_THIRD_PARTY}/json/include"
    "${IKIGAI_VMESH_COOKER_THIRD_PARTY}/nameof/include"
    "${IKIGAI_VMESH_COOKER_THIRD_PARTY}/magic_enum/include/magic_enum")
target_compile_features(ikigai_vmesh_cooker PUBLIC cxx_std_20)
target_link_libraries(ikigai_vmesh_cooker PUBLIC ikigai_vmesh_format PRIVATE meshoptimizer)
if(MSVC)
    target_compile_options(ikigai_vmesh_cooker PRIVATE /EHsc /utf-8)
endif()
