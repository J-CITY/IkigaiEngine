# Generates reflection once per build tree, before any consuming compilation.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/Python.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/EngineModules.cmake")
get_filename_component(IKIGAI_CODEGEN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(ikigai_add_reflection)
    if(TARGET ikigai_reflection)
        return()
    endif()
    set(root "${IKIGAI_CODEGEN_ROOT}")
    set(output_dir "${CMAKE_BINARY_DIR}/generated")
    set(tool "${root}/utils/build/IkigaiHeaderTool.py")
    set(tool_inputs "${tool}"
        "${root}/utils/build/header_parser.py"
        "${root}/utils/build/reflection_annotations.py"
        "${root}/utils/build/reflection_model.py"
        "${root}/utils/build/reflection_render.py"
        "${root}/uv.lock")
    set(headers)
    foreach(module IN LISTS IKIGAI_MODULE_MANIFEST_NAMES)
        foreach(file IN LISTS IKIGAI_MANIFEST_${module})
            get_filename_component(extension "${file}" LAST_EXT)
            if(extension STREQUAL ".h" OR extension STREQUAL ".hpp")
                list(APPEND headers "${file}")
            endif()
        endforeach()
    endforeach()
    # Header edits can add/remove reflected types, changing the output manifest.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${headers} ${tool_inputs})
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${tool}"
        "${root}/src" "${output_dir}" --list-outputs
        OUTPUT_VARIABLE manifest OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    string(REPLACE "\r" "" manifest "${manifest}")
    string(REPLACE "\n" ";" names "${manifest}")
    # A removed header/output must invalidate generation on every generator.
    set(input_manifest "${CMAKE_CURRENT_BINARY_DIR}/reflection-inputs.txt")
    file(CONFIGURE OUTPUT "${input_manifest}" CONTENT "${headers}\n${manifest}\n" @ONLY)
    set(outputs)
    set(sources)
    foreach(name IN LISTS names)
        list(APPEND outputs "${output_dir}/${name}")
        if(name MATCHES "\\.cpp$")
            list(APPEND sources "${output_dir}/${name}")
        endif()
    endforeach()
    set(stamp "${output_dir}/reflection.stamp")
    add_custom_command(OUTPUT "${stamp}" ${outputs}
        COMMAND "${Python3_EXECUTABLE}" "${tool}" "${root}/src" "${output_dir}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${stamp}" ${outputs}
        DEPENDS ${headers} ${tool_inputs} "${input_manifest}"
        COMMENT "Generating engine reflection in the build tree" VERBATIM)
    add_custom_target(ikigai_reflection DEPENDS "${stamp}" ${outputs})
    set_property(TARGET ikigai_reflection PROPERTY IKIGAI_GENERATED_SOURCES "${sources}")
endfunction()

function(ikigai_configure_reflection target)
    ikigai_add_reflection()
    # In-source generated files are not part of the explicit production manifests.
    get_target_property(generated ikigai_reflection IKIGAI_GENERATED_SOURCES)
    target_sources(${target} PRIVATE ${generated})
    target_include_directories(${target} BEFORE PRIVATE "${CMAKE_BINARY_DIR}")
    add_dependencies(${target} ikigai_reflection)
endfunction()
