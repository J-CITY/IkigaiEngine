#include "assimpParser.h"

#include <cassert>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/matrix4x4.h>
#include "../resource/bone.h"
#include <renderModule/vertex.h>
#include "skeletalModule/skeleton.h"
#include "skeletalModule/animation.h"
#include <unordered_map>
#include <renderModule/backends/meshFactory.h>
#include <renderModule/backends/interface/driverInterface.h>
#include <renderModule/backends/interface/modelInterface.h>


using namespace IKIGAI;
using namespace IKIGAI::RESOURCES;

unsigned int ID=0;

bool needSaveVerts = false;
std::vector<std::vector<Vertex>>* globalVerticesPerMesh;
std::vector< std::vector<uint32_t>>* globalIndicesPerMesh;
//if batchin we use this vectors
std::vector<Vertex> globalVertices;
std::vector<uint32_t> globalIndices;

bool AssimpParser::LoadModel(const std::string& fileName,
	RESOURCES::ResourcePtr<RENDER::ModelInterface> model, ModelParserFlags parserFlags) {

	Assimp::Importer* import = new Assimp::Importer();
	auto scene = import->ReadFile(fileName, static_cast<int>(parserFlags));

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
		return false;
	}
	processMaterials(scene, model->getMaterialsNames());

	aiMatrix4x4 identity;

	globalVertices.clear();
	globalIndices.clear();
	//if (scene->HasAnimations()) {
	std::unordered_map<std::string, int> boneMapping;
	SKELETON::Skeleton tempSkeleton;
	if (buildSkeleton(scene, tempSkeleton)) {
		for (int i = 0; i < tempSkeleton.getNumJolts(); ++i) {
			boneMapping[tempSkeleton.joints()[i].name] = i;
		}
	}
	processNode(&identity, scene->mRootNode, scene, model, boneMapping);
	//}
	if (model->getUseBatching()) {
		model->createBuffers(globalVertices, globalIndices);
	}


	//std::shared_ptr<Assimp::Importer> imp = std::shared_ptr<Assimp::Importer>(import);
	//for (auto m : meshes) {
	//	m->imp = imp;
	//}
	delete import;
	return true;
}

bool AssimpParser::LoadModel(const std::string& fileName, const std::vector<uint8_t>& data,
	RESOURCES::ResourcePtr<RENDER::ModelInterface> model,  ModelParserFlags parserFlags) {
	
	Assimp::Importer* import = new Assimp::Importer();
	auto scene = import->ReadFileFromMemory(data.data(), data.size(), static_cast<int>(parserFlags), fileName.c_str());
	
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
		return false;
	}
	processMaterials(scene, model->getMaterialsNames());

	aiMatrix4x4 identity;

	globalVertices.clear();
	globalIndices.clear();
	//if (scene->HasAnimations()) {
		std::unordered_map<std::string, int> boneMapping;
		SKELETON::Skeleton tempSkeleton;
		if (buildSkeleton(scene, tempSkeleton)) {
			for (int i = 0; i < tempSkeleton.getNumJolts(); ++i) {
				boneMapping[tempSkeleton.joints()[i].name] = i;
			}
		}

		processNode(&identity, scene->mRootNode, scene, model, boneMapping);
	//}
	if (model->getUseBatching()) {
		model->createBuffers(globalVertices, globalIndices);
	}


	//std::shared_ptr<Assimp::Importer> imp = std::shared_ptr<Assimp::Importer>(import);
	//for (auto m : meshes) {
	//	m->imp = imp;
	//}
	delete import;
	return true;
}

bool AssimpParser::LoadVertexes(const std::string& fileName,
	RESOURCES::ResourcePtr<RENDER::ModelInterface> model, ModelParserFlags parserFlags,
	std::vector<std::vector<Vertex>>& _globalVerticesPerMesh,
	std::vector< std::vector<uint32_t>>& _globalIndicesPerMesh) {
	needSaveVerts = true;
	globalVerticesPerMesh = &_globalVerticesPerMesh;
	globalIndicesPerMesh = &_globalIndicesPerMesh;
	Assimp::Importer* import = new Assimp::Importer();
	auto scene = import->ReadFile(fileName, static_cast<int>(parserFlags));

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
		delete import;
		needSaveVerts = false;
		return false;
	}
	//processMaterials(scene, model->getMaterialsNames());

	aiMatrix4x4 identity;

	globalVertices.clear();
	globalIndices.clear();
	//if (scene->HasAnimations()) {
	std::unordered_map<std::string, int> boneMapping;
	SKELETON::Skeleton tempSkeleton;
	if (buildSkeleton(scene, tempSkeleton)) {
		for (int i = 0; i < tempSkeleton.getNumJolts(); ++i) {
			boneMapping[tempSkeleton.joints()[i].name] = i;
		}
	}

	//if (scene->HasAnimations()) {
	processNode(&identity, scene->mRootNode, scene, model, boneMapping);
	//}


	//std::shared_ptr<Assimp::Importer> imp = std::shared_ptr<Assimp::Importer>(import);
	//for (auto m : meshes) {
	//	m->imp = imp;
	//}
	delete import;
	needSaveVerts = false;
	globalVerticesPerMesh = nullptr;
	globalIndicesPerMesh = nullptr;
	return true;
}

bool AssimpParser::LoadVertexes(const std::string& fileName, const std::vector<uint8_t>& data, 
	RESOURCES::ResourcePtr<RENDER::ModelInterface> model, ModelParserFlags parserFlags,
	std::vector<std::vector<Vertex>>& _globalVerticesPerMesh,
	std::vector< std::vector<uint32_t>>& _globalIndicesPerMesh) {
	needSaveVerts = true;
	globalVerticesPerMesh = &_globalVerticesPerMesh;
	globalIndicesPerMesh = &_globalIndicesPerMesh;
	Assimp::Importer* import = new Assimp::Importer();
	auto scene = import->ReadFileFromMemory(data.data(), data.size(), static_cast<int>(parserFlags), fileName.c_str());

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
		delete import;
		needSaveVerts = false;
		return false;
	}
	//processMaterials(scene, model->getMaterialsNames());

	aiMatrix4x4 identity;

	globalVertices.clear();
	globalIndices.clear();
	//if (scene->HasAnimations()) {
	std::unordered_map<std::string, int> boneMapping;
	SKELETON::Skeleton tempSkeleton;
	if (buildSkeleton(scene, tempSkeleton)) {
		for (int i = 0; i < tempSkeleton.getNumJolts(); ++i) {
			boneMapping[tempSkeleton.joints()[i].name] = i;
		}
	}

	//if (scene->HasAnimations()) {
	processNode(&identity, scene->mRootNode, scene, model, boneMapping);
	//}


	//std::shared_ptr<Assimp::Importer> imp = std::shared_ptr<Assimp::Importer>(import);
	//for (auto m : meshes) {
	//	m->imp = imp;
	//}
	delete import;
	needSaveVerts = false;
	globalVerticesPerMesh = nullptr;
	globalIndicesPerMesh = nullptr;
	return true;
}

void AssimpParser::processMaterials(const aiScene* scene, std::vector<std::string>& materials) {
	for (uint32_t i = 0; i < scene->mNumMaterials; ++i) {
		aiMaterial* material = scene->mMaterials[i];
		if (material) {
			aiString name;
			aiGetMaterialString(material, AI_MATKEY_NAME, &name);
			materials.push_back(name.C_Str());
		}
	}
}
void AssimpParser::processNode(void* transform, aiNode* node, const aiScene* scene, ResourcePtr<RENDER::ModelInterface> model, const std::unordered_map<std::string, int>& boneMapping) {
	aiMatrix4x4 nodeTransformation = *reinterpret_cast<aiMatrix4x4*>(transform) * node->mTransformation;

	auto boneSz = 0;
	for (size_t i = 0; i < scene->mNumMeshes; i++) {
		boneSz += scene->mMeshes[i]->mNumVertices;
	}
	
	unsigned m_NumBones = 0;
	// Process all the node's meshes (if any)
	int bonesSz = 0;
	for (uint32_t i = 0; i < node->mNumMeshes; ++i) {
		bonesSz += scene->mMeshes[i]->mNumVertices;
	}
	//std::vector<VertexBoneData> bones;
	//bones.resize(bonesSz/*scene->mMeshes[i]->mNumVertices*/);
	auto NumVertices = 0;
	auto NumIndices = 0;

	for (uint32_t i = 0; i < node->mNumMeshes; ++i) {
		m_NumBones = 0;
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		//std::map<std::string, unsigned> m_BoneMapping;
		//std::vector<BoneInfo> m_BoneInfo;
		
		MATH::Matrix4f m_GlobalInverseTransform(
			scene->mRootNode->mTransformation.a1, scene->mRootNode->mTransformation.a2, scene->mRootNode->mTransformation.a3, scene->mRootNode->mTransformation.a4,
			scene->mRootNode->mTransformation.b1, scene->mRootNode->mTransformation.b2, scene->mRootNode->mTransformation.b3, scene->mRootNode->mTransformation.b4,
			scene->mRootNode->mTransformation.c1, scene->mRootNode->mTransformation.c2, scene->mRootNode->mTransformation.c3, scene->mRootNode->mTransformation.c4,
			scene->mRootNode->mTransformation.d1, scene->mRootNode->mTransformation.d2, scene->mRootNode->mTransformation.d3, scene->mRootNode->mTransformation.d4
		);
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		processMesh(&nodeTransformation, mesh, scene, vertices, indices);

		loadBones(vertices, mesh, scene, model, boneMapping);
		//loadBones(NumVertices, mesh, bones, m_BoneMapping, m_NumBones, m_BoneInfo);'
		std::shared_ptr<RENDER::MeshInterface> newMash;
		if (needSaveVerts && RENDER::DriverInterface::settings.backend == RENDER::RenderSettings::Backend::OPENGL) {
			globalVerticesPerMesh->push_back(vertices);
			globalIndicesPerMesh->push_back(indices);
		}
		if (model->getUseBatching()) {
			newMash = RENDER::CreateMesh(vertices, indices, globalIndices.size(), mesh->mMaterialIndex);
			globalVertices.insert(globalVertices.end(), vertices.begin(), vertices.end());
			globalIndices.insert(globalIndices.end(), indices.begin(), indices.end());
		} else {
			newMash = RENDER::CreateMesh(vertices, indices, mesh->mMaterialIndex);
			if (newMash) {
				newMash->unbind();
			}
		}

		//newMash->m_pScene = scene;
		//newMash->m_BoneMapping = m_BoneMapping;
		//newMash->m_BoneInfo = m_BoneInfo;
		//newMash->bounseCount = bones.size();
		//newMash->BaseVertex = NumVertices;// -mesh->mNumVertices;
		//newMash->BaseIndex = NumIndices;// -mesh->mMaterialIndex;

		NumVertices += mesh->mNumVertices;
		NumIndices += mesh->mMaterialIndex;
		//std::vector<float> vertexData;
		//for (const auto& vertex : bones) {
		//	vertexData.push_back(vertex.IDs[0]);
		//	vertexData.push_back(vertex.IDs[1]);
		//	vertexData.push_back(vertex.IDs[2]);
		//	vertexData.push_back(vertex.IDs[3]);
		//	vertexData.push_back(vertex.Weights[0]);
		//	vertexData.push_back(vertex.Weights[1]);
		//	vertexData.push_back(vertex.Weights[2]);
		//	vertexData.push_back(vertex.Weights[3]);
		//}

		//TODO: move to init buffers
		// Create the buffers for the vertices attributes
		//if (ID != 0) {
		////	glDeleteBuffers(1, &ID);
		//}
		//glGenBuffers(1, &ID);
		//
		//glBindBuffer(GL_ARRAY_BUFFER, ID);
		//glBufferData(GL_ARRAY_BUFFER, sizeof(bones[0]) * bones.size(), &bones[0], GL_STATIC_DRAW);
		//glEnableVertexAttribArray(BONE_ID_LOCATION);
		//glVertexAttribIPointer(BONE_ID_LOCATION, 4, GL_INT, sizeof(VertexBoneData), nullptr);
		////glVertexAttribPointer(BONE_ID_LOCATION, 4, GL_FLOAT, GL_FALSE, sizeof(VertexBoneData), nullptr);
		//glEnableVertexAttribArray(BONE_WEIGHT_LOCATION);
		//glVertexAttribPointer(BONE_WEIGHT_LOCATION, 4, GL_FLOAT, GL_FALSE, sizeof(VertexBoneData), (const GLvoid*)16);
		////newMash->bonesBuffre = std::make_unique<RENDER::VertexAttribBuffer<VertexBoneData>>(vertexData);
		//
		//newMash->m_GlobalInverseTransform = m_GlobalInverseTransform;
		//newMash->m_NumBones = m_NumBones;
		model->getMeshes().push_back(newMash); // The model will handle mesh destruction
	}

	// Then do the same for each of its children
	for (uint32_t i = 0; i < node->mNumChildren; ++i) {
		processNode(&nodeTransformation, node->mChildren[i], scene, model, boneMapping);
	}
}

void AssimpParser::loadBones(std::vector<Vertex>& vertices, aiMesh* mesh, const aiScene* scene, ResourcePtr<RENDER::ModelInterface> model, const std::unordered_map<std::string, int>& boneMapping) {
	for (int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
		std::string boneName = IKIGAI::SKELETON::trimmedName(mesh->mBones[boneIndex]->mName.C_Str());
		
		int boneID = -1;
		if (boneMapping.find(boneName) != boneMapping.end()) {
			boneID = boneMapping.at(boneName);
		} else {
			continue; // If bone is not in skeleton, skip
		}

		if (!model->isBoneExist(boneName)) {
			RENDER::BoneInfo newBoneInfo;
			newBoneInfo.mId = boneID;
			newBoneInfo.mOffset = ConvertMatrix4x4(mesh->mBones[boneIndex]->mOffsetMatrix);
			model->addBone(boneName, newBoneInfo);
		}
		
		auto weights = mesh->mBones[boneIndex]->mWeights;
		int numWeights = mesh->mBones[boneIndex]->mNumWeights;

		for (int weightIndex = 0; weightIndex < numWeights; ++weightIndex) {
			int vertexId = weights[weightIndex].mVertexId;
			float weight = weights[weightIndex].mWeight;
			assert(vertexId <= vertices.size());
			setVertexBoneData(vertices[vertexId], boneID, weight);
		}
	}
}

void AssimpParser::setVertexBoneData(Vertex& vertex, int boneID, float weight) {
	for (unsigned i = 0; i < MAX_BONE_WEIGHTS; ++i) {
		if (vertex.m_BoneIDs[i] < 0) {
			vertex.m_Weights[i] = weight;
			vertex.m_BoneIDs[i] = boneID;
			break;
		}
	}
}

// ============================================================
// Skeleton loading
// ============================================================

namespace {
	void buildBoneListImpl(aiNode* node, const aiScene* scene, std::vector<aiBone*>& tempBoneList, std::unordered_set<std::string>& boneMap, uint32_t& numJoints) {
		for (unsigned int i = 0; i < node->mNumMeshes; i++) {
			aiMesh* currentMesh = scene->mMeshes[node->mMeshes[i]];

			for (unsigned int j = 0; j < currentMesh->mNumBones; j++) {
				std::string boneName = std::string(currentMesh->mBones[j]->mName.C_Str());
				if (boneMap.find(boneName) == boneMap.end()) {
					if (numJoints < tempBoneList.size()) {
						tempBoneList[numJoints] = currentMesh->mBones[j];
					} else {
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

	void buildSkeletonImpl(aiNode* node, const aiScene* scene, std::vector<aiBone*>& tempBoneList, uint32_t numJoints, IKIGAI::SKELETON::Skeleton& outSkeleton) {
		std::string nodeName = IKIGAI::SKELETON::trimmedName(node->mName.C_Str());

		for (uint32_t i = 0; i < numJoints; i++) {
			std::string boneName = IKIGAI::SKELETON::trimmedName(tempBoneList[i]->mName.C_Str());

			if (boneName == nodeName) {
				IKIGAI::SKELETON::Joint joint;

				joint.name = boneName;
				auto& m = tempBoneList[i]->mOffsetMatrix;
				joint.offsetTransform = IKIGAI::MATH::Matrix4f(
					m.a1, m.a2, m.a3, m.a4,
					m.b1, m.b2, m.b3, m.b4,
					m.c1, m.c2, m.c3, m.c4,
					m.d1, m.d2, m.d3, m.d4
				);

				aiNode* parent = node->mParent;
				int index = -1;

				while (parent) {
					index = outSkeleton.findJointIndex(IKIGAI::SKELETON::trimmedName(parent->mName.C_Str()));

					if (index == -1) {
						parent = parent->mParent;
					} else {
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
	const aiScene* scene = importer.ReadFile(
		fileName,
		aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);
	if (!scene) {
		return false;
	}
	return buildSkeleton(scene, outSkeleton);
}

bool AssimpParser::LoadSkeleton(const std::string& fileName, const std::vector<uint8_t>& data,
	SKELETON::Skeleton& outSkeleton) {
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFileFromMemory(
		data.data(), data.size(),
		aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs,
		fileName.c_str());
	if (!scene) {
		return false;
	}
	return buildSkeleton(scene, outSkeleton);
}

// ============================================================
// Animation loading
// ============================================================

bool AssimpParser::fillAnimation(const aiScene* scene, SKELETON::Skeleton& skeleton,
	SKELETON::Animation& outAnimation,
	bool additive, SKELETON::Animation* additiveReference) {
	if (!scene || !scene->mAnimations || scene->mNumAnimations == 0) {
		return false;
	}

	aiAnimation* anim = scene->mAnimations[0];

	outAnimation.channels.resize(skeleton.getNumJolts());
	outAnimation.name            = std::string(anim->mName.C_Str());
	outAnimation.duration        = anim->mDuration / anim->mTicksPerSecond;
	outAnimation.durationInTicks = anim->mDuration;
	outAnimation.ticksPerSecond  = anim->mTicksPerSecond;
	outAnimation.keyframeCount   = anim->mChannels[0]->mNumPositionKeys;

	for (unsigned int i = 0; i < anim->mNumChannels; ++i) {
		aiNodeAnim* channel = anim->mChannels[i];
		std::string channelName = SKELETON::trimmedName(channel->mNodeName.C_Str());

		int jointIndex = skeleton.findJointIndex(channelName);
		if (jointIndex == -1) continue;

		outAnimation.channels[jointIndex].jointName = channelName;

		// --- Translation ---
		outAnimation.channels[jointIndex].translationKeyframes.resize(channel->mNumPositionKeys);
		MATH::Vector3f refTrans;
		if (channel->mNumPositionKeys > 0) {
			if (additiveReference && additiveReference->channels[jointIndex].translationKeyframes.size() > 0)
				refTrans = additiveReference->channels[jointIndex].translationKeyframes[0].translation;
			else
				refTrans = MATH::Vector3f(channel->mPositionKeys[0].mValue.x,
					channel->mPositionKeys[0].mValue.y,
					channel->mPositionKeys[0].mValue.z);
		}
		for (unsigned int j = 0; j < channel->mNumPositionKeys; ++j) {
			outAnimation.channels[jointIndex].translationKeyframes[j].time = channel->mPositionKeys[j].mTime;
			outAnimation.channels[jointIndex].translationKeyframes[j].translation = MATH::Vector3f(
				channel->mPositionKeys[j].mValue.x,
				channel->mPositionKeys[j].mValue.y,
				channel->mPositionKeys[j].mValue.z);
			if (additive)
				outAnimation.channels[jointIndex].translationKeyframes[j].translation =
					SKELETON::translationDelta(refTrans, outAnimation.channels[jointIndex].translationKeyframes[j].translation);
		}

		// --- Rotation ---
		outAnimation.channels[jointIndex].rotationKeyframes.resize(channel->mNumRotationKeys);
		MATH::QuaternionF refRot;
		if (channel->mNumRotationKeys > 0) {
			if (additiveReference && additiveReference->channels[jointIndex].rotationKeyframes.size() > 0)
				refRot = additiveReference->channels[jointIndex].rotationKeyframes[0].rotation;
			else
				refRot = MATH::QuaternionF(
					channel->mRotationKeys[0].mValue.x,
					channel->mRotationKeys[0].mValue.y,
					channel->mRotationKeys[0].mValue.z,
					channel->mRotationKeys[0].mValue.w);
		}
		for (unsigned int j = 0; j < channel->mNumRotationKeys; ++j) {
			outAnimation.channels[jointIndex].rotationKeyframes[j].time = channel->mRotationKeys[j].mTime;
			outAnimation.channels[jointIndex].rotationKeyframes[j].rotation = MATH::QuaternionF(
				channel->mRotationKeys[j].mValue.x,
				channel->mRotationKeys[j].mValue.y,
				channel->mRotationKeys[j].mValue.z,
				channel->mRotationKeys[j].mValue.w);
			if (additive)
				outAnimation.channels[jointIndex].rotationKeyframes[j].rotation =
					SKELETON::rotationDelta(refRot, outAnimation.channels[jointIndex].rotationKeyframes[j].rotation);
		}

		// --- Scale ---
		outAnimation.channels[jointIndex].scaleKeyframes.resize(channel->mNumScalingKeys);
		MATH::Vector3f refScale;
		if (channel->mNumScalingKeys > 0) {
			if (additiveReference && additiveReference->channels[jointIndex].scaleKeyframes.size() > 0)
				refScale = additiveReference->channels[jointIndex].scaleKeyframes[0].scale;
			else
				refScale = MATH::Vector3f(channel->mScalingKeys[0].mValue.x,
					channel->mScalingKeys[0].mValue.y,
					channel->mScalingKeys[0].mValue.z);
		}
		for (unsigned int j = 0; j < channel->mNumScalingKeys; ++j) {
			outAnimation.channels[jointIndex].scaleKeyframes[j].time = channel->mScalingKeys[j].mTime;
			outAnimation.channels[jointIndex].scaleKeyframes[j].scale = MATH::Vector3f(
				channel->mScalingKeys[j].mValue.x,
				channel->mScalingKeys[j].mValue.y,
				channel->mScalingKeys[j].mValue.z);
			if (additive)
				outAnimation.channels[jointIndex].scaleKeyframes[j].scale =
					SKELETON::scaleDelta(refScale, outAnimation.channels[jointIndex].scaleKeyframes[j].scale);
		}
	}
	return true;
}

bool AssimpParser::LoadAnimation(const std::string& fileName,
	SKELETON::Skeleton& skeleton,
	SKELETON::Animation& outAnimation,
	bool additive, SKELETON::Animation* additiveReference) {
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(
		fileName,
		aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs);
	if (!scene) return false;
	return fillAnimation(scene, skeleton, outAnimation, additive, additiveReference);
}

bool AssimpParser::LoadAnimation(const std::string& fileName,
	const std::vector<uint8_t>& data,
	SKELETON::Skeleton& skeleton,
	SKELETON::Animation& outAnimation,
	bool additive, SKELETON::Animation* additiveReference) {
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFileFromMemory(
		data.data(), data.size(),
		aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs,
		fileName.c_str());
	if (!scene) return false;
	return fillAnimation(scene, skeleton, outAnimation, additive, additiveReference);
}

void AssimpParser::processMesh(void* transform, aiMesh* mesh, const aiScene* scene, std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices) {
	aiMatrix4x4 meshTransformation = *reinterpret_cast<aiMatrix4x4*>(transform);

	for (uint32_t i = 0; i < mesh->mNumVertices; ++i) {
		aiVector3D position = /*meshTransformation * */ mesh->mVertices[i];
		aiVector3D normal = /*meshTransformation * */ (mesh->mNormals ? mesh->mNormals[i] : aiVector3D(0.0f, 0.0f, 0.0f));
		aiVector3D texCoords = mesh->mTextureCoords[0] ? mesh->mTextureCoords[0][i] : aiVector3D(0.0f, 0.0f, 0.0f);
		aiVector3D tangent = mesh->mTangents ? /*meshTransformation * */  mesh->mTangents[i] : aiVector3D(0.0f, 0.0f, 0.0f);
		aiVector3D bitangent = mesh->mBitangents ? /*meshTransformation * */  mesh->mBitangents[i] : aiVector3D(0.0f, 0.0f, 0.0f);

		outVertices.push_back(
			Vertex(
				MATH::Vector3f{
					position.x,
					position.y,
					position.z
				},
				MATH::Vector2f{
					texCoords.x,
					texCoords.y
				},
				MATH::Vector3f{
					normal.x,
					normal.y,
					normal.z
				},
				MATH::Vector3f{
					tangent.x,
					tangent.y,
					tangent.z
				},
				MATH::Vector3f{
					bitangent.x,
					bitangent.y,
					bitangent.z
				}
		)
		);
	}

	for (uint32_t faceID = 0; faceID < mesh->mNumFaces; ++faceID) {
		auto& face = mesh->mFaces[faceID];
		for (size_t indexID = 0; indexID < 3; ++indexID) {
			outIndices.push_back(face.mIndices[indexID]);
		}
	}
}

