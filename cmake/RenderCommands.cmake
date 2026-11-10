# Owner-thread command storage and execution policies, independent of graphics SDKs.
include("${CMAKE_CURRENT_LIST_DIR}/Tracy.cmake")
set(IKIGAI_RENDER_COMMAND_ROOT "${CMAKE_CURRENT_LIST_DIR}/../src/engine")
if(NOT TARGET ikigai_render_commands)
    add_library(ikigai_render_commands STATIC
        "${IKIGAI_RENDER_COMMAND_ROOT}/renderModule/commands/renderCommandBuffer.cpp"
        "${IKIGAI_RENDER_COMMAND_ROOT}/renderModule/executors/renderExecutor.cpp"
        "${IKIGAI_RENDER_COMMAND_ROOT}/renderModule/frame/renderOwner.cpp"
        "${IKIGAI_RENDER_COMMAND_ROOT}/renderModule/frame/gpuCompletion.cpp"
        "${IKIGAI_RENDER_COMMAND_ROOT}/renderModule/frame/renderFrameSnapshot.cpp"
        "${IKIGAI_RENDER_COMMAND_ROOT}/renderModule/frame/frameContext.cpp"
        "${IKIGAI_RENDER_COMMAND_ROOT}/renderModule/renderGraph/renderGraphCompiler.cpp")
    target_compile_features(ikigai_render_commands PUBLIC cxx_std_20)
    target_include_directories(ikigai_render_commands PUBLIC "${IKIGAI_RENDER_COMMAND_ROOT}")
    target_link_libraries(ikigai_render_commands PUBLIC ikigai_trace)
    set_target_properties(ikigai_render_commands PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
foreach(command_source_list PROJECT_ABSOLUTE_SOURCES PROJECT_SOURCES)
    list(FILTER ${command_source_list} EXCLUDE REGEX "renderModule/(commands/renderCommandBuffer|executors/renderExecutor)\\.cpp$")
    list(FILTER ${command_source_list} EXCLUDE REGEX "renderModule/frame/(renderOwner|gpuCompletion|renderFrameSnapshot|frameContext)\\.cpp$")
    list(FILTER ${command_source_list} EXCLUDE REGEX "renderModule/renderGraph/renderGraphCompiler\\.cpp$")
endforeach()
