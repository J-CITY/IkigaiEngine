# Worker task pool. The implementation does not include the renderer or the scene.
include("${CMAKE_CURRENT_LIST_DIR}/Tracy.cmake")
set(IKIGAI_TASK_DIR "${CMAKE_CURRENT_LIST_DIR}/../src/engine/taskModule")
if(NOT TARGET ikigai_task)
    add_library(ikigai_task STATIC
        "${IKIGAI_TASK_DIR}/task.cpp"
        "${IKIGAI_TASK_DIR}/taskSystem.cpp")
    target_compile_features(ikigai_task PUBLIC cxx_std_20)
    target_include_directories(ikigai_task PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src/engine")
    target_link_libraries(ikigai_task PUBLIC ikigai_trace)
    set_target_properties(ikigai_task PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(ikigai_task PRIVATE /W4 /utf-8)
    else()
        target_compile_options(ikigai_task PRIVATE -Wall -Wextra)
    endif()
endif()

foreach(task_source_list PROJECT_ABSOLUTE_SOURCES PROJECT_SOURCES)
    list(FILTER ${task_source_list} EXCLUDE REGEX "taskModule/.*\\.cpp$")
endforeach()
