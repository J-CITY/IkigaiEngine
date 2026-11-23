# Configures optional scripting runtimes and their engine integration.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/SourceSelection.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Python.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Physics2D.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/Physics3D.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/JsonViews.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/EngineOptions.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/IkigaiScript.cmake")

function(ikigai_configure_scripting target)
    ikigai_configure_physics2d(${target})
    ikigai_configure_physics3d(${target})
    ikigai_configure_json_views(${target})
    target_compile_definitions(${target} PRIVATE
        USE_LUA=$<BOOL:${USE_LUA}> USE_IKIGAI_SCRIPT=$<BOOL:${USE_IKIGAI_SCRIPT}>)
    if(USE_LUA)
        target_link_libraries(${target} PRIVATE lua)
        target_include_directories(${target} PRIVATE "${THIRD_PARTY_DIR}/sol/include")
    else()
        get_target_property(sources ${target} SOURCES)
        ikigai_exclude_sources(sources
            "engine/coreModule/ecs/components/scriptComponent.cpp"
            "engine/coreModule/ecs/systems/scriptSystem.cpp"
            "engine/generated/scriptComponent.generated.cpp"
            "engine/coreModule/scripting/luaActorBinder.cpp"
            "engine/coreModule/scripting/luaBinder.cpp"
            "engine/coreModule/scripting/luaComponentBinder.cpp"
            "engine/coreModule/scripting/luaGlobalsBinder.cpp"
            "engine/coreModule/scripting/luaMathsBinder.cpp"
            "engine/coreModule/scripting/scriptInterpreter.cpp"
            "${CMAKE_BINARY_DIR}/generated/scriptComponent.generated.cpp")
        set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    endif()
    if(USE_IKIGAI_SCRIPT)
        ikigai_add_script_library("${THIRD_PARTY_DIR}/IkigaiScript")
        target_link_libraries(${target} PRIVATE IkigaiScriptCore)
    else()
        get_target_property(sources ${target} SOURCES)
        ikigai_exclude_sources(sources
            "engine/coreModule/ecs/components/ikigaiScriptComponent.cpp"
            "engine/generated/ikigaiScriptComponent.generated.cpp"
            "engine/coreModule/scripting/ikigaiScriptBinder.cpp"
            "engine/coreModule/scripting/ikigaiScriptMathsBinder.cpp"
            "engine/scriptModule/ikigaiScriptRuntime.cpp"
            "${CMAKE_BINARY_DIR}/generated/ikigaiScriptComponent.generated.cpp")
        set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    endif()
endfunction()
