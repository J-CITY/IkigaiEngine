#pragma once

#include <optional>
#include <string>
#include <vector>

#include <coreModule/ecs/object.h>

#include "coroutineModule/coroutineScope.h"
#include "resourceModule/serializerInterface.h"
#include "sceneDocument.h"
#include "sceneObjectDescriptor.h"
#include "sceneSimulation.h"
#include "utilsModule/ref.h"
namespace IKIGAI::GUI {
class GuiObject;
}
namespace IKIGAI::SCENE_SYSTEM {
class Scene : public RESOURCES::Serializable {
public:
	struct Descriptor {
		std::vector<SceneObjectDocument> Objects;

		template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
			using Self = Descriptor;
			using namespace serde::attribute;
			serde::serde_struct(context, value).field(&Self::Objects, "Objects");
		}
	};
	class IdGenerator_ : public IdGenerator<ECS::Object> { };

	explicit Scene(ECS::World* world = nullptr, std::unique_ptr<SceneSimulation> simulation = {});
	void setPhysicsFactories(PhysicsFactories physics);
	[[nodiscard]] SceneSimulation* simulation() noexcept {
		return mSimulation.get();
	}
	Scene(const Descriptor& _descriptor);
	virtual ~Scene();

	void init();
	void go();
	[[nodiscard]] bool getIsExecute() const;

	void beginSimulationFrame();
	void simulateFixedStep(std::chrono::duration<double> dt);
	void simulateFrame(std::chrono::duration<double> dt, float interpolationAlpha);

	// The loading scene calls update and lateUpdate directly.
	void update(std::chrono::duration<double> dt);
	void fixedUpdate(std::chrono::duration<double> dt);
	void finishFixedUpdate(std::chrono::duration<double> dt);
	void lateUpdate(std::chrono::duration<double> dt);
#if USE_PHYSICS_3D
	PHYSICS::PhysicWorld& physics3d();
	std::shared_ptr<PHYSICS::PhysicWorld> physics3dShared();
	void stepPhysics3D(std::chrono::duration<double> dt, bool beginDestructionFrame = true);
	bool physics3dPaused() const;
	void setPhysics3dPaused(bool paused);
	void presentDestructibles(float alpha);
#endif
#if USE_PHYSICS_2D
	PHYSICS2D::World& physics2d();
	void stepPhysics2D(std::chrono::duration<double> dt);
#endif

	std::shared_ptr<ECS::Object> createObject();
	std::shared_ptr<ECS::Object> createObject(const std::string& p_name, const std::string& p_tag = "");
	std::shared_ptr<ECS::Object> createObjectAfter(ECS::Entity parentId, const std::string& p_name, const std::string& p_tag = "");
	std::shared_ptr<ECS::Object> createObjectBefore(ECS::Entity parentId, const std::string& p_name, const std::string& p_tag = "");
	// std::shared_ptr<ECS::Object> _createObject(const std::string& p_name, const std::string& p_tag = "");
	std::shared_ptr<ECS::Object> createObject(ECS::Entity actorID, const std::string& name, const std::string& tag = "");
	std::shared_ptr<ECS::Object> createObject(const SceneObjectDescriptor& data);
	bool destroyObject(std::shared_ptr<ECS::Object> p_target);

	std::shared_ptr<ECS::Object> findObjectByName(const std::string& p_name);
	std::shared_ptr<ECS::Object> findObjectByTag(const std::string& p_tag);
	std::shared_ptr<ECS::Object> findObjectByID(ECS::Entity p_id);

	std::vector<std::shared_ptr<ECS::Object>> findObjectsByName(const std::string& p_name);
	std::vector<std::shared_ptr<ECS::Object>> findObjectsByTag(const std::string& p_tag);

	[[nodiscard]] std::span<const std::shared_ptr<ECS::Object>> getObjects() const;
	// Current position in this scene, including when multiple scenes share a world.
	[[nodiscard]] std::optional<std::size_t> getObjectIndex(const ECS::Object& object) const noexcept;

	virtual void onDeserialize(nlohmann::json& j) override {
		// for (auto& oj : j["objects"]) {
		//	auto obj = createObject();
		//	obj->onDeserialize(oj);
		// }
		// for (auto& oj : j["objects"]) {
		//	auto obj = findObjectByID(ObjectId<ECS::Object>(oj["id"]));
		//	auto parent = findObjectByID(ObjectId<ECS::Object>(oj["parent"]));
		//	obj->setParent(parent);
		// }
	}
	virtual void onSerialize(nlohmann::json& j) override {
		//	for (auto& obj : objects) {
		//		obj->onSerialize(j["objects"]);
		//	}
	}

	//[[nodiscard]] ECS::Object& getSkybox() const;
	//[[nodiscard]] RESOURCES::CubeMap& getSkyboxTexture() const;

	IKIGAI::UTILS::WeakPtr<IKIGAI::ECS::CameraComponent> findMainCamera();
	// Advance once after all views have recorded a game frame.
	void commitRenderTransforms();

	std::vector<std::shared_ptr<IKIGAI::GUI::GuiObject>> guiObjs;

	enum class ComponentChange { Added, Removed, Updated };
	using ComponentChanges = EVENT::Event<ComponentChange, UTILS::WeakPtr<ECS::ComponentBase>>;
	std::weak_ptr<ComponentChanges> componentChanges() const;
	void notifyComponentChange(ComponentChange change, UTILS::WeakPtr<ECS::ComponentBase> component);

	bool isSceneReady = false;
	void postLoad();
	void postLoadObject(ECS::Object& object);
	ECS::World& getWorld() const;
	std::shared_ptr<ECS::Object> createDeferredObject(const ECS::Object::Descriptor& descriptor);
	void removeLastObject();
	COROUTINE::CoroutineScope& coroutines();
	void cancelTasks();
	std::vector<DOCUMENT::Instance> prefabInstances;
	std::shared_ptr<void> lifetime = std::make_shared<int>(0);
	// Empty means render.json's default pipeline (or built-in forward). Environment is supplied
	// by the selected pipeline's SettingsAsset.
	std::string renderPipelineAsset;
	std::uint64_t renderTopologyRevision = 0;
	const DOCUMENT::Instance* prefabFor(int objectId) const;
	bool isPrefabRoot(int objectId) const;
	nlohmann::json serializeObjects() const;
	void capturePrefabBaselines();
	void unpackPrefab(int rootId);
	std::shared_ptr<ECS::Object> appendDocument(const DOCUMENT::Expanded& document);
	void destroySubtree(int rootId);
	void restoreObjectPosition(int rootId, std::size_t objectIndex, int siblingIndex);

private:
	friend class SceneManager;
	std::shared_ptr<ECS::Object> insertObject(std::shared_ptr<ECS::Object> object, std::size_t index);
	void refreshObjectIndices(std::size_t first) noexcept;
	std::shared_ptr<ECS::Object> appendDocument(const DOCUMENT::Expanded& document, std::span<const SceneObjectDescriptor> descriptors);
	std::unique_ptr<SceneSimulation> mSimulation;
	SceneSimulation& requireSimulation();
	void runScheduledPhase(ECS::SystemPhase phase, std::chrono::duration<double> dt = {}, float interpolationAlpha = 0.f);
	void observeComponents(ECS::Object& object);
	std::shared_ptr<ComponentChanges> mComponentChanges = std::make_shared<ComponentChanges>();
	ECS::World* mWorld = nullptr;
	std::unique_ptr<COROUTINE::CoroutineScope> mCoroutineScope;
	bool mSystemsRegistered = false;
	bool mStarting = false;
	bool isExecute = false;
	std::vector<std::shared_ptr<ECS::Object>> objects;

	// std::unique_ptr<ECS::Object> skyboxObject;
	// std::unique_ptr<RESOURCES::CubeMap> skyboxTexture;
};
};
