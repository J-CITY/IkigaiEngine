# Production delivery is independent of optional testModule and utils/tests.
function(ikigai_attach_production_assets target)
    find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
    get_filename_component(production_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    # Run even when the executable does not need relinking: edits to manifest or
    # delivery sources must be verified, and changed asset bytes must be copied.
    add_custom_target(${target}_production_assets
        COMMAND "${Python3_EXECUTABLE}"
            "${production_root}/utils/assets/install_production_assets.py"
            --source "${production_root}/production_assets"
            --assets "${production_root}/assets"
        COMMENT "Verifying and installing production renderer assets..."
        VERBATIM)
    add_dependencies(${target} ${target}_production_assets)
endfunction()
