#pragma once
#include <any>
#include <atomic>
#include <functional>
#include <memory>

namespace IKIGAI::SURFACE {
class BackgroundJob {
public:
	explicit BackgroundJob(std::function<std::any(const std::atomic_bool&)> work);
	~BackgroundJob();
	BackgroundJob(const BackgroundJob&) = delete;
	BackgroundJob& operator=(const BackgroundJob&) = delete;
	bool ready() const;
	std::any take();
	void cancel();

private:
	struct State;
	std::shared_ptr<State> mState = {};
};
// Uses the engine's worker pool. Kept separate from the result/cancellation state.
void dispatchSurfaceBuild(std::function<void()> work);
void checkBuildCancellation(const std::atomic_bool* cancellation);
}
