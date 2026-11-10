#include "resourceModule/serviceManager.h"
#include "surfaceJob.h"
#include "taskModule/taskSystem.h"

namespace IKIGAI::SURFACE {
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
