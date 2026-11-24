# Configure a separate Windows build from the repository root:
# cmake -S . -B build/render-smoke -DIKIGAI_BUILD_TEST_MODULE=ON -DUSE_OPENGL=ON -DUSE_SDL=ON -DUSE_CAPTURE=ON
# cmake --build build/render-smoke --config Debug --target IkigaiEngine --parallel 6
# ctest --test-dir build/render-smoke -C Debug -L render-smoke --output-on-failure

function(ikigai_attach_test_module target)
    if(NOT WIN32 OR NOT USE_SDL OR NOT USE_OPENGL OR NOT USE_CAPTURE)
        message(FATAL_ERROR "Render smoke requires Windows desktop SDL, OpenGL and screenshot capture")
    endif()
    get_target_property(sources ${target} SOURCES)
    list(REMOVE_ITEM sources "${IKIGAI_APPLICATION_ROOT}/src/main.cpp")
    set_property(TARGET ${target} PROPERTY SOURCES "${sources}")
    target_sources(${target} PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/main.cpp")
    set_target_properties(${target} PROPERTIES OUTPUT_NAME IkigaiRenderSmoke WIN32_EXECUTABLE FALSE)
    foreach(scene gui-demo environment-demo)
        set(artifacts "${CMAKE_BINARY_DIR}/render-smoke/${scene}")
        file(MAKE_DIRECTORY "${artifacts}")
        add_test(NAME render_${scene} COMMAND ${target} ${scene})
        set_tests_properties(render_${scene} PROPERTIES LABELS render-smoke TIMEOUT 180
            WORKING_DIRECTORY "${artifacts}" RUN_SERIAL TRUE)
    endforeach()
endfunction()
