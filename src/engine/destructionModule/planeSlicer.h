#pragma once
#include "collisionModule/convexPolyhedron.h"
#include "mathModule/math.h"
#include <array>
#include <string>

namespace IKIGAI::DESTRUCTION {
struct FractureVertex {
	MATH::Vector3f position {};
	MATH::Vector2f texCoord {};
	MATH::Vector3f normal {};
	MATH::Vector3f tangent {};
	MATH::Vector3f bitangent {};
	std::array<int, 4> boneIds { -1, -1, -1, -1 };
	MATH::Vector4f boneWeights {};
};
struct FractureMesh {
	std::vector<FractureVertex> vertices {};
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
