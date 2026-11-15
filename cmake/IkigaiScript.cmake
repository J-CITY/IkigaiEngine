# Builds the upstream interpreter from a patched copy in this build tree.
include_guard(GLOBAL)

function(ikigai_add_script_library sdk_dir)
    if(TARGET IkigaiScriptCore)
        return()
    endif()
    set(upstream "${sdk_dir}/SimpleScript")
    if(NOT EXISTS "${upstream}/CMakeLists.txt")
        message(FATAL_ERROR "Initialize the IkigaiScript submodule or set USE_IKIGAI_SCRIPT=OFF.")
    endif()

    find_package(Git REQUIRED)
    set(patch "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../patches/ikigai-script-portability.patch")
    # Only copy inputs for the headless upstream project. Configure dependencies
    # also catch local edits and source-only checkouts without submodule metadata.
    file(GLOB_RECURSE inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false
        "${upstream}/Library/*" "${upstream}/Tests/*" "${upstream}/VisualEditor/*")
    list(APPEND inputs "${upstream}/CMakeLists.txt" "${upstream}/json.hpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}" ${inputs})
    file(SHA256 "${patch}" patch_hash)
    set(fingerprint "${patch_hash}")
    foreach(input IN LISTS inputs)
        file(SHA256 "${input}" hash)
        file(RELATIVE_PATH path "${upstream}" "${input}")
        string(APPEND fingerprint "\n${path}:${hash}")
    endforeach()
    string(SHA256 fingerprint "${fingerprint}")
    set(source "${CMAKE_BINARY_DIR}/_deps/ikigai-script-${fingerprint}")
    set(marker "${source}/.ikigai-patch-ready")
    if(NOT EXISTS "${marker}")
        file(COPY "${upstream}/CMakeLists.txt" "${upstream}/Library" "${upstream}/Tests"
            "${upstream}/VisualEditor" "${upstream}/json.hpp" DESTINATION "${source}")
        # Normalize the two patched headers from LF or CRLF checkouts.
        foreach(header scope.hpp stringUtils.hpp)
            configure_file("${upstream}/Library/${header}" "${source}/Library/${header}" @ONLY NEWLINE_STYLE LF)
        endforeach()
        get_filename_component(ceiling "${source}" DIRECTORY)
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=GIT_DIR
            --unset=GIT_WORK_TREE --unset=GIT_INDEX_FILE
            "GIT_CEILING_DIRECTORIES=${ceiling}" "${GIT_EXECUTABLE}" apply "${patch}"
            WORKING_DIRECTORY "${source}" RESULT_VARIABLE result)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "IkigaiScript checkout is incompatible with ${patch}.")
        endif()
        file(TOUCH "${marker}")
    endif()

    set(BUILD_SHARED_LIBS OFF)
    set(IKIGAI_BUILD_VISUAL_EDITOR OFF)
    # EXCLUDE_FROM_ALL keeps upstream tests and the visual editor out of engine builds.
    add_subdirectory("${source}" "${CMAKE_BINARY_DIR}/3rd/IkigaiScript" EXCLUDE_FROM_ALL)
    target_compile_features(IkigaiScriptCore PUBLIC cxx_std_20)
    target_compile_definitions(IkigaiScriptCore PRIVATE NOMINMAX)
endfunction()
