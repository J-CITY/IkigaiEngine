#include "compoundCollider.h"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace IKIGAI::COLLISION {
namespace {
	using Aabb = MATH::GEOMETRY::Aabb<3>;

	bool finite(Vec3 value) {
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	std::vector<CompoundCollider3D::Child> validateChildren(std::vector<CompoundCollider3D::Child> children) {
		if (children.empty() || children.size() > CompoundCollider3D::MaxChildren) {
			throw std::invalid_argument("Invalid compound child count");
		}
		for (auto& child : children) {
			switch (child.shape.kind) {
			case Shape3D::Kind::Sphere:
				child.shape = Shape3D::sphere(child.shape.radius);
				break;
			case Shape3D::Kind::Box:
				child.shape = Shape3D::box(child.shape.halfSize);
				break;
			case Shape3D::Kind::Convex:
				child.shape = Shape3D::convex(child.shape.polyhedron);
				if (!std::isfinite(child.shape.polyhedron->volume) || child.shape.polyhedron->vertices.size() < 4
					|| !std::all_of(child.shape.polyhedron->vertices.begin(), child.shape.polyhedron->vertices.end(), finite)) {
					throw std::invalid_argument("Invalid compound convex hull");
				}
				break;
			default:
				throw std::invalid_argument("A compound child must be a sphere, box or convex hull");
			}
			auto& x = child.axes[0];
			auto& y = child.axes[1];
			auto& z = child.axes[2];
			constexpr float rotationTolerance = 1e-4f;
			if (!finite(child.position) || !finite(x) || !finite(y) || !finite(z) || !(std::abs(x.dot(x) - 1.f) <= rotationTolerance)
				|| !(std::abs(y.dot(y) - 1.f) <= rotationTolerance) || !(std::abs(z.dot(z) - 1.f) <= rotationTolerance)
				|| !(std::abs(x.dot(y)) <= rotationTolerance) || !(std::abs(x.dot(z)) <= rotationTolerance)
				|| !(std::abs(y.dot(z)) <= rotationTolerance) || !(std::abs(x.cross(y).dot(z) - 1.f) <= rotationTolerance)) {
				throw std::invalid_argument("A compound child requires a finite rigid pose");
			}
			// Remove accumulated floating-point drift after rejecting scale, shear and reflections.
			x = x / x.magnitude();
			y = y - x * x.dot(y);
			y = y / y.magnitude();
			z = x.cross(y);
			bounds({ child.shape, child.position, child.axes }).validate();
		}
		return children;
	}

	std::span<const CompoundCollider3D::Child> compoundChildren(const ShapeView3D& shape) {
		if (!shape.shape.compoundCollider) {
			throw std::invalid_argument("Missing compound collider");
		}
		return shape.shape.compoundCollider->children();
	}
}

CompoundCollider3D::CompoundCollider3D(std::vector<Child> children)
	: mChildren(validateChildren(std::move(children))) { }

std::span<const CompoundCollider3D::Child> CompoundCollider3D::children() const {
	return mChildren;
}

Shape3D Shape3D::compound(std::shared_ptr<const CompoundCollider3D> compoundCollider) {
	if (!compoundCollider) {
		throw std::invalid_argument("Missing compound collider");
	}
	Shape3D shape {};
	shape.kind = Kind::Compound;
	shape.compoundCollider = std::move(compoundCollider);
	return shape;
}

std::size_t compoundLeafCount(const ShapeView3D& shape) {
	return shape.shape.kind == Shape3D::Kind::Compound ? compoundChildren(shape).size() : 1;
}

ShapeView3D compoundLeafView(const ShapeView3D& shape, std::size_t child) {
	if (child >= compoundLeafCount(shape)) {
		throw std::out_of_range("Compound child index out of range");
	}
	if (shape.shape.kind != Shape3D::Kind::Compound) {
		return shape;
	}
	const auto& leaf = compoundChildren(shape)[child];
	ShapeView3D view { leaf.shape, shape.world(leaf.position) };
	for (std::size_t axis = 0; axis < view.axes.size(); ++axis) {
		const auto local = leaf.axes[axis];
		view.axes[axis] = shape.axes[0] * local.x + shape.axes[1] * local.y + shape.axes[2] * local.z;
	}
	return view;
}

ManifoldCollection3D collectManifolds(
	const INarrowPhase3D& narrowPhase, const ShapeView3D& a, const ShapeView3D& b, std::span<LeafManifold3D> output) {
	const auto countA = compoundLeafCount(a);
	const auto countB = compoundLeafCount(b);
	const bool cullLeafPairs = a.shape.kind == Shape3D::Kind::Compound || b.shape.kind == Shape3D::Kind::Compound;
	std::array<Aabb, CompoundCollider3D::MaxChildren> boundsB {};
	if (cullLeafPairs) {
		for (std::size_t childB = 0; childB < countB; ++childB) {
			boundsB[childB] = bounds(compoundLeafView(b, childB));
		}
	}
	std::size_t count = 0;
	for (std::size_t childA = 0; childA < countA; ++childA) {
		const auto leafA = compoundLeafView(a, childA);
		const auto boundsA = cullLeafPairs ? bounds(leafA) : Aabb {};
		for (std::size_t childB = 0; childB < countB; ++childB) {
			if (cullLeafPairs && !boundsA.overlaps(boundsB[childB])) {
				continue;
			}
			Manifold3D manifold {};
			const auto status = narrowPhase.collide(leafA, compoundLeafView(b, childB), manifold);
			if (status == Status::Separated) {
				continue;
			}
			if (status != Status::Contact) {
				return { status, 0 };
			}
			if (count == output.size()) {
				return { Status::Overflow, 0 };
			}
			output[count++] = { childA, childB, manifold };
		}
	}
	return { count == 0 ? Status::Separated : Status::Contact, count };
}

}
