# The pinned ImGui backend rotates present semaphores by CPU frame. WSI allows
# reusing one once its swapchain image has been acquired again.
function(ikigai_prepare_imgui_vulkan backend_dir output_variable)
    set(input "${backend_dir}/imgui_impl_vulkan.cpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
    file(READ "${input}" source)
    foreach(member IN ITEMS pSignalSemaphores pWaitSemaphores)
        set(original "info.${member} = &fsd->RenderCompleteSemaphore;")
        string(FIND "${source}" "${original}" position)
        if(position EQUAL -1)
            message(FATAL_ERROR "Review the Vulkan present semaphore fix for this ImGui revision")
        endif()
        string(REPLACE "${original}"
            "info.${member} = &wd->FrameSemaphores[wd->FrameIndex].RenderCompleteSemaphore;"
            source "${source}")
    endforeach()
    string(REPLACE "ImGui_ImplVulkanH_FrameSemaphores* fsd = &wd->FrameSemaphores[wd->SemaphoreIndex];\n    VkPresentInfoKHR"
        "VkPresentInfoKHR" source "${source}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/imgui-vulkan/imgui_impl_vulkan.cpp")
    configure_file("${backend_dir}/imgui_impl_vulkan.h"
        "${CMAKE_CURRENT_BINARY_DIR}/imgui-vulkan/imgui_impl_vulkan.h" COPYONLY)
    file(GENERATE OUTPUT "${output}" CONTENT "${source}")
    set(${output_variable} "${output}" PARENT_SCOPE)
endfunction()
