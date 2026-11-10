include_guard(GLOBAL)

function(ikigai_configure_crash_reporting target)
    get_filename_component(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    if(NOT TARGET ikigai_crash_reporter)
        add_executable(ikigai_crash_reporter "${root}/utils/crashReporter/main.cpp")
        set_target_properties(ikigai_crash_reporter PROPERTIES OUTPUT_NAME IkigaiCrashReporter)
        target_compile_features(ikigai_crash_reporter PRIVATE cxx_std_20)
        target_compile_definitions(ikigai_crash_reporter PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_include_directories(ikigai_crash_reporter PRIVATE "${root}/src/engine")
        target_link_libraries(ikigai_crash_reporter PRIVATE dbghelp)
    endif()
    add_dependencies(${target} ikigai_crash_reporter)

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
