#include "particleManager.h"
#include "materialManager.h"
#include "modelManager.h"
#include "renderModule/backends/interface/materialInterface.h"
#include "renderModule/backends/interface/meshInterface.h"
#include "renderModule/backends/interface/modelInterface.h"
#include "textureManager.h"

namespace IKIGAI::RESOURCES {
namespace {
	std::string materialPath(const PARTICLES::Renderer& r) {
		return r.material.empty() ? (r.mesh.empty() ? "/materials/render/portable/particles.mat" : "/materials/render/portable/particleMesh.mat")
								  : r.material;
	}
	std::shared_ptr<RENDER::MeshInterface> selectMesh(const std::shared_ptr<RENDER::ModelInterface>& model, const PARTICLES::Renderer& r) {
		const auto path = r.mesh + "/MeshIndex/" + std::to_string(r.meshIndex);
		if (!model || r.meshIndex >= model->getMeshes().size()) {
			throw std::runtime_error(path + ": mesh index does not exist");
		}
		if (model->getBoneCounter()) {
			throw std::runtime_error(path + ": mesh particles require static geometry");
		}
		auto mesh = model->getMeshes()[r.meshIndex];
		if (!mesh || mesh->rayTracingVertices().empty() || mesh->rayTracingIndices().empty() || mesh->rayTracingIndices().size() % 3) {
			throw std::runtime_error(path + ": canonical indexed triangle geometry is required");
		}
		for (auto index : mesh->rayTracingIndices()) {
			if (index >= mesh->rayTracingVertices().size()) {
				throw std::runtime_error(path + ": geometry index is out of range");
			}
		}
		return mesh;
	}
}
ResourcePtr<ParticleAsset> ParticleLoader::createInline(std::shared_ptr<const PARTICLES::Definition> definition) {
	auto asset = std::make_shared<ParticleAsset>();
	asset->definition = std::move(definition);
	auto& textures = ServiceManager::Get<TextureLoader>();
	auto& materials = ServiceManager::Get<MaterialLoader>();
	for (const auto& e : asset->definition->emitters) {
		ParticleAsset::DrawResources r;
		r.piece = e.renderer.piece;
		if (r.piece.empty() && !e.renderer.texture.empty()) {
			r.piece = textures.resolveSprite(e.renderer.texture).region;
		}
		if (!e.renderer.atlas.empty()) {
			r.texture = textures.createAtlasFromFile(e.renderer.atlas, true);
		}
		else if (!e.renderer.texture.empty()) {
			r.texture = textures.createSprite(e.renderer.texture);
		}
		else {
			r.texture = textures.createColor("particle-white", 255, 255, 255, 255, false);
		}
		r.material = materials.loadResource(materialPath(e.renderer));
		if (!e.renderer.mesh.empty()) {
			r.mesh = selectMesh(ServiceManager::Get<ModelLoader>().loadResource(
									e.renderer.mesh, e.renderer.mesh.ends_with(".model") ? ELoadingType::RESOURCE : ELoadingType::FILE),
				e.renderer);
		}
		asset->renderers.push_back(std::move(r));
	}
	return asset;
}
ResourcePtr<ParticleAsset> ParticleLoader::createResource(const std::string& path) {
	if (auto ready = getResource(path)) {
		return ready;
	}
	const auto file = ServiceManager::Get<FileSystem>().getFile(path, FileMode::READ);
	if (!file || !file->isValid()) {
		throw std::runtime_error("Missing particle asset: " + path);
	}
	auto definition = PARTICLES::parse(nlohmann::json::parse(file->readStr()), path);
	return registerResource(path, createInline(std::move(definition)));
}
COROUTINE::Task<ResourcePtr<ParticleAsset>> ParticleLoader::createResourceTask(
	std::string path, ELoadingType, std::shared_ptr<ResourceLoadControl> control) {
	control->check();
	if (auto ready = getResource(path)) {
		co_return ready;
	}
	auto file = ServiceManager::Get<FileSystem>().getFile(path, FileMode::READ);
	if (!file || !file->isValid()) {
		throw std::runtime_error("Missing particle asset: " + path);
	}
	auto bytes = co_await file->readAllTask(control);
	control->check();
	auto asset = std::make_shared<ParticleAsset>();
	asset->definition = PARTICLES::parse(nlohmann::json::parse(bytes.begin(), bytes.end()), path);
	for (const auto& e : asset->definition->emitters) {
		control->check();
		ParticleAsset::DrawResources r;
		auto& textures = ServiceManager::Get<TextureLoader>();
		r.piece = e.renderer.piece;
		if (r.piece.empty() && !e.renderer.texture.empty()) {
			r.piece = textures.resolveSprite(e.renderer.texture).region;
		}
		if (!e.renderer.atlas.empty()) {
			r.texture = co_await textures.requestAtlas(e.renderer.atlas).wait();
		}
		else if (!e.renderer.texture.empty()) {
			r.texture = co_await textures.requestSprite(e.renderer.texture).wait();
		}
		else {
			r.texture = textures.createColor("particle-white", 255, 255, 255, 255, false);
		}
		control->check();
		r.material = co_await ServiceManager::Get<MaterialLoader>().loadResourceTask(materialPath(e.renderer));
		if (!e.renderer.mesh.empty()) {
			auto model = co_await ServiceManager::Get<ModelLoader>().loadResourceTask(
				e.renderer.mesh, e.renderer.mesh.ends_with(".model") ? ELoadingType::RESOURCE : ELoadingType::FILE);
			control->check();
			r.mesh = selectMesh(model, e.renderer);
		}
		asset->renderers.push_back(std::move(r));
		co_await COROUTINE::YieldIfBudgetExceeded();
	}
	control->check();
	co_return registerResource(path, std::move(asset), control.get());
}
}
