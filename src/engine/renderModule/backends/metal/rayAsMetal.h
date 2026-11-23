#pragma once

#if defined(METAL_BACKEND) && USE_RAY_TRACING

#include "../interface/rayTracingInterface.h"
#import <Metal/Metal.h>

namespace IKIGAI::RENDER {

class DriverMetal;

class AccelerationStructureMetal final : public AccelerationStructure {
public:
	id<MTLAccelerationStructure> getHandle() const { return mHandle; }
	id<MTLDevice> getDevice() const { return mHandle.device; }
	bool isTopLevel() const { return mTopLevel; }
	bool belongsTo(const DriverMetal& driver) const { return mDriver == &driver; }
	NSArray<id<MTLAccelerationStructure>>* getReferencedBottomLevels() const {
		return mTopLevel ? ((MTLInstanceAccelerationStructureDescriptor*)mDescriptor).instancedAccelerationStructures : nil;
	}

private:
	friend class DriverMetal;

	const DriverMetal* mDriver = nullptr;
	id<MTLAccelerationStructure> mHandle = nil;
	MTLAccelerationStructureDescriptor* mDescriptor = nil;
	std::vector<RayTracingGeometry> mSource;
	bool mTopLevel = false;
	bool mAllowUpdate = false;
};

}

#endif
