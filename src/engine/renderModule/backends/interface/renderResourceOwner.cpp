#include "renderResourceOwner.h"
#include "driverInterface.h"
#include <stdexcept>

namespace IKIGAI::RENDER {
RenderResourceOwner::RenderResourceOwner()
	: mOwner(DriverInterface::resourceOwnerThread()) { }

void RenderResourceOwner::requireOwnerThread() const {
	if (mOwner != DriverInterface::resourceOwnerThread()) {
		throw std::logic_error("Render resource access from a non-owning thread");
	}
	(void)DriverInterface::Get();
}
}
