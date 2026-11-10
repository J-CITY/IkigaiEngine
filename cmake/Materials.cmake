# Production shader/material assets are independent of the optional test module.
function(ikigai_attach_material_assets target)
    get_filename_component(material_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${material_root}/material_assets/engine" "${material_root}/assets/engine"
        COMMENT "Installing layered PBR material assets..."
        VERBATIM)
endfunction()
