#include "fracturePreparation.h"
#include <map>

namespace IKIGAI::DESTRUCTION {
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
PreparedFracture prepareFracture(const FractureSnapshot& snapshot, const std::atomic<bool>* cancelled, const std::function<void()>& cooking) {
	PreparedFracture result {};
	result.source = snapshot.piece;
	result.revision = snapshot.revision;
	const auto started = std::chrono::steady_clock::now();
	try {
		auto cut = sliceMesh(snapshot.mesh, snapshot.point, snapshot.normal, snapshot.settings);
		const auto cooked = std::chrono::steady_clock::now();
		result.slicingMilliseconds = std::chrono::duration<double, std::milli>(cooked - started).count();
		result.status = cut.status;
		result.error = std::move(cut.error);
		if (cancelled && cancelled->load()) {
			result.status = SliceStatus::Cancelled;
			return result;
		}
		if (cooking) {
			cooking();
		}
		if (cut.status == SliceStatus::Split) {
			result.pieces[0].mesh = std::move(cut.negative);
			result.pieces[1].mesh = std::move(cut.positive);
			for (auto& piece : result.pieces) {
				piece.hull = centerMesh(piece.mesh, snapshot.settings, piece.center);
				piece.uploads = prepareUploads(piece.mesh);
			}
		}
		result.cookingMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cooked).count();
	} catch (const std::exception& error) {
		result.status = SliceStatus::UnsupportedMesh;
		result.error = error.what();
	}
	return result;
}
std::size_t meshBytes(const FractureMesh& mesh) {
	return mesh.vertices.size() * sizeof(Vertex) + mesh.indices.size() * sizeof(unsigned) + mesh.materials.size() * sizeof(unsigned);
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
