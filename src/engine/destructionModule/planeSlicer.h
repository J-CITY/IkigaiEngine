#pragma once
#include "collisionModule/convexPolyhedron.h"
#include "renderModule/vertex.h"
#include <string>

namespace IKIGAI::DESTRUCTION {
struct FractureMesh {
	std::vector<Vertex> vertices {};
	std::vector<unsigned> indices {};
	std::vector<unsigned> materials {};
};
struct SliceSettings {
	float relativeEpsilon = 1e-6f;
	float minimumVolume = 1e-6f;
	float interiorUvScale = 1;
	unsigned interiorMaterial = 0;
	unsigned maximumTriangles = 2048;
};
enum class SliceStatus { Split, NoSplit, UnsupportedMesh, TooSmall, BudgetExceeded, Busy, Stale, Cancelled };
struct SliceResult {
	SliceStatus status = SliceStatus::NoSplit;
	FractureMesh negative {};
	FractureMesh positive {};
	std::string error {};
};
float meshTolerance(const FractureMesh& mesh, float relativeEpsilon);
std::shared_ptr<const COLLISION::ConvexPolyhedron> validateMesh(const FractureMesh& mesh, const SliceSettings& settings);
SliceResult sliceMesh(const FractureMesh& mesh, MATH::Vector3f point, MATH::Vector3f normal, const SliceSettings& settings = {});
}
