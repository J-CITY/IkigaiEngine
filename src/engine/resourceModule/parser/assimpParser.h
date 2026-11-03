#pragma once
#include "importedModel.h"
#include "parseFlags.h"
#include <assimp/scene.h>
#include <renderModule/backends/interface/modelInterface.h>
#include <unordered_map>
#include <vector>


namespace IKIGAI::SKELETON {
class Skeleton;
struct Animation;
}

namespace IKIGAI::RESOURCES {
template <typename T> using ResourcePtr = std::shared_ptr<T>;

}

namespace IKIGAI {
class Vertex;

namespace RENDER {
	class Model;
}
namespace RESOURCES {
	class AssimpParser {
	public:
		// --- Model import, CPU stage ---
		// Reentrant: reads the scene into plain data and never touches renderer state.
		// The in-memory overload resolves sidecar files through the engine VFS.
		static bool ImportModel(const std::string& fileName, ModelParserFlags parserFlags, ImportedModel& outModel);
		static bool ImportModel(const std::string& fileName, const std::vector<uint8_t>& data, ModelParserFlags parserFlags, ImportedModel& outModel);

		// --- Model import, GPU stage ---
		// Main/render thread only: creates meshes through RENDER::CreateMesh and fills
		// the model. Consumes the morph targets of imported.
		static void CreateModelResources(ImportedModel&& imported, RENDER::ModelInterface& model, const ModelBuildOptions& options = {});

		// --- Model loading (CPU stage followed by GPU stage) ---
		bool LoadModel(const std::string& fileName, RESOURCES::ResourcePtr<RENDER::ModelInterface> model, ModelParserFlags parserFlags);
		bool LoadModel(const std::string& fileName, const std::vector<uint8_t>& data, RESOURCES::ResourcePtr<RENDER::ModelInterface> model,
			ModelParserFlags parserFlags);
		bool LoadVertexes(const std::string& fileName, RESOURCES::ResourcePtr<RENDER::ModelInterface> model, ModelParserFlags parserFlags,
			std::vector<std::vector<Vertex>>& globalVerticesPerMesh, std::vector<std::vector<uint32_t>>& globalIndicesPerMesh);
		bool LoadVertexes(const std::string& fileName, const std::vector<uint8_t>& data, RESOURCES::ResourcePtr<RENDER::ModelInterface> model,
			ModelParserFlags parserFlags, std::vector<std::vector<Vertex>>& globalVerticesPerMesh,
			std::vector<std::vector<uint32_t>>& globalIndicesPerMesh);

		// --- Skeleton loading ---
		bool LoadSkeleton(const std::string& fileName, SKELETON::Skeleton& outSkeleton);
		bool LoadSkeleton(const std::string& fileName, const std::vector<uint8_t>& data, SKELETON::Skeleton& outSkeleton);

		// --- Animation loading ---
		bool LoadAnimation(const std::string& fileName, SKELETON::Skeleton& skeleton, SKELETON::Animation& outAnimation, bool additive = false,
			SKELETON::Animation* additiveReference = nullptr);
		bool LoadAnimation(const std::string& fileName, const std::vector<uint8_t>& data, SKELETON::Skeleton& skeleton,
			SKELETON::Animation& outAnimation, bool additive = false, SKELETON::Animation* additiveReference = nullptr);

	private:
		static bool ReadImportedModel(const aiScene* scene, ImportedModel& outModel);
		// Skeleton internal helpers (delegates to Skeleton methods)
		static bool buildSkeleton(const aiScene* scene, SKELETON::Skeleton& outSkeleton);
		// Animation internal helpers
		bool fillAnimation(const aiScene* scene, SKELETON::Skeleton& skeleton, SKELETON::Animation& outAnimation, bool additive,
			SKELETON::Animation* additiveReference);
	};
}
}
