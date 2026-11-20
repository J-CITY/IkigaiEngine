#pragma once
#include <cstddef>
#include <functional>
#include <span>

namespace IKIGAI::ECS {
// Execute one job per name and finish all accepted work before returning or throwing.
// Return false only if no jobs were started and the caller should execute them inline.
using SystemBatchExecutor = std::function<bool(std::span<const char* const>, const std::function<void(std::size_t)>&)>;
}
