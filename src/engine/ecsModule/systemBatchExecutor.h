#pragma once
#include <cstddef>
#include <functional>
#include <span>

namespace IKIGAI::ECS {
// A true result means every job was executed exactly once. Join accepted work before returning or throwing.
// Return false only if no jobs were accepted and the caller should execute them inline.
using SystemBatchExecutor = std::function<bool(std::span<const char* const>, const std::function<void(std::size_t)>&)>;
}
