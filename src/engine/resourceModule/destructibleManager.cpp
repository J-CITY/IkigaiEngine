#include "destructibleManager.h"
#if USE_PHYSICS_3D
#include "materialManager.h"
#include "utilsModule/serialization/typedDescriptor.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <iostream>

namespace IKIGAI::RESOURCES {
namespace {
	std::shared_ptr<DestructibleResource> load(const std::string& path) {
		try {
			auto& fs = ServiceManager::Get<FileSystem>();
			auto file = fs.getFile(path);
			if (!file || !file->isValid()) {
				throw std::runtime_error("Missing descriptor");
			}
			auto settings = DATA::readDescriptor<DESTRUCTION::DestructibleDescriptor>(nlohmann::json::parse(file->readStr()));
			auto source = fs.getFile(settings.Model);
			if (!source || !source->isValid()) {
				throw std::runtime_error("Missing Model: " + settings.Model);
			}
			auto bytes = source->read();
			Assimp::Importer importer;
			auto extension = settings.Model.substr(settings.Model.find_last_of('.') + 1);
			const aiScene* scene = importer.ReadFileFromMemory(bytes.data(), bytes.size(),
				aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_CalcTangentSpace | aiProcess_FlipUVs, extension.c_str());
			if (!scene || !scene->HasMeshes()) {
				throw std::runtime_error("Cannot import Model: " + std::string(importer.GetErrorString()));
			}
			for (unsigned index = 0; index < scene->mNumMeshes; ++index) {
				if (scene->mMeshes[index]->HasBones() || scene->mMeshes[index]->mNumAnimMeshes != 0) {
					throw std::runtime_error("Skinned/morph meshes are unsupported");
				}
			}
			scene = importer.ApplyPostProcessing(aiProcess_PreTransformVertices);
			if (!scene) {
				throw std::runtime_error("Cannot bake destructible model transforms");
			}
			DESTRUCTION::FractureMesh mesh {};
			for (unsigned index = 0; index < scene->mNumMeshes; ++index) {
				const auto& part = *scene->mMeshes[index];
				if (part.HasBones() || part.mNumAnimMeshes != 0) {
					throw std::runtime_error("Skinned/morph meshes are unsupported");
				}
				if (part.mMaterialIndex >= settings.Materials.size()) {
					throw std::runtime_error("Materials must include every imported material slot");
				}
				if (mesh.vertices.size() + part.mNumVertices > 16384 || mesh.materials.size() + part.mNumFaces > settings.MaximumTriangles) {
					throw std::runtime_error("Model exceeds fracture vertex/triangle budget");
				}
				unsigned base = static_cast<unsigned>(mesh.vertices.size());
				for (unsigned vertex = 0; vertex < part.mNumVertices; ++vertex) {
					Vertex value {};
					auto convert = [](aiVector3D v) { return MATH::Vector3f { v.x, v.y, v.z }; };
					value.position = convert(part.mVertices[vertex]);
					value.normal = part.HasNormals() ? convert(part.mNormals[vertex]) : MATH::Vector3f {};
					if (part.HasTextureCoords(0)) {
						value.texCoord = { part.mTextureCoords[0][vertex].x, part.mTextureCoords[0][vertex].y };
					}
					if (part.HasTangentsAndBitangents()) {
						value.tangent = convert(part.mTangents[vertex]);
						value.bitangent = convert(part.mBitangents[vertex]);
					}
					mesh.vertices.push_back(value);
				}
				for (unsigned face = 0; face < part.mNumFaces; ++face) {
					if (part.mFaces[face].mNumIndices != 3) {
						throw std::runtime_error("Only triangle surfaces are supported");
					}
					for (unsigned corner = 0; corner < 3; ++corner) {
						mesh.indices.push_back(base + part.mFaces[face].mIndices[corner]);
					}
					mesh.materials.push_back(part.mMaterialIndex);
				}
			}
			auto resource = std::make_shared<DestructibleResource>();
			resource->definition = DESTRUCTION::compileDefinition(settings, std::move(mesh));
			auto& materials = ServiceManager::Get<MaterialLoader>();
			for (const auto& material : settings.Materials) {
				resource->materials.push_back(materials.loadResource(material));
			}
			resource->materials.push_back(materials.loadResource(settings.InteriorMaterial));
			for (const auto& material : resource->materials) {
				if (!material) {
					throw std::runtime_error("Missing fracture material");
				}
			}
			return resource;
		} catch (const std::exception& error) {
			throw std::runtime_error("Destructible " + path + ": " + error.what());
		}
	}
}
ResourcePtr<DestructibleResource> DestructibleLoader::createResource(const std::string& path) {
	auto result = load(path);
	addFileWatchSubscribe(path, { path, result->definition->settings.Model }, result);
	return result;
}
bool DestructibleLoader::reloadResource(std::weak_ptr<DestructibleResource> weak, const std::string& path) {
	if (auto resource = weak.lock()) {
		try {
			auto candidate = load(path);
			resource->definition = std::move(candidate->definition);
			resource->materials = std::move(candidate->materials);
			++resource->revision;
			return true;
		} catch (const std::exception& error) {
			std::cerr << "Destructible reload rejected: " << error.what() << '\n';
		}
	}
	return false;
}
}
#endif
