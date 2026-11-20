#include "sceneResourcePreparation.h"

#include "coreModule/ecs/componentCatalog.h"
#include "coreModule/ecs/components/audioComponent.h"
#include "coreModule/ecs/components/foliageComponent.h"
#include "coreModule/ecs/components/materialRenderer.h"
#include "coreModule/ecs/components/modelRenderer.h"
#include "coreModule/ecs/components/particleSystemComponent.h"
#include "coreModule/ecs/components/skeletalAnimationComponent.h"
#include "coreModule/ecs/components/skeletalComponent.h"
#include "coreModule/ecs/components/surfaceComponent.h"
#include "coreModule/gui/components/spriteComponent.h"
#include "coreModule/gui/components/vectorComponents.h"
#include "resourceModule/audioSourceManager.h"
#include "resourceModule/fontManager.h"
#include "resourceModule/materialManager.h"
#include "resourceModule/modelManager.h"
#include "resourceModule/particleManager.h"
#include "resourceModule/skeletonAnimationManager.h"
#include "resourceModule/skeletonBlendspaceManager.h"
#include "resourceModule/skeletonManager.h"
#include "resourceModule/skeletonStateGraphManager.h"
#include "resourceModule/textureManager.h"
#include "sceneModule/sceneManager.h"
#include "utilsModule/serialization/typedDescriptor.h"
#include "utilsModule/serviceManager.h"
#if USE_PHYSICS_3D
#include "coreModule/ecs/components/destructibleMeshComponent.h"
#include "coreModule/ecs/components/ragdollComponent.h"
#include "resourceModule/destructibleManager.h"
#include "resourceModule/ragdollManager.h"
#endif

using namespace IKIGAI;
using namespace IKIGAI::SCENE_SYSTEM;
using namespace IKIGAI::COROUTINE;
using namespace IKIGAI::RESOURCES;

namespace {
template <class T>
Task<> HoldResource(ResourceRequest<T> request, std::shared_ptr<SceneLoadState> state, std::vector<std::shared_ptr<void>>& resources) {
	state->control->check();
	state->cancelResource = [request] { request.cancel(); };
	struct Reset {
		SceneLoadState& state;
		~Reset() {
			state.cancelResource = {};
		}
	} reset { *state };
	resources.push_back(co_await request.wait());
	state->control->check();
}
template <class D> Task<> PrepareComponent(D descriptor, std::shared_ptr<SceneLoadState> state, std::vector<std::shared_ptr<void>>& resources) {
	if constexpr (std::is_same_v<D, ECS::ParticleSystem2DComponent::Descriptor> || std::is_same_v<D, ECS::ParticleSystem3DComponent::Descriptor>) {
		co_await HoldResource(ServiceManager::Get<ParticleLoader>().requestResource(descriptor.EffectPath), state, resources);
	}
	else if constexpr (std::is_same_v<D, ECS::ModelRenderer::Descriptor>) {
		if (!descriptor.Path.empty()) {
			co_await HoldResource(ServiceManager::Get<ModelLoader>().requestResource(
									  descriptor.Path, descriptor.Path.ends_with(".model") ? ELoadingType::RESOURCE : ELoadingType::FILE),
				state, resources);
		}
	}
	else if constexpr (std::is_same_v<D, ECS::ModelLODRenderer::Descriptor>) {
		for (const auto& model : descriptor.Paths) {
			if (!model.Path.empty()) {
				co_await HoldResource(ServiceManager::Get<ModelLoader>().requestResource(
										  model.Path, model.Path.ends_with(".model") ? ELoadingType::RESOURCE : ELoadingType::FILE),
					state, resources);
			}
		}
	}

	else if constexpr (std::is_same_v<D, ECS::SurfaceComponent::Descriptor>) {
		descriptor.Surface.validate();
		for (const auto& layer : descriptor.Surface.Layers) {
			if (!layer.Material.empty()) {
				co_await HoldResource(ServiceManager::Get<MaterialLoader>().requestResource(layer.Material), state, resources);
			}
		}
		if (!descriptor.Surface.Water.Material.empty()) {
			co_await HoldResource(ServiceManager::Get<MaterialLoader>().requestResource(descriptor.Surface.Water.Material), state, resources);
		}
	}
	else if constexpr (std::is_same_v<D, ECS::FoliageComponent::Descriptor>) {
		for (const auto& species : descriptor.Species) {
			species.validate();
			for (const auto& lod : species.Lods) {
				co_await HoldResource(ServiceManager::Get<ModelLoader>().requestResource(
										  lod.Model, lod.Model.ends_with(".model") ? ELoadingType::RESOURCE : ELoadingType::FILE),
					state, resources);
				if (!lod.Material.empty()) {
					co_await HoldResource(ServiceManager::Get<MaterialLoader>().requestResource(lod.Material), state, resources);
				}
			}
			for (const auto& material : species.Materials) {
				if (!material.empty()) {
					co_await HoldResource(ServiceManager::Get<MaterialLoader>().requestResource(material), state, resources);
				}
			}
			for (const auto& binding : species.MaterialBindings) {
				co_await HoldResource(ServiceManager::Get<MaterialLoader>().requestResource(binding.Material), state, resources);
			}
		}
	}
	else if constexpr (std::is_same_v<D, ECS::MaterialRenderer::Descriptor>) {
		for (const auto& path : descriptor.Materials) {
			if (!path.empty()) {
				co_await HoldResource(ServiceManager::Get<MaterialLoader>().requestResource(path), state, resources);
			}
		}
	}
	else if constexpr (std::is_same_v<D, ECS::SpriteComponent::Descriptor>) {
		if (!descriptor.TexturePath.empty()) {
			co_await HoldResource(ServiceManager::Get<TextureLoader>().requestSprite(descriptor.TexturePath), state, resources);
		}
		if (!descriptor.TextureAtlasPath.empty()) {
			co_await HoldResource(ServiceManager::Get<TextureLoader>().requestAtlas(descriptor.TextureAtlasPath), state, resources);
		}
	}
#if USE_PHYSICS_3D
	else if constexpr (std::is_same_v<D, ECS::RagdollComponent::Descriptor>) {
		if (!descriptor.Definition.empty()) {
			co_await HoldResource(ServiceManager::Get<RagdollLoader>().requestResource(descriptor.Definition), state, resources);
		}
	}
	else if constexpr (std::is_same_v<D, ECS::DestructibleMeshComponent::Descriptor>) {
		if (!descriptor.Definition.empty()) {
			co_await HoldResource(ServiceManager::Get<DestructibleLoader>().requestResource(descriptor.Definition), state, resources);
		}
	}
#endif
	else if constexpr (std::is_same_v<D, ECS::SkeletalComponent::Descriptor>) {
		if (!descriptor.SkeletalPath.empty()) {
			co_await HoldResource(
				ServiceManager::Get<SkeletonLoader>().requestResource(descriptor.SkeletalPath, ELoadingType::FILE), state, resources);
		}
	}
	else if constexpr (std::is_same_v<D, ECS::SkeletalAnimationComponent::Descriptor>) {
		const auto& path = descriptor.SkeletalPlayablePath;
		if (path.ends_with(".stategraph")) {
			co_await HoldResource(ServiceManager::Get<SkeletonStateGraphLoader>().requestResource(path), state, resources);
		}
		else if (path.ends_with(".blendspace1d") || path.ends_with(".blendspace2d")) {
			co_await HoldResource(ServiceManager::Get<SkeletonBlendspaceLoader>().requestResource(path), state, resources);
		}
		else if (!path.empty()) {
			co_await HoldResource(ServiceManager::Get<SkeletonAnimationLoader>().requestResource(path), state, resources);
		}
	}
	else if constexpr (std::is_same_v<D, ECS::AudioComponent::Descriptor>) {
		if (!descriptor.AudioPath.empty()) {
			co_await HoldResource(ServiceManager::Get<AudioSourceLoader>().requestResource(
									  descriptor.AudioPath, descriptor.AudioPath.ends_with(".sound") ? ELoadingType::RESOURCE : ELoadingType::FILE),
				state, resources);
		}
	}
	else if constexpr (std::is_same_v<D, ECS::SdfTextComponent::Descriptor>) {
		if (!descriptor.Font.empty()) {
			co_await HoldResource(ServiceManager::Get<FontLoader>().requestResource(descriptor.Font), state, resources);
		}
	}
	co_return;
}

void EnsureComponentPreparers() {
	static const bool ready = [] {
		auto bind = [](std::type_index type, auto read) {
			ECS::ComponentCatalog::instance().setPrepare(type,
				[read](const nlohmann::json& json, std::shared_ptr<SceneLoadState> state, std::vector<std::shared_ptr<void>>& resources) -> Task<> {
					auto document = json;
					co_await PrepareComponent(read(document), state, resources);
				});
		};
		bind(typeid(ECS::ParticleSystem2DComponent),
			[](nlohmann::json& document) { return serde::deserialize<ECS::ParticleSystem2DComponent::Descriptor>(document); });
		bind(typeid(ECS::ParticleSystem3DComponent),
			[](nlohmann::json& document) { return serde::deserialize<ECS::ParticleSystem3DComponent::Descriptor>(document); });
		bind(typeid(ECS::ModelRenderer), [](nlohmann::json& document) { return serde::deserialize<ECS::ModelRenderer::Descriptor>(document); });
		bind(typeid(ECS::ModelLODRenderer), [](nlohmann::json& document) { return serde::deserialize<ECS::ModelLODRenderer::Descriptor>(document); });
		bind(typeid(ECS::SurfaceComponent),
			[](nlohmann::json& document) { return DATA::readDescriptor<ECS::SurfaceComponent::Descriptor>(document); });
		bind(typeid(ECS::FoliageComponent),
			[](nlohmann::json& document) { return DATA::readDescriptor<ECS::FoliageComponent::Descriptor>(document); });
		bind(typeid(ECS::MaterialRenderer), [](nlohmann::json& document) { return serde::deserialize<ECS::MaterialRenderer::Descriptor>(document); });
		bind(typeid(ECS::SpriteComponent), [](nlohmann::json& document) { return serde::deserialize<ECS::SpriteComponent::Descriptor>(document); });
#if USE_PHYSICS_3D
		bind(typeid(ECS::RagdollComponent), [](nlohmann::json& document) { return serde::deserialize<ECS::RagdollComponent::Descriptor>(document); });
		bind(typeid(ECS::DestructibleMeshComponent),
			[](nlohmann::json& document) { return serde::deserialize<ECS::DestructibleMeshComponent::Descriptor>(document); });
#endif
		bind(typeid(ECS::SkeletalComponent),
			[](nlohmann::json& document) { return serde::deserialize<ECS::SkeletalComponent::Descriptor>(document); });
		bind(typeid(ECS::SkeletalAnimationComponent),
			[](nlohmann::json& document) { return serde::deserialize<ECS::SkeletalAnimationComponent::Descriptor>(document); });
		bind(typeid(ECS::AudioComponent), [](nlohmann::json& document) { return serde::deserialize<ECS::AudioComponent::Descriptor>(document); });
		bind(typeid(ECS::SdfTextComponent), [](nlohmann::json& document) { return serde::deserialize<ECS::SdfTextComponent::Descriptor>(document); });
		return true;
	}();
	(void)ready;
}

Task<> WarmLoadingSceneMaterials(std::shared_ptr<SceneLoadState> state, std::vector<std::shared_ptr<void>>& resources) {
	for (const auto material : { "gui.mat", "text.mat", "vector.mat", "sdfText.mat" }) {
		co_await HoldResource(
			ServiceManager::Get<MaterialLoader>().requestResource(std::string("/materials/render/portable/") + material), state, resources);
	}
}
}

void IKIGAI::ConfigureSceneResourcePreparation(SceneManager& sceneManager) {
	EnsureComponentPreparers();
	sceneManager.setLoadingSceneWarmup(WarmLoadingSceneMaterials);
}
