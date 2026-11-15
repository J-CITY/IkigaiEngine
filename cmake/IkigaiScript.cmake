# Builds the interpreter from the pinned IkigaiScript submodule.
include_guard(GLOBAL)

function(ikigai_add_script_library sdk_dir)
    if(TARGET IkigaiScriptCore)
        return()
    endif()
    set(upstream "${sdk_dir}/SimpleScript")
    if(NOT EXISTS "${upstream}/CMakeLists.txt")
        message(FATAL_ERROR "Initialize the IkigaiScript submodule or set USE_IKIGAI_SCRIPT=OFF.")
    endif()

    set(BUILD_SHARED_LIBS OFF)
    set(IKIGAI_BUILD_VISUAL_EDITOR OFF)
    # EXCLUDE_FROM_ALL keeps upstream tests and the visual editor out of engine builds.
    add_subdirectory("${upstream}" "${CMAKE_BINARY_DIR}/3rd/IkigaiScript" EXCLUDE_FROM_ALL)
    target_compile_features(IkigaiScriptCore PUBLIC cxx_std_20)
    target_compile_definitions(IkigaiScriptCore PRIVATE NOMINMAX)
endfunction()
