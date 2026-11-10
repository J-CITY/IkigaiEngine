#pragma once
#include "renderModule/frame/renderOwner.h"
#include "renderTicket.h"
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

namespace IKIGAI::RENDER {
class RenderThread final {
public:
	enum class Role { REPLAY, GPU_COMPLETION };
	explicit RenderThread(size_t capacity = 1, Role role = Role::REPLAY);
	~RenderThread();

	// Capacity includes work currently executing.
	[[nodiscard]] RenderTicket post(std::function<void()> work);

private:
	struct Job {
		std::function<void()> Work;
		std::promise<void> Completion;
	};

	void run();

	RenderOwner mProducer {};
	std::mutex mMutex {};
	std::condition_variable mWake {};
	std::deque<Job> mJobs {};
	size_t mCapacity = 1;
	size_t mOutstanding = 0;
	bool mStopping = false;
	std::thread mThread;
};
}
