# Explicit source manifest for application entry points and platform adapters.
# Add new production files here; generated reflection files are owned by Codegen.cmake.
set(IKIGAI_MANIFEST_application
    "${IKIGAI_SOURCE_ROOT}/EcsDemo.cpp"
    "${IKIGAI_SOURCE_ROOT}/EcsDemo.h"
    "${IKIGAI_SOURCE_ROOT}/ecsDemo/components.h"
    "${IKIGAI_SOURCE_ROOT}/ecsDemo/example.cpp"
    "${IKIGAI_SOURCE_ROOT}/ecsDemo/systems.cpp"
    "${IKIGAI_SOURCE_ROOT}/ecsDemo/systems.h"
    "${IKIGAI_SOURCE_ROOT}/main.cpp"
    "${IKIGAI_SOURCE_ROOT}/platform/quest/android_main.cpp"
    "${IKIGAI_SOURCE_ROOT}/ReflectionDemo.h"
)
