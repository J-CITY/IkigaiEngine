# Shared .vmesh encoding and file format for the cooker and runtime.
include_guard(GLOBAL)
get_filename_component(IKIGAI_VMESH_FORMAT_DIR "${CMAKE_CURRENT_LIST_DIR}/../src/engine/virtualGeometryModule" ABSOLUTE)

add_library(ikigai_vmesh_format STATIC
    "${IKIGAI_VMESH_FORMAT_DIR}/format/vmeshEncoding.cpp"
    "${IKIGAI_VMESH_FORMAT_DIR}/format/vmeshEncoding.h"
    "${IKIGAI_VMESH_FORMAT_DIR}/format/vmeshIO.cpp"
    "${IKIGAI_VMESH_FORMAT_DIR}/format/vmeshIO.h"
    "${IKIGAI_VMESH_FORMAT_DIR}/vmeshFormat.h")
target_compile_features(ikigai_vmesh_format PUBLIC cxx_std_20)
target_include_directories(ikigai_vmesh_format PUBLIC "${IKIGAI_VMESH_FORMAT_DIR}/..")
set_target_properties(ikigai_vmesh_format PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(ikigai_vmesh_format PRIVATE /EHsc /utf-8)
elseif(ANDROID OR EMSCRIPTEN)
    target_compile_options(ikigai_vmesh_format PRIVATE -fexceptions)
endif()
