#include "surfaceJob.h"
#include <mutex>
#include <stdexcept>

namespace IKIGAI::SURFACE {
struct BackgroundJob::State {
	std::atomic_bool Cancelled = false;
	std::atomic_bool Ready = false;
	std::mutex Mutex = {};
	std::any Result = {};
	std::exception_ptr Error = {};
};
BackgroundJob::BackgroundJob(std::function<std::any(const std::atomic_bool&)> work)
	: mState(std::make_shared<State>()) {
	dispatchSurfaceBuild([state = mState, work = std::move(work)] {
		try {
			if (!state->Cancelled.load()) {
				auto result = work(state->Cancelled);
				std::lock_guard lock(state->Mutex);
				if (!state->Cancelled.load()) {
					state->Result = std::move(result);
				}
			}
		} catch (...) {
			std::lock_guard lock(state->Mutex);
			state->Error = std::current_exception();
		}
		state->Ready.store(true);
	});
}
BackgroundJob::~BackgroundJob() {
	cancel();
}
bool BackgroundJob::ready() const {
	return mState->Ready.load();
}
void BackgroundJob::cancel() {
	mState->Cancelled.store(true);
}
std::any BackgroundJob::take() {
	if (!ready()) {
		throw std::logic_error("Surface job is not ready");
	}
	std::lock_guard lock(mState->Mutex);
	if (mState->Error) {
		std::rethrow_exception(mState->Error);
	}
	return std::move(mState->Result);
}
void checkBuildCancellation(const std::atomic_bool* cancellation) {
	if (cancellation && cancellation->load()) {
		throw std::runtime_error("Surface build cancelled");
	}
}
}
