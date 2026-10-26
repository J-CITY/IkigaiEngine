include("${CMAKE_CURRENT_LIST_DIR}/Memory.cmake")
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/Python.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Physics2D.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Physics3D.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/JsonViews.cmake")
# Keep versioned script examples over the downloaded asset archive, including
# before Emscripten links its preloaded asset bundle.
get_filename_component(_ikigai_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(GLOB_RECURSE _ikigai_scripts LIST_DIRECTORIES false "${_ikigai_root}/script_assets/*")
foreach(_script IN LISTS _ikigai_scripts)
    file(RELATIVE_PATH _relative "${_ikigai_root}/script_assets" "${_script}")
    configure_file("${_script}" "${_ikigai_root}/assets/${_relative}" COPYONLY)
endforeach()

option(USE_LUA "Enable Lua scripting" ON)
option(USE_IKIGAI_SCRIPT "Enable IkigaiScript scripting" ON)

function(ikigai_configure_scripting target)
    ikigai_configure_physics2d(${target})
    ikigai_configure_physics3d(${target})
    ikigai_configure_memory(${target})
    ikigai_configure_json_views(${target})
    # Explicit outputs are required on the first configure: generated files are
    # absent from the engine's source glob until the header tool has run.
    set(_script_headers)
    set(_script_generated)
    set(_script_registration)
    foreach(_feature LUA IKIGAI_SCRIPT)
        if(USE_${_feature})
            if(_feature STREQUAL "LUA")
                set(_name scriptComponent)
            else()
                set(_name ikigaiScriptComponent)
            endif()
            list(APPEND _script_headers "${MAIN_SOURCE_DIR}/engine/coreModule/ecs/components/${_name}.h")
            list(APPEND _script_generated "${MAIN_SOURCE_DIR}/engine/generated/${_name}.generated.h"
                "${MAIN_SOURCE_DIR}/engine/generated/${_name}.generated.cpp")
            list(APPEND _script_registration "${MAIN_SOURCE_DIR}/engine/generated/${_name}.generated.cpp")
        endif()
    endforeach()
    if(_script_generated)
        add_custom_command(OUTPUT ${_script_generated}
            COMMAND "${Python3_EXECUTABLE}" "${MAIN_SOURCE_DIR}/../utils/build/IkigaiHeaderTool.py"
                "${MAIN_SOURCE_DIR}" "${MAIN_SOURCE_DIR}/engine/generated"
            DEPENDS ${_script_headers} "${MAIN_SOURCE_DIR}/../utils/build/IkigaiHeaderTool.py"
            COMMENT "Generating scripting component reflection"
            VERBATIM)
        add_custom_target(ikigai_scripting_reflection DEPENDS ${_script_generated})
        add_dependencies(${target} ikigai_scripting_reflection)
        target_sources(${target} PRIVATE ${_script_registration})
    endif()
    target_compile_definitions(${target} PRIVATE
        USE_LUA=$<BOOL:${USE_LUA}> USE_IKIGAI_SCRIPT=$<BOOL:${USE_IKIGAI_SCRIPT}>)
    if(USE_LUA)
        target_link_libraries(${target} PRIVATE lua)
        target_include_directories(${target} PRIVATE "${THIRD_PARTY_DIR}/sol/include")
    else()
        get_target_property(sources ${target} SOURCES)
        list(FILTER sources EXCLUDE REGEX "(lua[^/]*Binder|scriptInterpreter|scriptComponent|scriptSystem)(\\.generated)?\\.(cpp|cxx|c)$")
        set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    endif()
    if(USE_IKIGAI_SCRIPT)
        # The upstream checkout keeps CMake inside SimpleScript/. EXCLUDE_FROM_ALL
        # prevents its standalone tests and VisualCore from joining engine builds.
        if(NOT TARGET IkigaiScriptCore)
            set(BUILD_SHARED_LIBS OFF)
            set(IKIGAI_BUILD_VISUAL_EDITOR OFF)
            add_subdirectory("${THIRD_PARTY_DIR}/IkigaiScript/SimpleScript"
                "${CMAKE_BINARY_DIR}/3rd/IkigaiScript" EXCLUDE_FROM_ALL)
            target_compile_features(IkigaiScriptCore PUBLIC cxx_std_20)
            target_compile_definitions(IkigaiScriptCore PRIVATE NOMINMAX)
        endif()
        target_link_libraries(${target} PRIVATE IkigaiScriptCore)
    else()
        get_target_property(sources ${target} SOURCES)
        list(FILTER sources EXCLUDE REGEX "ikigaiScript[^/]*\\.(cpp|cxx|c)$")
        set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    endif()
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/../utils/tests/cmake/Scripting.cmake" OPTIONAL)
