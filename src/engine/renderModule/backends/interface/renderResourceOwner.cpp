#include "renderResourceOwner.h"
#include "driverInterface.h"

namespace IKIGAI::RENDER {
void RenderResourceOwner::requireOwnerThread() const {
	mOwner.requireCurrentThread();
	(void)DriverInterface::Get();
}
}
