#pragma once
#include "collisionModule/closedSurfaceMesh.h"
#include "planeSlicer.h"

namespace IKIGAI::DESTRUCTION {
struct ConcaveSettings {
	// Also caps total output triangles across all pieces of one cut.
	SliceSettings slicing {};
	unsigned maximumPieces = 32;
	unsigned maximumConvexChildren = 32;
	unsigned maximumChildTriangles = 0; // 0 uses four times slicing.maximumTriangles.
};
struct ConcaveGeometry {
	COLLISION::ClosedSurfaceMesh surface {};
	std::vector<std::shared_ptr<const COLLISION::ConvexPolyhedron>> children {};
};
struct CutPlane {
	MATH::Vector3f point {};
	MATH::Vector3f normal {};
};
struct ConcaveResult {
	SliceStatus status = SliceStatus::NoSplit;
	std::vector<FractureMesh> remaining {};
	std::vector<FractureMesh> removed {};
	std::vector<bool> positiveSide {};
	std::string error {};
};

// Bounded solid cooker for one closed outward-wound shell. Collision cells partition
// the surface volume within the configured tolerance and use the source mesh axes.
ConcaveGeometry cookConcaveMesh(const FractureMesh& mesh, const ConcaveSettings& settings = {});

// The negative-side components precede the positive-side components in remaining.
ConcaveResult sliceConcaveMesh(const FractureMesh& mesh, MATH::Vector3f point, MATH::Vector3f normal, const ConcaveSettings& settings = {});

// Cutter interior lies on the negative side of each of its 4 to 12 outward-facing planes.
// Fully enclosed cavities need nested shell support and are rejected.
ConcaveResult subtractConvexCutter(const FractureMesh& mesh, std::span<const CutPlane> cutter, const ConcaveSettings& settings = {});
}
