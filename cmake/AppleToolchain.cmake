# Select Apple SDK defaults before CMake detects compilers.
include_guard(GLOBAL)

function(ikigai_configure_apple_toolchain platform)
    if(platform STREQUAL "ios")
        set(CMAKE_SYSTEM_NAME iOS PARENT_SCOPE)
        set(CMAKE_OSX_SYSROOT iphoneos CACHE STRING "iOS SDK (iphoneos or iphonesimulator)")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum iOS deployment version")
    elseif("$ENV{MACOSX_DEPLOYMENT_TARGET}" STREQUAL "")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment version")
    endif()
endfunction()
