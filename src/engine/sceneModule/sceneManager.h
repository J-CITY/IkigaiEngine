#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ecsModule/systemBatchExecutor.h"
#include "scene.h"
#include "sceneLoading.h"

namespace IKIGAI::SCENE_SYSTEM {
class SceneManager {
public:
	using LoadingSceneWarmup = std::function<COROUTINE::Task<>(std::shared_ptr<SceneLoadState>, std::vector<std::shared_ptr<void>>&)>;

	SceneManager(const std::string& p_sceneRootFolder = "", PhysicsFactories physics = {}, bool profileLoading = false,
		SceneSimulationFactory simulationFactory = nullptr);
	// Applies to subsequently created/loaded scenes, not existing worlds.
	void setPhysicsFactoriesForNewScenes(PhysicsFactories physics) {
		mPhysicsFactories = std::move(physics);
	}
	~SceneManager();

	void setLoadingSceneWarmup(LoadingSceneWarmup warmup) {
		mLoadingSceneWarmup = std::move(warmup);
	}
	void setLoadingSceneBatchExecutor(ECS::SystemBatchExecutor executor);

	void update();
	void loadFromFile(const std::string& sceneFilePath);
	SceneLoadHandle loadFromFileAsync(std::string path);
	SceneLoadHandle prepareLoadingScene(std::string path);
	SceneLoadHandle instantiatePrefabAsync(std::string path, int parentId = -1);
	void createPrefab(int rootId, const std::string& path);
	nlohmann::json serializeScene() const;
	bool isPrefabLoading() const {
		return mPrefabLoad && !mPrefabLoad->done();
	}
	bool isEditingLocked() const {
		return isTransitioning() || isPrefabLoading();
	}
	SceneTransitionState getTransitionState() const {
		return mTransitionState;
	}
	bool isTransitioning() const {
		return mTransitionState != SceneTransitionState::Idle;
	}
	bool isPreparingLoadingScene() const {
		return mCacheLoad && !mCacheLoad->done();
	}
	Scene* getPresentedScene() const;
	void updateLoadingScene(std::chrono::duration<double> dt);
	void notifyPresented() {
		mLoadingFramePresented = true;
	}
	SceneLoadHandle getLoadOperation() const {
		return SceneLoadHandle(mActiveLoad);
	}
	void shutdownLoading();
	void saveToFile(const std::string& sceneFilePath);
	void saveToFile();
	void loadEmptyScene();
	void loadDefaultScene();
	void unloadCurrentScene();
	bool hasCurrentScene() const;
	Scene& getCurrentScene();
	[[nodiscard]] std::string getCurrentSceneSourcePath() const;
	void setCurrentSceneSourcePath(const std::string& name);

public:
	EVENT::Event<> sceneLoadEvent;
	EVENT::Event<> sceneUnloadEvent;
	EVENT::Event<const std::string&> surrentSceneSourcePathChangedEvent;

private:
	PhysicsFactories mPhysicsFactories;
	bool mProfileLoading = false;
	SceneSimulationFactory mSimulationFactory = nullptr;
	std::unique_ptr<SceneSimulation> makeSimulation(const nlohmann::json* document = nullptr) const;
	LoadingSceneWarmup mLoadingSceneWarmup;
	ECS::SystemBatchExecutor mLoadingSceneBatchExecutor;
	struct LoadContext;
	struct LoadingSceneCache;
	std::unique_ptr<LoadContext> mBuild;
	std::unique_ptr<LoadContext> mCacheBuild;
	std::unique_ptr<LoadingSceneCache> mLoadingCache;
	SceneTransitionState mTransitionState = SceneTransitionState::Idle;
	std::shared_ptr<SceneLoadState> mActiveLoad;
	std::shared_ptr<SceneLoadState> mQueuedLoad;
	std::shared_ptr<SceneLoadState> mCacheLoad;
	COROUTINE::TaskHandle mTransitionTask;
	COROUTINE::TaskHandle mCacheTask;
	COROUTINE::TaskHandle mPrefabTask;
	std::shared_ptr<SceneLoadState> mPrefabLoad;
	bool mLoadingFramePresented = false;
	COROUTINE::Task<> transition(std::shared_ptr<SceneLoadState> state);
	COROUTINE::Task<> buildScene(LoadContext& context, std::shared_ptr<SceneLoadState> state, bool lightweight);
	COROUTINE::Task<> clearScene(Scene& scene);
	COROUTINE::Task<> cacheLoadingScene(std::shared_ptr<SceneLoadState> state);
	COROUTINE::Task<> instantiatePrefab(std::shared_ptr<SceneLoadState> state, int parentId);
	void startQueuedLoad();
	const std::string m_sceneRootFolder;
	std::unique_ptr<Scene> m_currentScene = nullptr;

	std::string m_currentSceneSourcePath;
	std::function<void()> m_delayedLoadCall;
};
}
