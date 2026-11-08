#pragma once
#include "collisionModule/collision3d.h"
#include "concaveDestruction.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

namespace IKIGAI::DESTRUCTION {
struct BoxCutout {
	MATH::Vector3f halfExtents {};
	MATH::QuaternionF orientation { 0, 0, 0, 1 };
};
bool validBoxCutout(const BoxCutout& box);
struct FractureSnapshot {
	std::uint64_t piece = 0;
	std::uint64_t revision = 0;
	FractureMesh mesh {};
	SliceSettings settings {};
	MATH::Vector3f point {};
	MATH::Vector3f normal { 1, 0, 0 };
	std::optional<ConcaveSettings> concave {};
	std::optional<BoxCutout> cutout {};
};
struct PieceGeometry {
	COLLISION::Shape3D shape {};
	MATH::Vector3f center {};
	MATH::Matrix3f inertia { 0.f };
	double volume = 0;
};
struct PreparedPiece {
	FractureMesh mesh {};
	PieceGeometry geometry {};
	bool positiveSide = false;
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
	std::vector<PreparedPiece> pieces {};
	std::string error {};
	double slicingMilliseconds = 0;
	double cookingMilliseconds = 0;
};
std::shared_ptr<const COLLISION::ConvexPolyhedron> centerMesh(FractureMesh& mesh, const SliceSettings& settings, MATH::Vector3f& center);
PieceGeometry prepareGeometry(FractureMesh& mesh, const SliceSettings& settings, const std::optional<ConcaveSettings>& concave);
PreparedFracture prepareFracture(
	const FractureSnapshot& snapshot, const std::atomic<bool>* cancelled = nullptr, const std::function<void()>& cooking = {});
std::size_t meshBytes(const FractureMesh& mesh);
std::size_t pieceBytes(const FractureMesh& mesh, const COLLISION::Shape3D& shape);
std::vector<PreparedPiece::UploadBatch> prepareUploads(const FractureMesh& mesh);
const char* sliceStatusName(SliceStatus status);
}
