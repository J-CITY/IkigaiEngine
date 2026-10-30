#include "resourceModule/serviceManager.h"
#include "surfaceJob.h"
#include "taskModule/taskSystem.h"

namespace IKIGAI::SURFACE {
void dispatchSurfaceBuild(std::function<void()> work) {
	if (RESOURCES::ServiceManager::Check<TASK::TaskSystem>() && RESOURCES::ServiceManager::Get<TASK::TaskSystem>().isReady()) {
		RESOURCES::ServiceManager::Get<TASK::TaskSystem>().dispatch("Surface build", std::move(work));
	}
	else {
		work();
	}
}
}
