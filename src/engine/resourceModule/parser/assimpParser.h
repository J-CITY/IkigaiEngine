#pragma once
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
		// --- Model loading ---
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
		const unsigned int MAX_BONE_WEIGHTS = 4;
		void processMaterials(const struct aiScene* scene, std::vector<std::string>& materials);
		;
		void processNode(void* transform, struct aiNode* node, const struct aiScene* scene, RESOURCES::ResourcePtr<RENDER::ModelInterface> model,
			const std::unordered_map<std::string, int>& boneMapping);
		void processMesh(
			void* transform, struct aiMesh* mesh, const struct aiScene* scene, std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices);
		void importMorphAnimations(const struct aiScene* scene, RENDER::ModelInterface& model);
		void loadBones(std::vector<Vertex>& vertices, aiMesh* mesh, const aiScene* scene, RESOURCES::ResourcePtr<RENDER::ModelInterface> model,
			const std::unordered_map<std::string, int>& boneMapping);
		void setVertexBoneData(Vertex& vertex, int boneID, float weight);

		// Skeleton internal helpers (delegates to Skeleton methods)
		bool buildSkeleton(const aiScene* scene, SKELETON::Skeleton& outSkeleton);
		// Animation internal helpers
		bool fillAnimation(const aiScene* scene, SKELETON::Skeleton& skeleton, SKELETON::Animation& outAnimation, bool additive,
			SKELETON::Animation* additiveReference);
	};
}
}
