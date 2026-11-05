#pragma once
#include <future>

namespace IKIGAI::COROUTINE {
template <typename T> class Task;
}

namespace IKIGAI::RENDER {
using RenderTicket = std::shared_future<void>;

// The scheduler polls on its owner thread. Cancelling a waiter leaves submitted work in flight.
[[nodiscard]] COROUTINE::Task<void> AwaitRender(RenderTicket ticket);
}
