#include "resourceModule/audioSourceManager.h"
#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/fontManager.h"
#include "resourceModule/materialManager.h"
#include "resourceModule/modelManager.h"
#include "resourceModule/particleManager.h"
#include "resourceModule/serviceManager.h"
#include "resourceModule/shaderManager.h"
#include "resourceModule/skeletonAnimationManager.h"
#include "resourceModule/skeletonBlendspaceManager.h"
#include "resourceModule/skeletonManager.h"
#include "resourceModule/skeletonStateGraphManager.h"
#include "resourceModule/textureManager.h"
#include "utilsModule/serialization/typedDescriptor.h"
#if USE_PHYSICS_3D
#include "resourceModule/destructibleManager.h"
#include "resourceModule/ragdollManager.h"
#endif
#include "sceneLoadingContext.h"
#include "utilsModule/log/loggerDefine.h"
#include "utilsModule/time/loadTimer.h"
#include <unordered_map>
#include <unordered_set>

using namespace IKIGAI;
using namespace IKIGAI::SCENE_SYSTEM;
using namespace IKIGAI::COROUTINE;
using namespace IKIGAI::RESOURCES;

namespace {
template <class... T> void RegisterComponents(ECS::World& world, ECS::ComponentsTypeProvider<T...>) {
	(world.getComponentManager()->registerComponent<T>(ECS::StorageType::Sparse), ...);
}
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
template <class D> bool IsLightweight(const D&) {
	return std::is_same_v<D, ECS::TransformComponent::Descriptor> || std::is_same_v<D, ECS::CameraComponent::Descriptor>
		|| std::is_same_v<D, ECS::RootGuiComponent::Descriptor> || std::is_same_v<D, ECS::SpriteComponent::Descriptor>
		|| std::is_same_v<D, ECS::SpriteAnimateComponent::Descriptor> || std::is_same_v<D, ECS::LabelComponent::Descriptor>
		|| std::is_same_v<D, ECS::SdfTextComponent::Descriptor> || std::is_same_v<D, ECS::VectorShapeComponent::Descriptor>
		|| std::is_same_v<D, ECS::VectorPathComponent::Descriptor> || std::is_same_v<D, ECS::VectorCanvasComponent::Descriptor>
		|| std::is_same_v<D, ECS::InteractionComponent::Descriptor> || std::is_same_v<D, ECS::ClipComponent::Descriptor>
		|| std::is_same_v<D, ECS::ScrollComponent::Descriptor> || std::is_same_v<D, ECS::LayoutComponent::Descriptor>;
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
}

SceneLoadHandle SceneManager::loadFromFileAsync(std::string path) {
	auto state = std::make_shared<SceneLoadState>();
	state->path = std::move(path);
	if (mPrefabLoad && !mPrefabLoad->done()) {
		mPrefabLoad->cancel();
	}
	if (mActiveLoad && !mActiveLoad->done()) {
		mActiveLoad->cancel();
	}
	if (mQueuedLoad) {
		mQueuedLoad->cancel();
		mQueuedLoad->status = SceneLoadStatus::Cancelled;
	}
	mQueuedLoad = state;
	startQueuedLoad();
	return SceneLoadHandle(state);
}

void SceneManager::startQueuedLoad() {
	if (!mQueuedLoad || isPrefabLoading() || (mTransitionTask && !mTransitionTask.done())) {
		return;
	}
	mActiveLoad = std::exchange(mQueuedLoad, {});
	if (mActiveLoad->control->cancelled) {
		mActiveLoad->status = SceneLoadStatus::Cancelled;
		return;
	}
	mTransitionState = SceneTransitionState::ShowingLoadingScene;
	mLoadingFramePresented = false;
	mTransitionTask = ServiceManager::Get<Scheduler>().start(transition(mActiveLoad), "Scene: " + mActiveLoad->path);
}

Scene* SceneManager::getPresentedScene() const {
	if (isTransitioning()) {
		return mLoadingCache ? mLoadingCache->scene.get() : nullptr;
	}
	return m_currentScene.get();
}
void SceneManager::updateLoadingScene(std::chrono::duration<double> dt) {
	if (isTransitioning() && mLoadingCache) {
		mLoadingCache->scene->update(dt);
		mLoadingCache->scene->lateUpdate(dt);
	}
}

Task<> SceneManager::clearScene(Scene& scene) {
	scene.cancelTasks();
	scene.isSceneReady = false;
	// Break owning parent->child edges before popping objects, so destroying a
	// root does not recursively delete an entire hierarchy in a single step.
	for (auto& object : scene.getObjects()) {
		object->detachFromParent();
		co_await YieldIfBudgetExceeded();
	}
	while (!scene.getObjects().empty()) {
		scene.removeLastObject();
		co_await YieldIfBudgetExceeded();
	}
}

Task<> SceneManager::buildScene(LoadContext& context, std::shared_ptr<SceneLoadState> state, bool lightweight) {
	UTILS::LoadTimer timing("scene-build", state->path);
	state->phase = "Reading";
	auto file = ServiceManager::Get<FileSystem>().getFile(state->path, FileMode::READ);
	if (!file || !file->isValid()) {
		throw std::runtime_error("Scene file not found: " + state->path);
	}
	auto bytes = co_await file->readAllTask(state->control);
	state->control->check();
	auto json = nlohmann::json::parse(bytes);
	const auto pipeline = json.value("RenderPipeline", std::string {});
#if USE_PHYSICS_3D
	const auto destruction = DATA::readDescriptor<DESTRUCTION::RuntimeSettings>(json.value("Destruction", nlohmann::json::object()));
	destruction.validate();
#endif
	state->phase = "Expanding prefabs";
	DOCUMENT::Resolver resolver(json.at("Objects"));
	DOCUMENT::Expanded expanded;
	std::map<std::string, nlohmann::json> prefabCache;
	for (auto entry : json.at("Objects")) {
		state->control->check();
		nlohmann::json asset;
		if (entry.contains("Prefab")) {
			const auto path = DOCUMENT::Path(entry.at("Prefab").get<std::string>());
			if (!prefabCache.contains(path)) {
				auto prefabFile = ServiceManager::Get<FileSystem>().getFile(path);
				if (!prefabFile || !prefabFile->isValid()) {
					throw std::runtime_error("Prefab not found: " + path);
				}
				auto bytes = co_await prefabFile->readAllTask(state->control);
				prefabCache[path] = nlohmann::json::parse(bytes);
			}
			asset = prefabCache.at(path);
		}
		auto part = resolver.Expand(std::move(entry), asset);
		for (auto& object : part.objects) {
			expanded.objects.push_back(std::move(object));
			co_await YieldIfBudgetExceeded();
		}
		for (auto& instance : part.instances) {
			expanded.instances.push_back(std::move(instance));
		}
	}
	DOCUMENT::Validate(expanded.objects);
	state->phase = "Preparing descriptors";
	std::unordered_map<int, std::size_t> ids;
	for (auto& value : expanded.objects) {
		state->control->check();
		auto parsed = UTILS::FromJson<ECS::Object::Descriptor>(value);
		if (parsed.isErr()) {
			throw std::runtime_error(parsed.unwrapErr().text);
		}
		auto descriptor = parsed.unwrap();
		if (descriptor.Id <= 0 || !ids.emplace(descriptor.Id, context.descriptors.size()).second) {
			throw std::runtime_error("Invalid or duplicate entity ID in scene");
		}
		bool transform = false;
		for (const auto& component : descriptor.Components) {
			std::visit(
				[&](const auto& data) {
					using D = std::decay_t<decltype(data)>;
					transform |= std::is_same_v<D, ECS::TransformComponent::Descriptor>;
					if (lightweight && !IsLightweight(data)) {
						throw std::runtime_error("Loading scene supports only camera and lightweight GUI components");
					}
				},
				component);
		}
		if (!transform) {
			throw std::runtime_error("Scene object has no TransformComponent");
		}
		context.descriptors.push_back(std::move(descriptor));
		co_await YieldIfBudgetExceeded();
	}
	// Iterative linear validation: no recursion on deep parent chains.
	std::vector<unsigned char> colors(context.descriptors.size());
	for (std::size_t start = 0; start < context.descriptors.size(); ++start) {
		std::vector<std::size_t> chain;
		auto index = start;
		while (colors[index] != 2) {
			if (colors[index] == 1) {
				throw std::runtime_error("Scene parent cycle");
			}
			colors[index] = 1;
			chain.push_back(index);
			const auto parent = context.descriptors[index].ParentId;
			if (parent <= 0) {
				break;
			}
			const auto it = ids.find(parent);
			if (it == ids.end()) {
				throw std::runtime_error("Scene references a missing parent");
			}
			index = it->second;
			co_await YieldIfBudgetExceeded();
		}
		for (auto item : chain) {
			colors[item] = 2;
		}
		state->control->check();
		co_await YieldIfBudgetExceeded();
	}
	state->phase = "Loading resources";
	state->progress = .1;
	for (std::size_t i = 0; i < context.descriptors.size(); ++i) {
		for (const auto& component : context.descriptors[i].Components) {
			state->control->check();
			co_await std::visit([&](const auto& data) { return PrepareComponent(data, state, context.resources); }, component);
		}
		state->progress = .1 + .4 * double(i + 1) / std::max<std::size_t>(1, context.descriptors.size());
		co_await YieldIfBudgetExceeded();
	}
	if (!lightweight) {
		mTransitionState = SceneTransitionState::Unloading;
		state->phase = "Unloading old scene";
		if (m_currentScene) {
			co_await clearScene(*m_currentScene);
			m_currentScene.reset();
			m_currentSceneSourcePath.clear();
			sceneUnloadEvent.run();
		}
		mTransitionState = SceneTransitionState::Loading;
	}
	state->control->check();
	state->phase = "Creating objects";
	context.scene = std::make_unique<Scene>(context.world.get(), mPhysicsFactories);
	context.scene->renderPipelineAsset = pipeline;
#if USE_PHYSICS_3D
	context.scene->destructionSettings = destruction;
#endif
	for (const auto& descriptor : context.descriptors) {
		state->control->check();
		context.scene->createDeferredObject(descriptor);
		co_await YieldIfBudgetExceeded();
	}
	auto objects = context.scene->getObjects();
	for (std::size_t i = 0; i < context.descriptors.size(); ++i) {
		for (const auto& component : context.descriptors[i].Components) {
			state->control->check();
			objects[i]->addSerializedComponent(component);
			co_await YieldIfBudgetExceeded();
		}
		state->progress = .5 + .25 * double(i + 1) / std::max<std::size_t>(1, objects.size());
	}
	state->phase = "Resolving references";
	for (std::size_t i = 0; i < objects.size(); ++i) {
		state->control->check();
		if (auto parent = context.descriptors[i].ParentId; parent > 0) {
			objects[i]->setParent(objects[ids.at(parent)]);
		}
		co_await YieldIfBudgetExceeded();
	}
	state->phase = "Finalizing";
	for (const auto& object : objects) {
		state->control->check();
		context.scene->postLoadObject(*object);
		co_await YieldIfBudgetExceeded();
	}

	for (const auto& object : objects) {
		if (auto foliage = object->getComponent<ECS::FoliageComponent>()) {
			foliage->bind(*context.scene);
			co_await YieldIfBudgetExceeded();
		}
	}
	context.scene->prefabInstances = std::move(expanded.instances);
	for (std::size_t i = 0; i < objects.size(); ++i) {
		objects[i]->setPendingActive(context.descriptors[i].IsActive);
	}
	context.scene->capturePrefabBaselines();
	context.scene->isSceneReady = true;
	state->progress = .9;
}

Task<> SceneManager::transition(std::shared_ptr<SceneLoadState> state) {
	UTILS::LoadTimer timing("scene-transition", state->path);
	state->status = SceneLoadStatus::Running;
	bool cancelled = false;
	bool committed = false;
	try {
		while (!mLoadingFramePresented) {
			state->control->check();
			co_await NextFrame();
		}
		state->control->check();
		mTransitionState = SceneTransitionState::Preparing;
		mBuild = std::make_unique<LoadContext>();
		co_await buildScene(*mBuild, state, false);
		state->control->check();
		mTransitionState = SceneTransitionState::Activating;
		state->phase = "Activating";
		m_currentScene = std::move(mBuild->scene);
		committed = true;
		auto objects = m_currentScene->getObjects();
		for (std::size_t i = 0; i < objects.size(); ++i) {
			objects[i]->setPendingActive(mBuild->descriptors[i].IsActive);
			co_await YieldIfBudgetExceeded();
		}
		for (std::size_t i = 0; i < objects.size(); ++i) {
			state->control->check();
			objects[i]->activateLoaded();
			co_await YieldIfBudgetExceeded();
		}
		while (!mBuild->resources.empty()) {
			mBuild->resources.pop_back();
			co_await YieldIfBudgetExceeded();
		}
		while (!mBuild->descriptors.empty()) {
			mBuild->descriptors.pop_back();
			co_await YieldIfBudgetExceeded();
		}
		state->control->check();
		m_currentSceneSourcePath = state->path;
		mTransitionState = SceneTransitionState::HidingLoadingScene;
		if (mLoadingCache) {
			mLoadingCache->scene->cancelTasks();
		}
		co_await NextFrame();
		state->control->check();
	} catch (const ResourceLoadCancelled&) {
		cancelled = true;
	} catch (const std::exception& error) {
		state->error = error.what();
	} catch (...) {
		state->error = "Unknown scene loading error";
	}
	if (cancelled || !state->error.empty()) {
		mTransitionState = SceneTransitionState::CleaningUp;
		state->phase = "Cleaning up";
		if (mBuild && mBuild->scene) {
			co_await clearScene(*mBuild->scene);
		}
		if (committed && m_currentScene) {
			co_await clearScene(*m_currentScene);
			m_currentScene.reset();
			m_currentSceneSourcePath.clear();
		}
		if (mBuild) {
			while (!mBuild->resources.empty()) {
				mBuild->resources.pop_back();
				co_await YieldIfBudgetExceeded();
			}
			while (!mBuild->descriptors.empty()) {
				mBuild->descriptors.pop_back();
				co_await YieldIfBudgetExceeded();
			}
		}
		mBuild.reset();
		state->status = cancelled ? SceneLoadStatus::Cancelled : SceneLoadStatus::Failed;
		mTransitionState = m_currentScene ? SceneTransitionState::Idle : SceneTransitionState::Failed;
		if (m_currentScene && mLoadingCache) {
			mLoadingCache->scene->cancelTasks();
		}
		if (!state->error.empty()) {
			LOG_ERROR << "Scene loading failed: " << state->path << ": " << state->error;
		}
	}
	else {
		mBuild.reset();
		state->status = SceneLoadStatus::Completed;
		state->progress = 1.0;
		state->phase = "Completed";
		mTransitionState = SceneTransitionState::Idle;
		sceneLoadEvent.run();
		surrentSceneSourcePathChangedEvent.run(m_currentSceneSourcePath);
	}
}

SceneLoadHandle SceneManager::prepareLoadingScene(std::string path) {
	if (mCacheTask && !mCacheTask.done()) {
		throw std::logic_error("Loading scene preparation is already running");
	}
	auto state = std::make_shared<SceneLoadState>();
	state->path = std::move(path);
	mCacheLoad = state;
	if (state->path.empty()) {
		mLoadingCache.reset();
		state->status = SceneLoadStatus::Completed;
		state->progress = 1;
		return SceneLoadHandle(state);
	}
	mCacheTask = ServiceManager::Get<Scheduler>().start(cacheLoadingScene(state), "Prepare loading scene");
	return SceneLoadHandle(state);
}

Task<> SceneManager::cacheLoadingScene(std::shared_ptr<SceneLoadState> state) {
	state->status = SceneLoadStatus::Running;
	bool cancelled = false;
	try {
		mCacheBuild = std::make_unique<LoadContext>();
		mCacheBuild->world = std::make_unique<ECS::World>();
		RegisterComponents(*mCacheBuild->world, ECS::ComponentsTypeProviderType {});
		for (const auto material : { "gui.mat", "text.mat", "vector.mat", "sdfText.mat" }) {
			co_await HoldResource(ServiceManager::Get<MaterialLoader>().requestResource(std::string("/materials/render/portable/") + material), state,
				mCacheBuild->resources);
		}
		co_await buildScene(*mCacheBuild, state, true);
		for (std::size_t i = 0; i < mCacheBuild->descriptors.size(); ++i) {
			mCacheBuild->scene->getObjects()[i]->setPendingActive(mCacheBuild->descriptors[i].IsActive);
		}
		for (std::size_t i = 0; i < mCacheBuild->descriptors.size(); ++i) {
			state->control->check();
			mCacheBuild->scene->getObjects()[i]->activateLoaded();
			co_await YieldIfBudgetExceeded();
		}
	} catch (const ResourceLoadCancelled&) {
		cancelled = true;
	} catch (const std::exception& error) {
		state->error = error.what();
	} catch (...) {
		state->error = "Unknown loading scene preparation error";
	}
	if (cancelled || !state->error.empty()) {
		if (mCacheBuild && mCacheBuild->scene) {
			co_await clearScene(*mCacheBuild->scene);
		}
		mCacheBuild.reset();
		state->status = cancelled ? SceneLoadStatus::Cancelled : SceneLoadStatus::Failed;
	}
	else {
		auto cache = std::make_unique<LoadingSceneCache>();
		cache->world = std::move(mCacheBuild->world);
		cache->scene = std::move(mCacheBuild->scene);
		cache->resources = std::move(mCacheBuild->resources);
		mLoadingCache = std::move(cache);
		mCacheBuild.reset();
		state->status = SceneLoadStatus::Completed;
		state->progress = 1;
		state->phase = "Completed";
	}
}

void SceneManager::shutdownLoading() {
	if (mPrefabLoad && !mPrefabLoad->done()) {
		mPrefabLoad->cancel();
		mPrefabLoad->status = SceneLoadStatus::Cancelled;
	}
	mPrefabTask.cancel();
	if (mActiveLoad && !mActiveLoad->done()) {
		mActiveLoad->cancel();
		mActiveLoad->status = SceneLoadStatus::Cancelled;
	}
	if (mQueuedLoad) {
		mQueuedLoad->cancel();
		mQueuedLoad->status = SceneLoadStatus::Cancelled;
		mQueuedLoad.reset();
	}
	if (mCacheLoad && !mCacheLoad->done()) {
		mCacheLoad->cancel();
		mCacheLoad->status = SceneLoadStatus::Cancelled;
	}
	mTransitionTask.cancel();
	mCacheTask.cancel();
	mBuild.reset();
	mCacheBuild.reset();
	mLoadingCache.reset();
	mTransitionState = SceneTransitionState::Idle;
}

SceneLoadHandle SceneManager::instantiatePrefabAsync(std::string path, int parentId) {
	if (isEditingLocked() || !m_currentScene) {
		throw std::logic_error("Another scene operation is running");
	}
	auto state = std::make_shared<SceneLoadState>();
	state->path = DOCUMENT::Path(std::move(path));
	mPrefabLoad = state;
	mPrefabTask = ServiceManager::Get<Scheduler>().start(instantiatePrefab(state, parentId), "Instantiate: " + state->path);
	return SceneLoadHandle(state);
}

Task<> SceneManager::instantiatePrefab(std::shared_ptr<SceneLoadState> state, int parentId) {
	state->status = SceneLoadStatus::Running;
	std::vector<std::shared_ptr<void>> resources;
	auto* target = m_currentScene.get();
	std::size_t firstObject = 0, firstInstance = 0;
	bool creating = false;
	bool cancelled = false;
	try {
		if (parentId > 0) {
			if (!target->findObjectByID(ECS::Entity(ECS::Entity::ID(parentId)))) {
				throw std::runtime_error("Instance parent not found");
			}
			if (target->prefabFor(parentId)) {
				throw std::runtime_error("Unpack prefab before adding a child");
			}
		}
		state->phase = "Reading prefab";
		auto file = ServiceManager::Get<FileSystem>().getFile(state->path);
		if (!file || !file->isValid()) {
			throw std::runtime_error("Prefab not found: " + state->path);
		}
		auto bytes = co_await file->readAllTask(state->control);
		auto asset = nlohmann::json::parse(bytes);
		auto entries = serializeScene().at("Objects");
		int root = 0;
		do {
			root = static_cast<int>(ECS::Entity().getUniqueId());
		} while (root <= 0 || target->findObjectByID(ECS::Entity(ECS::Entity::ID(root))));
		nlohmann::json entry { { "Prefab", state->path }, { "Id", root }, { "ParentId", parentId } };
		entries.push_back(entry);
		DOCUMENT::Resolver resolver(entries);
		auto expanded = resolver.Expand(entry, asset);
		auto combined = target->serializeObjects();
		for (auto object : expanded.objects) {
			combined.push_back(std::move(object));
		}
		DOCUMENT::Validate(combined);
		state->phase = "Loading prefab resources";
		std::vector<ECS::Object::Descriptor> descriptors;
		for (auto value : expanded.objects) {
			state->control->check();
			auto parsed = UTILS::FromJson<ECS::Object::Descriptor>(value);
			if (parsed.isErr()) {
				throw std::runtime_error(parsed.unwrapErr().text);
			}
			auto descriptor = parsed.unwrap();
			for (const auto& component : descriptor.Components) {
				co_await std::visit([&](const auto& data) { return PrepareComponent(data, state, resources); }, component);
			}
			descriptors.push_back(std::move(descriptor));
			co_await YieldIfBudgetExceeded();
		}
		state->control->check();
		if (m_currentScene.get() != target) {
			throw std::runtime_error("Scene changed during prefab preparation");
		}
		state->phase = "Creating prefab objects";
		// Only the new inactive shells are visible to the world during the
		// budgeted commit; simulation and editor mutations are paused.
		auto latest = target->serializeObjects();
		for (auto value : expanded.objects) {
			latest.push_back(std::move(value));
		}
		DOCUMENT::Validate(latest);
		firstObject = target->getObjects().size();
		firstInstance = target->prefabInstances.size();
		creating = true;
		for (const auto& descriptor : descriptors) {
			state->control->check();
			target->createDeferredObject(descriptor);
			co_await YieldIfBudgetExceeded();
		}
		for (std::size_t i = 0; i < descriptors.size(); ++i) {
			auto object = target->getObjects()[firstObject + i];
			for (const auto& component : descriptors[i].Components) {
				state->control->check();
				object->addSerializedComponent(component);
				co_await YieldIfBudgetExceeded();
			}
		}
		for (std::size_t i = 0; i < descriptors.size(); ++i) {
			state->control->check();
			auto object = target->getObjects()[firstObject + i];
			if (descriptors[i].ParentId > 0) {
				object->setParent(target->findObjectByID(ECS::Entity(ECS::Entity::ID(descriptors[i].ParentId))));
			}
			target->postLoadObject(*object);
			co_await YieldIfBudgetExceeded();
		}
		state->control->check();
		for (std::size_t i = 0; i < descriptors.size(); ++i) {
			target->getObjects()[firstObject + i]->setPendingActive(descriptors[i].IsActive);
		}
		auto instance = std::move(expanded.instances.front());
		auto canonical = target->serializeObjects();
		const auto ids = DOCUMENT::ReadMap(instance.entry);
		std::set<int> owned;
		for (auto [local, id] : ids) {
			owned.insert(id);
		}
		for (const auto& object : canonical) {
			if (owned.contains(DOCUMENT::Id(object.at("Id")))) {
				instance.baseline.push_back(object);
			}
		}
		target->prefabInstances.push_back(std::move(instance));
		for (std::size_t i = 0; i < descriptors.size(); ++i) {
			state->control->check();
			target->getObjects()[firstObject + i]->activateLoaded();
			co_await YieldIfBudgetExceeded();
		}
		state->resultRootId = root;
		state->control->check();
	} catch (const ResourceLoadCancelled&) {
		cancelled = true;
	} catch (const std::exception& e) {
		state->error = state->path + ": " + e.what();
	} catch (...) {
		state->error = "Unknown prefab loading error: " + state->path;
	}
	if (cancelled || !state->error.empty()) {
		state->phase = "Cleaning up prefab";
		if (creating && m_currentScene.get() == target) {
			while (target->getObjects().size() > firstObject) {
				target->removeLastObject();
				co_await YieldIfBudgetExceeded();
			}
			target->prefabInstances.resize(firstInstance);
		}
	}
	while (!resources.empty()) {
		resources.pop_back();
		co_await YieldIfBudgetExceeded();
	}
	state->status = cancelled ? SceneLoadStatus::Cancelled : !state->error.empty() ? SceneLoadStatus::Failed : SceneLoadStatus::Completed;
	state->phase = cancelled ? "Cancelled" : !state->error.empty() ? "Failed" : "Completed";
	if (state->status == SceneLoadStatus::Completed) {
		state->progress = 1.0;
	}
}
