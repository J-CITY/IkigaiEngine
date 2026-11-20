# CPU virtual geometry runtime. The byte format is shared with the offline cooker.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/Math.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/VMeshFormat.cmake")
get_filename_component(IKIGAI_VG_RUNTIME_DIR "${CMAKE_CURRENT_LIST_DIR}/../src/engine/virtualGeometryModule/runtime" ABSOLUTE)

add_library(ikigai_virtual_geometry STATIC
    "${IKIGAI_VG_RUNTIME_DIR}/virtualGeometryHierarchy.cpp"
    "${IKIGAI_VG_RUNTIME_DIR}/virtualGeometryHierarchy.h"
    "${IKIGAI_VG_RUNTIME_DIR}/virtualGeometryResidency.cpp"
    "${IKIGAI_VG_RUNTIME_DIR}/virtualGeometryResidency.h"
    "${IKIGAI_VG_RUNTIME_DIR}/virtualGeometrySelection.cpp"
    "${IKIGAI_VG_RUNTIME_DIR}/virtualGeometrySelection.h"
    "${IKIGAI_VG_RUNTIME_DIR}/virtualMeshDebugMode.h"
    "${IKIGAI_VG_RUNTIME_DIR}/virtualMeshPageSource.cpp"
    "${IKIGAI_VG_RUNTIME_DIR}/virtualMeshPageSource.h")
target_link_libraries(ikigai_virtual_geometry PUBLIC ikigai_math ikigai_vmesh_format)
target_compile_features(ikigai_virtual_geometry PUBLIC cxx_std_20)
set_target_properties(ikigai_virtual_geometry PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(ikigai_virtual_geometry PRIVATE /utf-8)
endif()
