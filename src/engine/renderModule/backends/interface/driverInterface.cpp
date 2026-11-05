#include "driverInterface.h"
#include <stdexcept>

namespace IKIGAI::RENDER {
DriverInterface* DriverInterface::Get() {
	if (sActive) {
		sActive->prepareAccess();
	}
	return sActive;
}

void DriverInterface::prepareAccess() {
	requireOwnerThread();
	if (mAccessBoundary) {
		mAccessBoundary();
	}
}

void DriverInterface::waitForCompletion(GpuCompletionToken token) {
	requireOwnerThread();
	mCompletion->validate(token);
	if (!mCompletion->isComplete(token)) {
		throw std::logic_error("Backend does not implement GPU completion waits");
	}
}
}
