# Configures production crash metadata and debug symbols, without test dependencies.
# The optional reporter executable is built separately in testModule/tools/crashReporter.
include_guard(GLOBAL)

function(ikigai_configure_crash_reporting target)
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    set(revision unknown)
    find_package(Git QUIET)
    if(Git_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --verify HEAD
            WORKING_DIRECTORY "${root}"
            OUTPUT_VARIABLE git_head
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        string(LENGTH "${git_head}" git_head_length)
        if(git_head_length LESS_EQUAL 64 AND git_head MATCHES "^[0-9a-fA-F]+$")
            set(revision "${git_head}")
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no --ignore-submodules=all
                WORKING_DIRECTORY "${root}"
                RESULT_VARIABLE git_status
                OUTPUT_VARIABLE tracked_changes
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(git_status EQUAL 0 AND tracked_changes)
                string(APPEND revision "-dirty")
            endif()
        endif()
    endif()
    set_property(SOURCE "${root}/src/engine/utilsModule/crashReport.cpp" TARGET_DIRECTORY ${target} APPEND PROPERTY COMPILE_DEFINITIONS
        IKIGAI_CRASH_BUILD_CONFIGURATION="$<CONFIG>"
        IKIGAI_CRASH_BUILD_REVISION="${revision}"
    )
    if(MSVC)
        # Release and MinSizeRel lack debug information in the MSVC defaults.
        target_compile_options(${target} PRIVATE "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/Zi>")
        # Reflection/template debug records can exceed the 4 GiB default PDB limit.
        target_link_options(${target} PRIVATE /DEBUG:FULL /PDBPAGESIZE:8192
            "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/OPT:REF>"
            "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/OPT:ICF>"
        )
    endif()
endfunction()
