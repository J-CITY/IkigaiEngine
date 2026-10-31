#include "assimpParser.h"

#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "skeletalModule/animation.h"
#include "skeletalModule/skeleton.h"
#include "utilsModule/log/loggerDefine.h"
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/matrix4x4.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <renderModule/backends/interface/driverInterface.h>
#include <renderModule/backends/interface/modelInterface.h>
#include <renderModule/backends/meshFactory.h>
#include <renderModule/vertex.h>
#include <unordered_map>
#include <unordered_set>

using namespace IKIGAI;
using namespace IKIGAI::RESOURCES;

namespace {
// Keep the top-level asynchronous read, and resolve sidecars through the same
// VFS mount (native, ZIP, SDL or Android) instead of Assimp's host filesystem.
class AssetStream final : public Assimp::IOStream {
	std::vector<uint8_t> bytes;
	size_t position = 0;

public:
	explicit AssetStream(std::vector<uint8_t> data)
		: bytes(std::move(data)) { }
	size_t Read(void* output, size_t size, size_t count) override {
		if (!size) {
			return 0;
		}
		count = std::min(count, (bytes.size() - position) / size);
		if (count) {
			std::memcpy(output, bytes.data() + position, count * size);
		}
		position += count * size;
		return count;
	}
	size_t Write(const void*, size_t, size_t) override {
		return 0;
	}
	aiReturn Seek(size_t offset, aiOrigin origin) override {
		size_t next = offset;
		if (origin == aiOrigin_CUR) {
			if (offset > bytes.size() - position) {
				return aiReturn_FAILURE;
			}
			next = position + offset;
		}
		else if (origin == aiOrigin_END) {
			if (offset > bytes.size()) {
				return aiReturn_FAILURE;
			}
			next = bytes.size() - offset;
		}
		if (next > bytes.size()) {
			return aiReturn_FAILURE;
		}
		position = next;
		return aiReturn_SUCCESS;
	}
	size_t Tell() const override {
		return position;
	}
	size_t FileSize() const override {
		return bytes.size();
	}
	void Flush() override { }
};
class AssetIO final : public Assimp::IOSystem {
	std::string root;
	const std::vector<uint8_t>& bytes;
	std::string resolve(const char* name) const {
		auto path = std::filesystem::path(name).lexically_normal().generic_string();
		if (path == root || RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().isFileExist(path)) {
			return path;
		}
		return (std::filesystem::path(root).parent_path() / path).lexically_normal().generic_string();
	}

public:
	AssetIO(std::string path, const std::vector<uint8_t>& data)
		: root(std::move(path))
		, bytes(data) { }
	bool Exists(const char* name) const override {
		const auto path = resolve(name);
		return path == root || RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().isFileExist(path);
	}
	char getOsSeparator() const override {
		return '/';
	}
	Assimp::IOStream* Open(const char* name, const char* mode = "rb") override {
		if (std::strchr(mode, 'w') || std::strchr(mode, '+') || std::strchr(mode, 'a')) {
			return nullptr;
		}
		const auto path = resolve(name);
		if (path == root) {
			return new AssetStream(bytes);
		}
		auto file = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().getFile(path);
		if (!file || !file->isValid()) {
			return nullptr;
		}
		return new AssetStream(file->read());
	}
	void Close(Assimp::IOStream* stream) override {
		delete stream;
	}
};
const aiScene* ReadAssetScene(Assimp::Importer& importer, const std::string& fileName, const std::vector<uint8_t>& data, unsigned int flags) {
	// Keep FBX pre/post rotations on the actual joint. Synthetic pivot nodes
	// otherwise separate animated channels from the skin's joint hierarchy.
	importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
	importer.SetIOHandler(new AssetIO(fileName, data));
	const aiScene* scene = importer.ReadFile(fileName, flags);
	if (!scene) {
		IKIGAI_COUT("Assimp failed to load " << fileName << ": " << importer.GetErrorString());
	}
	return scene;
}
}

namespace {
constexpr unsigned kMaxBoneWeights = 4;

MATH::Matrix4f ToMatrix(const aiMatrix4x4& m) {
	return MATH::Matrix4f(m.a1, m.a2, m.a3, m.a4, m.b1, m.b2, m.b3, m.b4, m.c1, m.c2, m.c3, m.c4, m.d1, m.d2, m.d3, m.d4);
}

bool IsSceneUsable(const aiScene* scene) {
	return scene && !(scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) && scene->mRootNode;
}

void ReadMaterials(const aiScene* scene, std::vector<std::string>& materials) {
	for (uint32_t i = 0; i < scene->mNumMaterials; ++i) {
		aiMaterial* material = scene->mMaterials[i];
		if (material) {
			aiString name;
			aiGetMaterialString(material, AI_MATKEY_NAME, &name);
			materials.push_back(name.C_Str());
		}
	}
}

void ReadMeshGeometry(const aiMesh* mesh, std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices) {
	for (uint32_t i = 0; i < mesh->mNumVertices; ++i) {
		// Node transforms are intentionally not applied to vertices.
		aiVector3D position = mesh->mVertices[i];
		aiVector3D normal = mesh->mNormals ? mesh->mNormals[i] : aiVector3D(0.0f, 0.0f, 0.0f);
		aiVector3D texCoords = mesh->mTextureCoords[0] ? mesh->mTextureCoords[0][i] : aiVector3D(0.0f, 0.0f, 0.0f);
		aiVector3D tangent = mesh->mTangents ? mesh->mTangents[i] : aiVector3D(0.0f, 0.0f, 0.0f);
		aiVector3D bitangent = mesh->mBitangents ? mesh->mBitangents[i] : aiVector3D(0.0f, 0.0f, 0.0f);

		outVertices.push_back(Vertex(MATH::Vector3f { position.x, position.y, position.z }, MATH::Vector2f { texCoords.x, texCoords.y },
			MATH::Vector3f { normal.x, normal.y, normal.z }, MATH::Vector3f { tangent.x, tangent.y, tangent.z },
			MATH::Vector3f { bitangent.x, bitangent.y, bitangent.z }));
	}

	for (uint32_t faceID = 0; faceID < mesh->mNumFaces; ++faceID) {
		auto& face = mesh->mFaces[faceID];
		for (size_t indexID = 0; indexID < 3; ++indexID) {
			outIndices.push_back(face.mIndices[indexID]);
		}
	}
}

void SetVertexBoneData(Vertex& vertex, int boneID, float weight) {
	for (unsigned i = 0; i < kMaxBoneWeights; ++i) {
		if (vertex.m_BoneIDs[i] < 0) {
			vertex.m_Weights[i] = weight;
			vertex.m_BoneIDs[i] = boneID;
			break;
		}
	}
}

struct ImportContext {
	const aiScene* Scene = nullptr;
	std::unordered_map<std::string, int> BoneMapping {};
	std::unordered_set<std::string> KnownBones {};
	ImportedModel* Output = nullptr;
};

void ReadMeshBones(ImportContext& context, const aiMesh* mesh, std::vector<Vertex>& vertices) {
	for (unsigned boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
		const aiBone* bone = mesh->mBones[boneIndex];
		std::string boneName = IKIGAI::SKELETON::trimmedName(bone->mName.C_Str());

		const auto mapping = context.BoneMapping.find(boneName);
		if (mapping == context.BoneMapping.end()) {
			continue; // If bone is not in skeleton, skip
		}
		const int boneID = mapping->second;

		if (context.KnownBones.insert(boneName).second) {
			ImportedBone imported;
			imported.Name = boneName;
			imported.Info.mId = boneID;
			imported.Info.mOffset = ToMatrix(bone->mOffsetMatrix);
			context.Output->Bones.push_back(std::move(imported));
		}

		for (unsigned weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
			const unsigned vertexId = bone->mWeights[weightIndex].mVertexId;
			const float weight = bone->mWeights[weightIndex].mWeight;
			assert(vertexId <= vertices.size());
			SetVertexBoneData(vertices[vertexId], boneID, weight);
		}
	}
}

std::vector<RENDER::MorphTarget> ReadMorphTargets(const aiMesh* mesh) {
	std::vector<RENDER::MorphTarget> targets;
	if (!mesh->mNumAnimMeshes) {
		return targets;
	}
	targets.reserve(mesh->mNumAnimMeshes);
	for (unsigned targetIndex = 0; targetIndex < mesh->mNumAnimMeshes; ++targetIndex) {
		const auto* source = mesh->mAnimMeshes[targetIndex];
		if (!source || source->mNumVertices != mesh->mNumVertices) {
			throw std::runtime_error("Morph target vertex count does not match the base mesh");
		}
		RENDER::MorphTarget target;
		target.Name = source->mName.length ? source->mName.C_Str() : "Target" + std::to_string(targetIndex);
		target.DefaultWeight = source->mWeight;
		target.Deltas.reserve(mesh->mNumVertices);
		for (unsigned vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
			RENDER::MorphVertexDelta delta;
			if (source->mVertices) {
				const auto value = source->mVertices[vertexIndex] - mesh->mVertices[vertexIndex];
				delta.Position = { value.x, value.y, value.z, 0 };
			}
			if (source->mNormals && mesh->mNormals) {
				const auto value = source->mNormals[vertexIndex] - mesh->mNormals[vertexIndex];
				delta.Normal = { value.x, value.y, value.z, 0 };
			}
			if (source->mTangents && mesh->mTangents) {
				const auto value = source->mTangents[vertexIndex] - mesh->mTangents[vertexIndex];
				delta.Tangent = { value.x, value.y, value.z, 0 };
			}
			target.Deltas.push_back(delta);
		}
		targets.push_back(std::move(target));
	}
	return targets;
}

void ComputeBounds(ImportedMesh& mesh) {
	if (mesh.Vertices.empty()) {
		return;
	}
	mesh.BoundsMin = mesh.Vertices.front().position;
	mesh.BoundsMax = mesh.Vertices.front().position;
	for (const auto& vertex : mesh.Vertices) {
		mesh.BoundsMin.x = std::min(mesh.BoundsMin.x, vertex.position.x);
		mesh.BoundsMin.y = std::min(mesh.BoundsMin.y, vertex.position.y);
		mesh.BoundsMin.z = std::min(mesh.BoundsMin.z, vertex.position.z);
		mesh.BoundsMax.x = std::max(mesh.BoundsMax.x, vertex.position.x);
		mesh.BoundsMax.y = std::max(mesh.BoundsMax.y, vertex.position.y);
		mesh.BoundsMax.z = std::max(mesh.BoundsMax.z, vertex.position.z);
	}
}

void ReadNode(ImportContext& context, const aiMatrix4x4& parentTransform, const aiNode* node) {
	const aiMatrix4x4 nodeTransformation = parentTransform * node->mTransformation;
	for (uint32_t i = 0; i < node->mNumMeshes; ++i) {
		const aiMesh* mesh = context.Scene->mMeshes[node->mMeshes[i]];
		ImportedMesh imported;
		imported.NodeName = node->mName.C_Str();
		imported.NodeTransform = ToMatrix(nodeTransformation);
		imported.MaterialIndex = mesh->mMaterialIndex;
		ReadMeshGeometry(mesh, imported.Vertices, imported.Indices);
		ReadMeshBones(context, mesh, imported.Vertices);
		imported.MorphTargets = ReadMorphTargets(mesh);
		ComputeBounds(imported);
		context.Output->Meshes.push_back(std::move(imported));
	}
	for (uint32_t i = 0; i < node->mNumChildren; ++i) {
		ReadNode(context, nodeTransformation, node->mChildren[i]);
	}
}

void ReadMorphAnimations(const aiScene* scene, std::vector<ImportedMorphAnimation>& outAnimations) {
	for (unsigned animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex) {
		const auto* animation = scene->mAnimations[animationIndex];
		if (!animation || animation->mNumMorphMeshChannels == 0) {
			continue;
		}
		ImportedMorphAnimation imported;
		imported.Name = animation->mName.length ? animation->mName.C_Str() : "Animation" + std::to_string(animationIndex);
		imported.Duration = animation->mDuration;
		imported.TicksPerSecond = animation->mTicksPerSecond;
		for (unsigned channelIndex = 0; channelIndex < animation->mNumMorphMeshChannels; ++channelIndex) {
			const auto* channel = animation->mMorphMeshChannels[channelIndex];
			if (!channel || !channel->mNumKeys) {
				continue;
			}
			ImportedMorphChannel importedChannel;
			importedChannel.NodeName = channel->mName.C_Str();
			importedChannel.Keys.reserve(channel->mNumKeys);
			for (unsigned keyIndex = 0; keyIndex < channel->mNumKeys; ++keyIndex) {
				const auto& source = channel->mKeys[keyIndex];
				ImportedMorphKey key;
				key.Time = source.mTime;
				key.Values.assign(source.mValues, source.mValues + source.mNumValuesAndWeights);
				key.Weights.assign(source.mWeights, source.mWeights + source.mNumValuesAndWeights);
				importedChannel.Keys.push_back(std::move(key));
			}
			imported.Channels.push_back(std::move(importedChannel));
		}
		outAnimations.push_back(std::move(imported));
	}
}

std::vector<RENDER::MorphAnimationClip> ResolveMorphAnimations(
	const std::vector<ImportedMorphAnimation>& animations, const RENDER::ModelInterface& model) {
	std::vector<RENDER::MorphAnimationClip> clips;
	for (const auto& animation : animations) {
		RENDER::MorphAnimationClip clip;
		clip.Name = animation.Name;
		clip.Duration = animation.TicksPerSecond > 0 ? float(animation.Duration / animation.TicksPerSecond) : 0;
		for (const auto& channel : animation.Channels) {
			for (const auto meshIndex : model.morphMeshesForNode(channel.NodeName)) {
				const auto& targets = model.getMeshes()[meshIndex]->morphTargets();
				for (std::size_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex) {
					RENDER::MorphAnimationTrack track;
					track.MeshIndex = meshIndex;
					track.TargetIndex = targetIndex;
					track.Keys.reserve(channel.Keys.size());
					for (const auto& source : channel.Keys) {
						float weight = 0;
						for (std::size_t valueIndex = 0; valueIndex < source.Values.size(); ++valueIndex) {
							if (source.Values[valueIndex] == targetIndex) {
								weight = float(source.Weights[valueIndex]);
								break;
							}
						}
						const float time = animation.TicksPerSecond > 0 ? float(source.Time / animation.TicksPerSecond) : 0;
						if (!std::isfinite(time) || !std::isfinite(weight) || time < 0 || weight < 0 || weight > 1
							|| (!track.Keys.empty() && time <= track.Keys.back().Time)) {
							throw std::runtime_error("Invalid morph animation key in " + clip.Name);
						}
						track.Keys.push_back({ time, weight });
					}
					clip.Duration = std::max(clip.Duration, track.Keys.back().Time);
					clip.Tracks.push_back(std::move(track));
				}
			}
		}
		if (!clip.Tracks.empty()) {
			clip.Duration = std::max(clip.Duration, 0.001f);
			clips.push_back(std::move(clip));
		}
	}
	return clips;
}
}

bool AssimpParser::ReadImportedModel(const aiScene* scene, ImportedModel& outModel) {
	if (!IsSceneUsable(scene)) {
		return false;
	}
	ImportedModel imported;
	ReadMaterials(scene, imported.MaterialNames);
	ImportContext context;
	context.Scene = scene;
	context.Output = &imported;
	SKELETON::Skeleton tempSkeleton;
	if (buildSkeleton(scene, tempSkeleton)) {
		for (int i = 0; i < tempSkeleton.getNumJolts(); ++i) {
			context.BoneMapping[tempSkeleton.joints()[i].name] = i;
		}
	}
	ReadNode(context, aiMatrix4x4(), scene->mRootNode);
	ReadMorphAnimations(scene, imported.MorphAnimations);
	outModel = std::move(imported);
	return true;
}

bool AssimpParser::ImportModel(const std::string& fileName, ModelParserFlags parserFlags, ImportedModel& outModel) {
	Assimp::Importer importer;
	return ReadImportedModel(importer.ReadFile(fileName, static_cast<int>(parserFlags)), outModel);
}

bool AssimpParser::ImportModel(const std::string& fileName, const std::vector<uint8_t>& data, ModelParserFlags parserFlags, ImportedModel& outModel) {
	Assimp::Importer importer;
	return ReadImportedModel(ReadAssetScene(importer, fileName, data, static_cast<unsigned int>(parserFlags)), outModel);
}

void AssimpParser::CreateModelResources(ImportedModel&& imported, RENDER::ModelInterface& model, const ModelBuildOptions& options) {
	if (options.ImportMaterials) {
		auto& materials = model.getMaterialsNames();
		materials.insert(materials.end(), imported.MaterialNames.begin(), imported.MaterialNames.end());
	}
	for (auto& bone : imported.Bones) {
		if (!model.isBoneExist(bone.Name)) {
			model.addBone(bone.Name, bone.Info);
		}
	}

	const bool batching = model.getUseBatching();
	std::vector<Vertex> batchedVertices;
	std::vector<uint32_t> batchedIndices;
	for (auto& mesh : imported.Meshes) {
		std::shared_ptr<RENDER::MeshInterface> newMesh;
		if (batching) {
			newMesh = RENDER::CreateMesh(mesh.Vertices, mesh.Indices, batchedIndices.size(), mesh.MaterialIndex);
			batchedVertices.insert(batchedVertices.end(), mesh.Vertices.begin(), mesh.Vertices.end());
			batchedIndices.insert(batchedIndices.end(), mesh.Indices.begin(), mesh.Indices.end());
		}
		else {
			newMesh = RENDER::CreateMesh(mesh.Vertices, mesh.Indices, mesh.MaterialIndex);
			if (newMesh) {
				newMesh->unbind();
			}
		}
		if (newMesh && !mesh.MorphTargets.empty()) {
			if (batching) {
				throw std::runtime_error("Morph targets cannot use batched model geometry");
			}
			newMesh->setMorphTargets(std::move(mesh.MorphTargets));
		}
		if (newMesh && !newMesh->morphTargets().empty()) {
			model.addMorphNodeMesh(mesh.NodeName, model.getMeshes().size());
		}
		model.getMeshes().push_back(newMesh); // The model will handle mesh destruction
	}

	if (options.ImportMorphAnimations) {
		model.setMorphAnimations(ResolveMorphAnimations(imported.MorphAnimations, model));
	}
	if (batching) {
		model.createBuffers(batchedVertices, batchedIndices);
	}
}

bool AssimpParser::LoadModel(const std::string& fileName, RESOURCES::ResourcePtr<RENDER::ModelInterface> model, ModelParserFlags parserFlags) {
	ImportedModel imported;
	if (!ImportModel(fileName, parserFlags, imported)) {
		return false;
	}
	CreateModelResources(std::move(imported), *model);
	return true;
}

bool AssimpParser::LoadModel(const std::string& fileName, const std::vector<uint8_t>& data, RESOURCES::ResourcePtr<RENDER::ModelInterface> model,
	ModelParserFlags parserFlags) {
	ImportedModel imported;
	if (!ImportModel(fileName, data, parserFlags, imported)) {
		return false;
	}
	CreateModelResources(std::move(imported), *model);
	return true;
}

namespace {
void CollectOpenGlVertices(
	const ImportedModel& imported, std::vector<std::vector<Vertex>>& verticesPerMesh, std::vector<std::vector<uint32_t>>& indicesPerMesh) {
	// The per-mesh CPU copies are only consumed by the OpenGL batching path.
	if (RENDER::DriverInterface::settings.backend != RENDER::RenderSettings::Backend::OPENGL) {
		return;
	}
	for (const auto& mesh : imported.Meshes) {
		verticesPerMesh.push_back(mesh.Vertices);
		indicesPerMesh.push_back(mesh.Indices);
	}
}

ModelBuildOptions VertexOnlyBuildOptions() {
	ModelBuildOptions options;
	options.ImportMaterials = false;
	options.ImportMorphAnimations = false;
	return options;
}
}

bool AssimpParser::LoadVertexes(const std::string& fileName, RESOURCES::ResourcePtr<RENDER::ModelInterface> model, ModelParserFlags parserFlags,
	std::vector<std::vector<Vertex>>& globalVerticesPerMesh, std::vector<std::vector<uint32_t>>& globalIndicesPerMesh) {
	ImportedModel imported;
	if (!ImportModel(fileName, parserFlags, imported)) {
		return false;
	}
	CollectOpenGlVertices(imported, globalVerticesPerMesh, globalIndicesPerMesh);
	CreateModelResources(std::move(imported), *model, VertexOnlyBuildOptions());
	return true;
}

bool AssimpParser::LoadVertexes(const std::string& fileName, const std::vector<uint8_t>& data, RESOURCES::ResourcePtr<RENDER::ModelInterface> model,
	ModelParserFlags parserFlags, std::vector<std::vector<Vertex>>& globalVerticesPerMesh, std::vector<std::vector<uint32_t>>& globalIndicesPerMesh) {
	ImportedModel imported;
	if (!ImportModel(fileName, data, parserFlags, imported)) {
		return false;
	}
	CollectOpenGlVertices(imported, globalVerticesPerMesh, globalIndicesPerMesh);
	CreateModelResources(std::move(imported), *model, VertexOnlyBuildOptions());
	return true;
}

// ============================================================
// Skeleton loading
// ============================================================

namespace {
void buildBoneListImpl(
	aiNode* node, const aiScene* scene, std::vector<aiBone*>& tempBoneList, std::unordered_set<std::string>& boneMap, uint32_t& numJoints) {
	for (unsigned int i = 0; i < node->mNumMeshes; i++) {
		aiMesh* currentMesh = scene->mMeshes[node->mMeshes[i]];

		for (unsigned int j = 0; j < currentMesh->mNumBones; j++) {
			std::string boneName = std::string(currentMesh->mBones[j]->mName.C_Str());
			if (boneMap.find(boneName) == boneMap.end()) {
				if (numJoints < tempBoneList.size()) {
					tempBoneList[numJoints] = currentMesh->mBones[j];
				}
				else {
					tempBoneList.push_back(currentMesh->mBones[j]);
				}
				numJoints++;
				boneMap.insert(boneName);
			}
		}
	}

	for (unsigned int i = 0; i < node->mNumChildren; i++) {
		buildBoneListImpl(node->mChildren[i], scene, tempBoneList, boneMap, numJoints);
	}
}

void buildSkeletonImpl(
	aiNode* node, const aiScene* scene, std::vector<aiBone*>& tempBoneList, uint32_t numJoints, IKIGAI::SKELETON::Skeleton& outSkeleton) {
	std::string nodeName = IKIGAI::SKELETON::trimmedName(node->mName.C_Str());

	for (uint32_t i = 0; i < numJoints; i++) {
		std::string boneName = IKIGAI::SKELETON::trimmedName(tempBoneList[i]->mName.C_Str());

		if (boneName == nodeName) {
			IKIGAI::SKELETON::Joint joint;

			joint.name = boneName;
			aiVector3D bindScale, bindPosition;
			aiQuaternion bindRotation;
			node->mTransformation.Decompose(bindScale, bindRotation, bindPosition);
			joint.bindPose.translation = { bindPosition.x, bindPosition.y, bindPosition.z };
			joint.bindPose.rotation = { bindRotation.x, bindRotation.y, bindRotation.z, bindRotation.w };
			joint.bindPose.scale = { bindScale.x, bindScale.y, bindScale.z };
			auto& m = tempBoneList[i]->mOffsetMatrix;
			joint.offsetTransform
				= IKIGAI::MATH::Matrix4f(m.a1, m.a2, m.a3, m.a4, m.b1, m.b2, m.b3, m.b4, m.c1, m.c2, m.c3, m.c4, m.d1, m.d2, m.d3, m.d4);

			aiNode* parent = node->mParent;
			int index = -1;

			while (parent) {
				index = outSkeleton.findJointIndex(IKIGAI::SKELETON::trimmedName(parent->mName.C_Str()));

				if (index == -1) {
					parent = parent->mParent;
				}
				else {
					break;
				}
			}

			joint.parentIndex = index;
			outSkeleton.joints().push_back(joint);
			break;
		}
	}

	for (unsigned int i = 0; i < node->mNumChildren; i++) {
		buildSkeletonImpl(node->mChildren[i], scene, tempBoneList, numJoints, outSkeleton);
	}
}
}

bool AssimpParser::buildSkeleton(const aiScene* scene, SKELETON::Skeleton& outSkeleton) {
	if (!scene || !scene->mRootNode) {
		return false;
	}
	std::vector<aiBone*> tempBoneList(256);
	std::unordered_set<std::string> boneMap;

	uint32_t numJoints = 0;
	buildBoneListImpl(scene->mRootNode, scene, tempBoneList, boneMap, numJoints);

	outSkeleton.setNumJoints(numJoints);
	outSkeleton.joints().reserve(numJoints);

	buildSkeletonImpl(scene->mRootNode, scene, tempBoneList, numJoints, outSkeleton);
	return true;
}

bool AssimpParser::LoadSkeleton(const std::string& fileName, SKELETON::Skeleton& outSkeleton) {
	Assimp::Importer importer;
	importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
	const aiScene* scene = importer.ReadFile(fileName, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);
	if (!scene) {
		return false;
	}
	return buildSkeleton(scene, outSkeleton);
}

bool AssimpParser::LoadSkeleton(const std::string& fileName, const std::vector<uint8_t>& data, SKELETON::Skeleton& outSkeleton) {
	Assimp::Importer importer;
	const aiScene* scene = ReadAssetScene(importer, fileName, data, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);
	if (!scene) {
		return false;
	}
	return buildSkeleton(scene, outSkeleton);
}

// ============================================================
// Animation loading
// ============================================================

bool AssimpParser::fillAnimation(
	const aiScene* scene, SKELETON::Skeleton& skeleton, SKELETON::Animation& outAnimation, bool additive, SKELETON::Animation* additiveReference) {
	if (!scene || !scene->mAnimations || scene->mNumAnimations == 0) {
		return false;
	}

	aiAnimation* anim = scene->mAnimations[0];
	if (!anim || anim->mNumChannels == 0 || !anim->mChannels || anim->mTicksPerSecond <= 0) {
		return false;
	}

	outAnimation.channels.resize(skeleton.getNumJolts());
	outAnimation.name = std::string(anim->mName.C_Str());
	outAnimation.duration = anim->mDuration / anim->mTicksPerSecond;
	outAnimation.durationInTicks = anim->mDuration;
	outAnimation.ticksPerSecond = anim->mTicksPerSecond;
	outAnimation.keyframeCount = anim->mChannels[0]->mNumPositionKeys;

	for (unsigned int i = 0; i < anim->mNumChannels; ++i) {
		aiNodeAnim* channel = anim->mChannels[i];
		std::string channelName = SKELETON::trimmedName(channel->mNodeName.C_Str());

		int jointIndex = skeleton.findJointIndex(channelName);
		if (jointIndex == -1) {
			continue;
		}

		outAnimation.channels[jointIndex].jointName = channelName;

		// --- Translation ---
		outAnimation.channels[jointIndex].translationKeyframes.resize(channel->mNumPositionKeys);
		MATH::Vector3f refTrans;
		if (channel->mNumPositionKeys > 0) {
			if (additiveReference && additiveReference->channels[jointIndex].translationKeyframes.size() > 0) {
				refTrans = additiveReference->channels[jointIndex].translationKeyframes[0].translation;
			}
			else {
				refTrans = MATH::Vector3f(channel->mPositionKeys[0].mValue.x, channel->mPositionKeys[0].mValue.y, channel->mPositionKeys[0].mValue.z);
			}
		}
		for (unsigned int j = 0; j < channel->mNumPositionKeys; ++j) {
			outAnimation.channels[jointIndex].translationKeyframes[j].time = channel->mPositionKeys[j].mTime;
			outAnimation.channels[jointIndex].translationKeyframes[j].translation
				= MATH::Vector3f(channel->mPositionKeys[j].mValue.x, channel->mPositionKeys[j].mValue.y, channel->mPositionKeys[j].mValue.z);
			if (additive) {
				outAnimation.channels[jointIndex].translationKeyframes[j].translation
					= SKELETON::translationDelta(refTrans, outAnimation.channels[jointIndex].translationKeyframes[j].translation);
			}
		}

		// --- Rotation ---
		outAnimation.channels[jointIndex].rotationKeyframes.resize(channel->mNumRotationKeys);
		MATH::QuaternionF refRot;
		if (channel->mNumRotationKeys > 0) {
			if (additiveReference && additiveReference->channels[jointIndex].rotationKeyframes.size() > 0) {
				refRot = additiveReference->channels[jointIndex].rotationKeyframes[0].rotation;
			}
			else {
				refRot = MATH::QuaternionF(channel->mRotationKeys[0].mValue.x, channel->mRotationKeys[0].mValue.y, channel->mRotationKeys[0].mValue.z,
					channel->mRotationKeys[0].mValue.w);
			}
		}
		for (unsigned int j = 0; j < channel->mNumRotationKeys; ++j) {
			outAnimation.channels[jointIndex].rotationKeyframes[j].time = channel->mRotationKeys[j].mTime;
			outAnimation.channels[jointIndex].rotationKeyframes[j].rotation = MATH::QuaternionF(channel->mRotationKeys[j].mValue.x,
				channel->mRotationKeys[j].mValue.y, channel->mRotationKeys[j].mValue.z, channel->mRotationKeys[j].mValue.w);
			if (additive) {
				outAnimation.channels[jointIndex].rotationKeyframes[j].rotation
					= SKELETON::rotationDelta(refRot, outAnimation.channels[jointIndex].rotationKeyframes[j].rotation);
			}
		}

		// --- Scale ---
		outAnimation.channels[jointIndex].scaleKeyframes.resize(channel->mNumScalingKeys);
		MATH::Vector3f refScale;
		if (channel->mNumScalingKeys > 0) {
			if (additiveReference && additiveReference->channels[jointIndex].scaleKeyframes.size() > 0) {
				refScale = additiveReference->channels[jointIndex].scaleKeyframes[0].scale;
			}
			else {
				refScale = MATH::Vector3f(channel->mScalingKeys[0].mValue.x, channel->mScalingKeys[0].mValue.y, channel->mScalingKeys[0].mValue.z);
			}
		}
		for (unsigned int j = 0; j < channel->mNumScalingKeys; ++j) {
			outAnimation.channels[jointIndex].scaleKeyframes[j].time = channel->mScalingKeys[j].mTime;
			outAnimation.channels[jointIndex].scaleKeyframes[j].scale
				= MATH::Vector3f(channel->mScalingKeys[j].mValue.x, channel->mScalingKeys[j].mValue.y, channel->mScalingKeys[j].mValue.z);
			if (additive) {
				outAnimation.channels[jointIndex].scaleKeyframes[j].scale
					= SKELETON::scaleDelta(refScale, outAnimation.channels[jointIndex].scaleKeyframes[j].scale);
			}
		}
	}
	return true;
}

bool AssimpParser::LoadAnimation(const std::string& fileName, SKELETON::Skeleton& skeleton, SKELETON::Animation& outAnimation, bool additive,
	SKELETON::Animation* additiveReference) {
	Assimp::Importer importer;
	importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
	const aiScene* scene = importer.ReadFile(fileName, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);
	if (!scene) {
		return false;
	}
	return fillAnimation(scene, skeleton, outAnimation, additive, additiveReference);
}

bool AssimpParser::LoadAnimation(const std::string& fileName, const std::vector<uint8_t>& data, SKELETON::Skeleton& skeleton,
	SKELETON::Animation& outAnimation, bool additive, SKELETON::Animation* additiveReference) {
	Assimp::Importer importer;
	const aiScene* scene = ReadAssetScene(importer, fileName, data, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);
	if (!scene) {
		return false;
	}
	return fillAnimation(scene, skeleton, outAnimation, additive, additiveReference);
}
