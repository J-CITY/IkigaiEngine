include_guard(GLOBAL)

option(IKIGAI_ENABLE_MEMORY_TRACKING "Track engine CPU/GPU allocations; snapshots are captured on demand" OFF)

# The definition must agree across targets because it changes inline code and type layouts.
add_library(ikigai_memory_tracking INTERFACE)
target_compile_definitions(ikigai_memory_tracking INTERFACE
    IKIGAI_ENABLE_MEMORY_TRACKING=$<BOOL:${IKIGAI_ENABLE_MEMORY_TRACKING}>)
