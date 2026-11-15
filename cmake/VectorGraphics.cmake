# CPU vector graphics library; intentionally independent of ECS and render APIs.
include("${CMAKE_CURRENT_LIST_DIR}/SourceSelection.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Math.cmake")
set(IKIGAI_VECTOR_DIR "${CMAKE_CURRENT_LIST_DIR}/../src/engine/vectorGraphicsModule")
add_library(ikigai_vector STATIC
    "${IKIGAI_VECTOR_DIR}/geometry.cpp"
    "${IKIGAI_VECTOR_DIR}/vectorContext.cpp"
    "${IKIGAI_VECTOR_DIR}/text/fontResource.cpp")
target_compile_features(ikigai_vector PUBLIC cxx_std_20)
target_link_libraries(ikigai_vector PUBLIC ikigai_math)
set_target_properties(ikigai_vector PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(ikigai_vector PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src/engine")
if(TARGET freetype)
    target_link_libraries(ikigai_vector PRIVATE freetype)
elseif(EMSCRIPTEN)
    target_compile_options(ikigai_vector PRIVATE "SHELL:-sUSE_FREETYPE=1")
    target_link_options(ikigai_vector INTERFACE "SHELL:-sUSE_FREETYPE=1")
endif()
if(MSVC)
    target_compile_options(ikigai_vector PRIVATE /utf-8)
endif()
foreach(vector_source_list PROJECT_ABSOLUTE_SOURCES PROJECT_SOURCES)
    ikigai_exclude_sources(${vector_source_list}
        "engine/vectorGraphicsModule/geometry.cpp"
        "engine/vectorGraphicsModule/text/fontResource.cpp"
        "engine/vectorGraphicsModule/vectorContext.cpp")
endforeach()
option(IKIGAI_VECTOR_TESTS "Build vector graphics CPU regression tests" OFF)
if(IKIGAI_VECTOR_TESTS)
    add_executable(ikigai_vector_tests "${CMAKE_CURRENT_LIST_DIR}/../utils/tests/vector_graphics.cpp")
    target_link_libraries(ikigai_vector_tests PRIVATE ikigai_vector)
    enable_testing()
    add_test(NAME vector_graphics COMMAND ikigai_vector_tests "${CMAKE_CURRENT_LIST_DIR}/../assets/engine/fonts/a_AlternaSw.TTF")
endif()
