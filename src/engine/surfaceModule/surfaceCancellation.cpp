#include "surfaceCancellation.h"

namespace IKIGAI::SURFACE {
SurfaceBuildCancelled::SurfaceBuildCancelled()
	: std::runtime_error("Surface build cancelled") { }

void checkBuildCancellation(const std::atomic_bool* cancellation) {
	if (cancellation && cancellation->load()) {
		throw SurfaceBuildCancelled();
	}
}
}
