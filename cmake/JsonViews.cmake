include_guard(GLOBAL)
option(USE_JSON_VIEWS "Enable JSON editor/debug windows independently of USE_EDITOR" ON)

function(ikigai_configure_json_views target)
    target_compile_definitions(${target} PRIVATE USE_JSON_VIEWS=$<BOOL:${USE_JSON_VIEWS}>)
endfunction()
