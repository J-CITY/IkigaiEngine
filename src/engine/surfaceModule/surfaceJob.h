#pragma once
#include "taskModule/backgroundTicket.h"
#include <any>
#include <atomic>
#include <functional>
#include <memory>
#include <stop_token>

namespace IKIGAI::SURFACE {
class BackgroundJob {
public:
	explicit BackgroundJob(std::function<std::any(const std::atomic_bool&)> work);
	~BackgroundJob();
	BackgroundJob(const BackgroundJob&) = delete;
	BackgroundJob& operator=(const BackgroundJob&) = delete;
	bool ready() const;
	bool failed() const;
	std::any take();
	void cancel();

private:
	struct State;
	std::shared_ptr<State> mState = {};
};
// Completes rejected work synchronously and accepted work exactly once.
void dispatchSurfaceBuild(std::function<void(std::stop_token)> work, std::function<void(const TASK::BackgroundResult&)> complete);
void checkBuildCancellation(const std::atomic_bool* cancellation);
}
