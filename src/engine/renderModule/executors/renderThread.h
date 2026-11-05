#pragma once
#include "renderModule/frame/renderOwner.h"
#include "renderTicket.h"
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>

namespace IKIGAI::RENDER {
class RenderThread final {
public:
	RenderThread();
	~RenderThread();

	// One slot, including work currently executing. Wait for its ticket before posting again.
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
	std::optional<Job> mJob {};
	bool mBusy = false;
	bool mStopping = false;
	std::thread mThread;
};
}
