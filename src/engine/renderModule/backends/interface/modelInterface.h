#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../../objects/boundingSphere.h"
#include "../../vertex.h"


namespace IKIGAI
{
	namespace RESOURCES {
		class ModelLoader;
		class AssimpParser;
	}
	namespace RENDER
	{
		class TextureInterface;
		class MeshInterface;

		struct BoneInfo {
			//id is index in finalBoneMatrices
			int mId = 0;
			//offset matrix transforms vertex from model space to bone space
			MATH::Matrix4f mOffset;
		};

		class ModelInterface {
		public:
			virtual ~ModelInterface() = default;

			[[nodiscard]] const IKIGAI::RENDER::BoundingSphere& getBoundingSphere() const;

			const std::string& getPath();
			void computeBoundingSphere();

			std::vector<std::string>& getMaterialsNames() {
				return mMaterialNames;
			}

			std::vector<std::shared_ptr<MeshInterface>>& getMeshes() {
				return mMeshes;
			}

			const std::vector<std::shared_ptr<MeshInterface>>& getMeshes() const {
				return mMeshes;
			}

			bool isBoneExist(const std::string& boneName) const {
				return mBoneInfoMap.contains(boneName);
			}

			size_t getBoneCounter() const {
				return mBoneCounter;
			}

			void addBone(const std::string& boneName, RENDER::BoneInfo& newBoneInfo) {
				mBoneInfoMap[boneName] = newBoneInfo;
				mBoneCounter++;
			}

			int getBoneId(const std::string& boneName) {
				return mBoneInfoMap[boneName].mId;
			}

			void setMeshes(const std::vector<std::shared_ptr<MeshInterface>>& meshes) {
				mMeshes = meshes;
			}

			void clearMeshes() {
				mMeshes.clear();
			}
			void setMaterialNames(const std::vector<std::string>& materialNames) {
				mMaterialNames = materialNames;
			}
			void setPath(const std::string path) {
				mPath = path;
			}
			auto& GetBoneInfoMap() { return mBoneInfoMap; }
			int& GetBoneCount() { return mBoneCounter; }

			void setUseBatching(bool b) {
				mUseBatching = b;
			}
			bool getUseBatching() const {
				return mUseBatching;
			}

			virtual void createBuffers(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {}

		protected:
			std::vector<std::string> mMaterialNames;
			std::vector<std::shared_ptr<MeshInterface>> mMeshes;
			BoundingSphere mBoundingSphere;
			std::string mPath;

			std::map<std::string, BoneInfo> mBoneInfoMap;
			int mBoneCounter = 0;
			const int MAX_BONE_WEIGHTS = 4;

			bool mUseBatching = false;
		};
	}
}
