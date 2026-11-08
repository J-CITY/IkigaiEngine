#include "fracturePreparation.h"
#include "collisionModule/compoundCollider.h"
#include <cmath>
#include <map>
#include <stdexcept>

namespace IKIGAI::DESTRUCTION {
bool validBoxCutout(const BoxCutout& box) {
	const auto length = MATH::QuaternionF::Length(box.orientation);
	if (!std::isfinite(length) || std::abs(length - 1.f) > 1e-4f) {
		return false;
	}
	for (unsigned axis = 0; axis < 3; ++axis) {
		if (!std::isfinite(box.halfExtents[axis]) || box.halfExtents[axis] < 1e-5f || box.halfExtents[axis] > 1e4f) {
			return false;
		}
	}
	return true;
}
std::shared_ptr<const COLLISION::ConvexPolyhedron> centerMesh(FractureMesh& mesh, const SliceSettings& settings, MATH::Vector3f& center) {
	// Translation preserves volume, face normals and inertia about COM. Cook only once.
	auto hull = std::make_shared<COLLISION::ConvexPolyhedron>(*validateMesh(mesh, settings));
	center = hull->center;
	for (auto& vertex : mesh.vertices) {
		vertex.position -= center;
	}
	for (auto& vertex : hull->vertices) {
		vertex -= center;
	}
	for (auto& face : hull->faces) {
		face.offset -= face.normal.dot(center);
	}
	hull->center = {};
	return hull;
}
PieceGeometry prepareGeometry(FractureMesh& mesh, const SliceSettings& settings, const std::optional<ConcaveSettings>& concave) {
	PieceGeometry piece {};
	if (!concave) {
		auto hull = centerMesh(mesh, settings, piece.center);
		piece.volume = hull->volume;
		piece.inertia = hull->inertia;
		piece.shape = COLLISION::Shape3D::convex(std::move(hull));
		return piece;
	}
	const auto cooked = cookConcaveMesh(mesh, *concave);
	piece.center = cooked.surface.center;
	piece.volume = cooked.surface.volume;
	piece.inertia = cooked.surface.inertia;
	std::vector<COLLISION::CompoundCollider3D::Child> children {};
	children.reserve(cooked.children.size());
	for (const auto& hull : cooked.children) {
		children.push_back({ COLLISION::Shape3D::convex(hull), -piece.center });
	}
	piece.shape = COLLISION::Shape3D::compound(std::make_shared<const COLLISION::CompoundCollider3D>(std::move(children)));
	for (auto& vertex : mesh.vertices) {
		vertex.position -= piece.center;
	}
	return piece;
}
PreparedFracture prepareFracture(const FractureSnapshot& snapshot, const std::atomic<bool>* cancelled, const std::function<void()>& cooking) {
	PreparedFracture result {};
	result.source = snapshot.piece;
	result.revision = snapshot.revision;
	const auto started = std::chrono::steady_clock::now();
	try {
		std::vector<FractureMesh> meshes {};
		std::vector<bool> positiveSide {};
		if (snapshot.concave) {
			ConcaveResult cut {};
			if (snapshot.cutout) {
				const auto& box = *snapshot.cutout;
				if (!validBoxCutout(box)) {
					throw std::invalid_argument("Cutout requires bounded half extents and a unit orientation");
				}
				std::array<CutPlane, 6> planes {};
				for (unsigned axis = 0; axis < 3; ++axis) {
					MATH::Vector3f unit {};
					unit[axis] = 1.f;
					const auto normal = box.orientation * unit;
					const auto offset = normal * box.halfExtents[axis];
					planes[axis * 2] = { snapshot.point + offset, normal };
					planes[axis * 2 + 1] = { snapshot.point - offset, -normal };
				}
				cut = subtractConvexCutter(snapshot.mesh, planes, *snapshot.concave);
			}
			else {
				cut = sliceConcaveMesh(snapshot.mesh, snapshot.point, snapshot.normal, *snapshot.concave);
			}
			result.status = cut.status;
			result.error = std::move(cut.error);
			meshes = std::move(cut.remaining);
			positiveSide = std::move(cut.positiveSide);
		}
		else if (snapshot.cutout) {
			throw std::invalid_argument("Cutout requires Concave destructible geometry");
		}
		else {
			auto cut = sliceMesh(snapshot.mesh, snapshot.point, snapshot.normal, snapshot.settings);
			result.status = cut.status;
			result.error = std::move(cut.error);
			if (cut.status == SliceStatus::Split) {
				meshes.push_back(std::move(cut.negative));
				meshes.push_back(std::move(cut.positive));
				positiveSide = { false, true };
			}
		}
		const auto cooked = std::chrono::steady_clock::now();
		result.slicingMilliseconds = std::chrono::duration<double, std::milli>(cooked - started).count();
		if (cancelled && cancelled->load()) {
			result.status = SliceStatus::Cancelled;
			return result;
		}
		if (cooking) {
			cooking();
		}
		if (result.status == SliceStatus::Split) {
			if (!snapshot.cutout && positiveSide.size() != meshes.size()) {
				throw std::logic_error("Slice output missing side metadata");
			}
			result.pieces.reserve(meshes.size());
			auto concave = snapshot.concave;
			unsigned remainingChildTriangles = concave ? concave->slicing.maximumTriangles * 4 : 0;
			for (std::size_t index = 0; index < meshes.size(); ++index) {
				if (cancelled && cancelled->load()) {
					result.status = SliceStatus::Cancelled;
					result.pieces.clear();
					return result;
				}
				PreparedPiece piece {};
				piece.mesh = std::move(meshes[index]);
				piece.positiveSide = !snapshot.cutout && positiveSide[index];
				if (concave) {
					if (!remainingChildTriangles) {
						throw std::length_error("Fracture exceeds aggregate convex child geometry budget");
					}
					concave->maximumChildTriangles = remainingChildTriangles;
				}
				piece.geometry = prepareGeometry(piece.mesh, snapshot.settings, concave);
				if (concave) {
					for (const auto& child : piece.geometry.shape.compoundCollider->children()) {
						remainingChildTriangles -= static_cast<unsigned>(child.shape.polyhedron->triangles.size());
					}
				}
				piece.uploads = prepareUploads(piece.mesh);
				result.pieces.push_back(std::move(piece));
			}
		}
		result.cookingMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cooked).count();
	} catch (const std::length_error& error) {
		result.status = SliceStatus::BudgetExceeded;
		result.error = error.what();
		result.pieces.clear();
	} catch (const std::exception& error) {
		result.status = SliceStatus::UnsupportedMesh;
		result.error = error.what();
		result.pieces.clear();
	}
	return result;
}
std::size_t meshBytes(const FractureMesh& mesh) {
	return mesh.vertices.size() * sizeof(Vertex) + mesh.indices.size() * sizeof(unsigned) + mesh.materials.size() * sizeof(unsigned);
}
std::size_t pieceBytes(const FractureMesh& mesh, const COLLISION::Shape3D& shape) {
	auto bytes = meshBytes(mesh);
	if (shape.kind == COLLISION::Shape3D::Kind::Compound) {
		bytes += sizeof(COLLISION::CompoundCollider3D);
		for (const auto& child : shape.compoundCollider->children()) {
			const auto& hull = *child.shape.polyhedron;
			bytes += sizeof(child) + sizeof(hull) + hull.vertices.capacity() * sizeof(hull.vertices.front())
				+ hull.triangles.capacity() * sizeof(hull.triangles.front()) + hull.faces.capacity() * sizeof(hull.faces.front())
				+ hull.edges.capacity() * sizeof(hull.edges.front());
			for (const auto& face : hull.faces) {
				bytes += face.vertices.capacity() * sizeof(unsigned);
			}
		}
	}
	return bytes;
}
std::vector<PreparedPiece::UploadBatch> prepareUploads(const FractureMesh& mesh) {
	std::map<unsigned, std::vector<unsigned>> groups {};
	for (std::size_t triangle = 0; triangle < mesh.materials.size(); ++triangle) {
		groups[mesh.materials[triangle]].push_back(unsigned(triangle));
	}
	std::vector<PreparedPiece::UploadBatch> result {};
	for (const auto& [material, triangles] : groups) {
		// Even completely unshared vertices fit one driver call within the minimum upload budget.
		constexpr std::size_t trianglesPerBatch = 65536 / (3 * (sizeof(Vertex) + sizeof(unsigned)));
		for (std::size_t start = 0; start < triangles.size(); start += trianglesPerBatch) {
			PreparedPiece::UploadBatch batch {};
			batch.material = material;
			std::map<unsigned, unsigned> remap {};
			for (std::size_t index = start; index < std::min(start + trianglesPerBatch, triangles.size()); ++index) {
				for (unsigned corner = 0; corner < 3; ++corner) {
					const auto source = mesh.indices[triangles[index] * 3 + corner];
					auto [entry, inserted] = remap.try_emplace(source, unsigned(batch.vertices.size()));
					if (inserted) {
						batch.vertices.push_back(mesh.vertices[source]);
					}
					batch.indices.push_back(entry->second);
				}
			}
			result.push_back(std::move(batch));
		}
	}
	return result;
}
const char* sliceStatusName(SliceStatus status) {
	switch (status) {
	case SliceStatus::Split:
		return "Split";
	case SliceStatus::NoSplit:
		return "NoSplit";
	case SliceStatus::UnsupportedMesh:
		return "UnsupportedMesh";
	case SliceStatus::TooSmall:
		return "TooSmall";
	case SliceStatus::BudgetExceeded:
		return "BudgetExceeded";
	case SliceStatus::Busy:
		return "Busy";
	case SliceStatus::Stale:
		return "Stale";
	case SliceStatus::Cancelled:
		return "Cancelled";
	}
	return "Unknown";
}
}
