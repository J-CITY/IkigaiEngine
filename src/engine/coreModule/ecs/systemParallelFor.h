#pragma once

#include <cstddef>
#include <functional>

namespace IKIGAI::TASK {
class TaskSystem;
}

namespace IKIGAI::ECS {
class CommandBuffer;

using SystemRangeCallback = std::function<void(std::size_t begin, std::size_t end, CommandBuffer& commands)>;

// Call from a system with declared access. Ranges write to disjoint data,
// and the callback and its captures must be safe to invoke concurrently.
// Range commands are appended to the system's buffer in input order on success.
void parallelForSystem(TASK::TaskSystem& tasks, CommandBuffer& parentCommands, std::size_t begin, std::size_t end, std::size_t grainSize,
	const SystemRangeCallback& runRange);
}
