#pragma once
#include <atomic>
#include <stdexcept>

namespace IKIGAI::SURFACE {
class SurfaceBuildCancelled : public std::runtime_error {
public:
	SurfaceBuildCancelled();
};

void checkBuildCancellation(const std::atomic_bool* cancellation);
}
