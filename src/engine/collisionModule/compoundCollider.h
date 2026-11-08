#pragma once
#include "collision3d.h"
#include <cstddef>
#include <span>
#include <vector>

namespace IKIGAI::COLLISION {

// A flat list of rigidly placed leaves. The authored order is their stable identity.
class CompoundCollider3D {
public:
	struct Child {
		Shape3D shape {};
		Vec3 position {};
		std::array<Vec3, 3> axes { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
	};

	static constexpr std::size_t MaxChildren = 32;
	explicit CompoundCollider3D(std::vector<Child> children);
	std::span<const Child> children() const;

private:
	const std::vector<Child> mChildren;
};

// A non-compound shape is one leaf with index zero. Views borrow their shape's storage.
std::size_t compoundLeafCount(const ShapeView3D& shape);
ShapeView3D compoundLeafView(const ShapeView3D& shape, std::size_t child);

struct LeafManifold3D {
	std::size_t childA = 0;
	std::size_t childB = 0;
	Manifold3D manifold {};
};

struct ManifoldCollection3D {
	Status status = Status::Separated;
	std::size_t count = 0;
};

// Results are ordered by (childA, childB). On Overflow or Unsupported, the
// count is zero and any partially written output must be discarded.
ManifoldCollection3D collectManifolds(
	const INarrowPhase3D& narrowPhase, const ShapeView3D& a, const ShapeView3D& b, std::span<LeafManifold3D> output);

}
