# Some Android NDK libc++ versions gate C++20 stop tokens and jthread.
# Probe both compilation and linking before opting affected targets into that library.
include_guard(GLOBAL)

function(ikigai_enable_stop_token target)
    if(NOT ANDROID)
        return()
    endif()
    include(CheckCXXSourceCompiles)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(probe [[
        #include <stop_token>
        #include <thread>
        int main() {
            std::jthread worker([](std::stop_token) {});
            worker.request_stop();
        }
    ]])
    check_cxx_source_compiles("${probe}" IKIGAI_ANDROID_HAS_STOP_TOKEN)
    if(IKIGAI_ANDROID_HAS_STOP_TOKEN)
        return()
    endif()
    string(APPEND CMAKE_REQUIRED_FLAGS " -fexperimental-library")
    check_cxx_source_compiles("${probe}" IKIGAI_ANDROID_HAS_EXPERIMENTAL_STOP_TOKEN)
    if(NOT IKIGAI_ANDROID_HAS_EXPERIMENTAL_STOP_TOKEN)
        message(FATAL_ERROR "The Android NDK must provide C++20 std::jthread and std::stop_token. Select an NDK with modern libc++ (for example r27 or later).")
    endif()
    target_compile_options(${target} PUBLIC -fexperimental-library)
    target_link_options(${target} INTERFACE -fexperimental-library)
endfunction()
