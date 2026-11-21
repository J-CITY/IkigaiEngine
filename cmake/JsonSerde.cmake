include_guard(GLOBAL)

get_filename_component(IKIGAI_JSON_SERDE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(ikigai_json_serde INTERFACE)
target_include_directories(ikigai_json_serde SYSTEM INTERFACE
    "${IKIGAI_JSON_SERDE_ROOT}/3rd/serdepp/include"
    "${IKIGAI_JSON_SERDE_ROOT}/3rd/json/include"
    "${IKIGAI_JSON_SERDE_ROOT}/3rd/nameof/include"
    "${IKIGAI_JSON_SERDE_ROOT}/3rd/magic_enum/include/magic_enum")
