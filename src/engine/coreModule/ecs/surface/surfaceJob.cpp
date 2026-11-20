#include "surfaceJob.h"
#include "surfaceModule/surfaceCancellation.h"
#include "taskModule/taskSystem.h"
#include "utilsModule/serviceManager.h"
#include <mutex>
#include <stdexcept>

namespace IKIGAI::SURFACE {
namespace {
	void dispatchSurfaceBuild(std::function<void(std::stop_token)> work, std::function<void(const TASK::BackgroundResult&)> complete) {
		if (!RESOURCES::ServiceManager::Check<TASK::TaskSystem>()) {
			complete({ TASK::BackgroundOutcome::Rejected, TASK::BackgroundRejection::Unavailable });
			return;
		}
		auto& tasks = RESOURCES::ServiceManager::Get<TASK::TaskSystem>();
		auto ticket = tasks.dispatch("Surface build", std::move(work), complete);
		if (!ticket.accepted()) {
			complete(ticket.get());
		}
	}
}
struct BackgroundJob::State {
	std::atomic_bool Cancelled = false;
	std::atomic_bool Ready = false;
	std::mutex Mutex = {};
	std::any Result = {};
	TASK::BackgroundResult Completion = {};
};
BackgroundJob::BackgroundJob(std::function<std::any(const std::atomic_bool&)> work)
	: mState(std::make_shared<State>()) {
	dispatchSurfaceBuild(
		[state = mState, work = std::move(work)](std::stop_token stopToken) {
			const std::stop_callback stopCallback(stopToken, [&] { state->Cancelled.store(true); });
			try {
				if (!state->Cancelled.load()) {
					state->Result = work(state->Cancelled);
				}
			} catch (const SurfaceBuildCancelled&) {
				state->Cancelled.store(true);
			}
		},
		[state = mState](const TASK::BackgroundResult& completion) {
			state->Completion = completion;
			if (completion.Outcome == TASK::BackgroundOutcome::Succeeded && state->Cancelled.load()) {
				state->Completion.Outcome = TASK::BackgroundOutcome::Cancelled;
			}
			if (state->Completion.Outcome != TASK::BackgroundOutcome::Succeeded) {
				state->Result.reset();
			}
			state->Ready.store(true, std::memory_order_release);
		});
}
BackgroundJob::~BackgroundJob() {
	cancel();
}
bool BackgroundJob::ready() const {
	return mState->Ready.load(std::memory_order_acquire);
}
bool BackgroundJob::failed() const {
	return ready() && mState->Completion.Outcome != TASK::BackgroundOutcome::Succeeded;
}
void BackgroundJob::cancel() {
	mState->Cancelled.store(true);
}
std::any BackgroundJob::take() {
	if (!ready()) {
		throw std::logic_error("Surface job is not ready");
	}
	const auto& completion = mState->Completion;
	switch (completion.Outcome) {
	case TASK::BackgroundOutcome::Succeeded: {
		std::lock_guard lock(mState->Mutex);
		return std::move(mState->Result);
	}
	case TASK::BackgroundOutcome::Rejected:
		throw std::runtime_error(TASK::backgroundRejectionMessage(completion.Rejection));
	case TASK::BackgroundOutcome::Cancelled:
		throw SurfaceBuildCancelled();
	case TASK::BackgroundOutcome::Failed:
		if (completion.Exception) {
			try {
				std::rethrow_exception(completion.Exception);
			} catch (const std::exception&) {
				throw;
			} catch (...) { }
		}
		break;
	}
	throw std::runtime_error("Surface build failed");
}
}
