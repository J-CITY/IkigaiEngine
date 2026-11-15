"""Pass resolved engine options to Gradle without overriding AGP's toolchain."""

import re


def gradle_cmake_arguments(platform, min_sdk, gles_version, cache_variables=None, **features):
    # Gradle selects these independently for each ABI and build variant.
    managed_by_gradle = {
        "CMAKE_TOOLCHAIN_FILE", "CMAKE_BUILD_TYPE", "CMAKE_CONFIGURATION_TYPES",
        "CMAKE_SYSTEM_NAME", "CMAKE_SYSTEM_PROCESSOR", "CMAKE_ANDROID_ARCH_ABI",
        "CMAKE_ANDROID_NDK", "ANDROID_NDK", "ANDROID_ABI",
        "CMAKE_GENERATOR", "CMAKE_GENERATOR_PLATFORM", "CMAKE_GENERATOR_TOOLSET",
        "CMAKE_GENERATOR_INSTANCE", "CMAKE_MAKE_PROGRAM", "CMAKE_C_COMPILER",
        "CMAKE_CXX_COMPILER", "CMAKE_INSTALL_PREFIX",
        "CMAKE_RUNTIME_OUTPUT_DIRECTORY", "CMAKE_LIBRARY_OUTPUT_DIRECTORY",
        "CMAKE_ARCHIVE_OUTPUT_DIRECTORY", "ENGINE_RUNTIME_OUTPUT_DIRECTORY",
    }
    values = {key: value for key, value in (cache_variables or {}).items()
              if key not in managed_by_gradle and value is not None
              and not key.startswith(("CMAKE_RUNTIME_OUTPUT_DIRECTORY_",
                                      "CMAKE_LIBRARY_OUTPUT_DIRECTORY_",
                                      "CMAKE_ARCHIVE_OUTPUT_DIRECTORY_"))}
    # Optional legacy kwargs remain overrides; omitted values use CMake defaults.
    values.update({key: value for key, value in features.items() if value is not None})
    values.update(IKIGAI_PLATFORM=platform, ANDROID_STL="c++_shared",
                  ANDROID_PLATFORM=f"android-{min_sdk}", IKIGAI_GLES_VERSION=gles_version)
    arguments = []
    for key, value in values.items():
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key):
            raise ValueError(f"Invalid CMake cache variable name: {key!r}")
        if isinstance(value, bool):
            value = "ON" if value else "OFF"
        if not isinstance(value, (str, int, float)):
            raise ValueError(f"Invalid value for CMake cache variable {key}")
        argument = f"-D{key}={value}"
        # Groovy single-quoted strings do not interpolate '$' in paths or values.
        argument = argument.replace("\\", "\\\\").replace("'", "\\'")
        argument = argument.replace("\r", "\\r").replace("\n", "\\n")
        arguments.append("'" + argument + "'")
    return ", ".join(arguments)
