#pragma once
#include "planeSlicer.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>

namespace IKIGAI::DESTRUCTION {
struct FractureSnapshot {
	std::uint64_t piece = 0;
	std::uint64_t revision = 0;
	FractureMesh mesh {};
	SliceSettings settings {};
	MATH::Vector3f point {};
	MATH::Vector3f normal { 1, 0, 0 };
};
struct PreparedPiece {
	FractureMesh mesh {};
	std::shared_ptr<const COLLISION::ConvexPolyhedron> hull {};
	MATH::Vector3f center {};
	struct UploadBatch {
		unsigned material = 0;
		std::vector<Vertex> vertices {};
		std::vector<unsigned> indices {};
	};
	std::vector<UploadBatch> uploads {};
};
struct PreparedFracture {
	std::uint64_t source = 0;
	std::uint64_t revision = 0;
	SliceStatus status = SliceStatus::NoSplit;
	std::array<PreparedPiece, 2> pieces {};
	std::string error {};
	double slicingMilliseconds = 0;
	double cookingMilliseconds = 0;
};
std::shared_ptr<const COLLISION::ConvexPolyhedron> centerMesh(FractureMesh& mesh, const SliceSettings& settings, MATH::Vector3f& center);
PreparedFracture prepareFracture(
	const FractureSnapshot& snapshot, const std::atomic<bool>* cancelled = nullptr, const std::function<void()>& cooking = {});
std::size_t meshBytes(const FractureMesh& mesh);
std::vector<PreparedPiece::UploadBatch> prepareUploads(const FractureMesh& mesh);
const char* sliceStatusName(SliceStatus status);
}
