#pragma once

#include <string>
#include <vector>

#include <coreModule/ecs/object.h>
#include <renderModule/drawable.h>

#include "coroutineModule/coroutineScope.h"
#include "resourceModule/serializerInterface.h"
#include "sceneDocument.h"
#include "utilsModule/ref.h"
#if USE_PHYSICS_3D
#include "destructionModule/destructionRuntime.h"
#endif

namespace IKIGAI::GUI {
class GuiObject;
}
#if USE_PHYSICS_3D
namespace IKIGAI::ECS {
class Physics3DSystem;
}
namespace IKIGAI::PHYSICS {
class PhysicWorld;
}
#endif
#if USE_PHYSICS_2D
namespace IKIGAI::ECS {
class Physics2DSystem;
}
namespace IKIGAI::PHYSICS2D {
class World;
}
#endif

namespace IKIGAI::RENDER {
class Camera;
class Frustum;
class Model;
enum class CullingOptions;
struct LightOGL;
struct Drawable;
}

namespace IKIGAI::SCENE_SYSTEM {
struct PhysicsFactories {
#if USE_PHYSICS_2D
	std::function<std::unique_ptr<PHYSICS2D::World>()> physics2d;
#endif
#if USE_PHYSICS_3D
	std::function<std::unique_ptr<PHYSICS::PhysicWorld>()> physics3d;
#endif
};
class Scene : public RESOURCES::Serializable {
public:
	struct Descriptor {
		std::vector<ECS::Object::Descriptor> Objects;

		template <class Context> constexpr static auto serde(Context& context, Descriptor& value) {
			using Self = Descriptor;
			using namespace serde::attribute;
			serde::serde_struct(context, value).field(&Self::Objects, "Objects");
		}
	};
	class IdGenerator_ : public IdGenerator<ECS::Object> { };

	explicit Scene(ECS::World* world = nullptr, PhysicsFactories physics = {});
	void setPhysicsFactories(PhysicsFactories physics);
	Scene(const Descriptor& _descriptor);
	virtual ~Scene();

	void init();
	void go();
	[[nodiscard]] bool getIsExecute() const;

	void update(std::chrono::duration<double> dt);
	void fixedUpdate(std::chrono::duration<double> dt);
	void finishFixedUpdate(std::chrono::duration<double> dt);
	void lateUpdate(std::chrono::duration<double> dt);
#if USE_PHYSICS_3D
	PHYSICS::PhysicWorld& physics3d();
	std::shared_ptr<PHYSICS::PhysicWorld> physics3dShared();
	void stepPhysics3D(std::chrono::duration<double> dt, bool beginDestructionFrame = true);
	void presentDestructibles(float alpha);
	DESTRUCTION::RuntimeSettings destructionSettings {};
	std::shared_ptr<DESTRUCTION::DestructionRuntime> destructionRuntime {};
#ifdef USE_EDITOR
	void drawPhysics3DDebug();
#endif
#endif
#if USE_PHYSICS_2D
	PHYSICS2D::World& physics2d();
	void stepPhysics2D(std::chrono::duration<double> dt);
#ifdef USE_EDITOR
	void drawPhysics2DDebug();
#endif
#endif

	std::shared_ptr<ECS::Object> createObject();
	std::shared_ptr<ECS::Object> createObject(const std::string& p_name, const std::string& p_tag = "");
	std::shared_ptr<ECS::Object> createObjectAfter(ECS::Entity parentId, const std::string& p_name, const std::string& p_tag = "");
	std::shared_ptr<ECS::Object> createObjectBefore(ECS::Entity parentId, const std::string& p_name, const std::string& p_tag = "");
	// std::shared_ptr<ECS::Object> _createObject(const std::string& p_name, const std::string& p_tag = "");
	std::shared_ptr<ECS::Object> createObject(ECS::Entity actorID, const std::string& name, const std::string& tag = "");
	std::shared_ptr<ECS::Object> createObject(const ECS::Object::Descriptor& data);
	bool destroyObject(std::shared_ptr<ECS::Object> p_target);

	std::shared_ptr<ECS::Object> findObjectByName(const std::string& p_name);
	std::shared_ptr<ECS::Object> findObjectByTag(const std::string& p_tag);
	std::shared_ptr<ECS::Object> findObjectByID(ECS::Entity p_id);

	std::vector<std::shared_ptr<ECS::Object>> findObjectsByName(const std::string& p_name);
	std::vector<std::shared_ptr<ECS::Object>> findObjectsByTag(const std::string& p_tag);

	[[nodiscard]] std::span<std::shared_ptr<ECS::Object>> getObjects();

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

	std::vector<std::shared_ptr<RENDER::MeshInterface>> getMeshesInFrustum(const RENDER::ModelInterface& model,
		const RENDER::BoundingSphere& modelBoundingSphere, const ECS::Transform& modelTransform, const RENDER::Frustum& frustum,
		RENDER::CullingOptions cullingOptions);

	std::vector<IKIGAI::RENDER::RenderChunk> findDrawables(const MATH::Vector3f& p_cameraPosition, const RENDER::Camera& p_camera,
		const RENDER::Frustum* p_customFrustum, std::shared_ptr<RENDER::MaterialInterface> p_defaultMaterial);

	std::vector<IKIGAI::RENDER::RenderChunk> findAndSortFrustumCulledBVHDrawables(
		const MATH::Vector3f& cameraPosition, const RENDER::Frustum& frustum, std::shared_ptr<RENDER::MaterialInterface> defaultMaterial);

	std::vector<IKIGAI::RENDER::RenderChunk> findAndSortFrustumCulledDrawables(
		const MATH::Vector3f& cameraPosition, const RENDER::Frustum& frustum, std::shared_ptr<RENDER::MaterialInterface> defaultMaterial);

	std::vector<IKIGAI::RENDER::RenderChunk> findAndSortDrawables(
		const MATH::Vector3f& cameraPosition, std::shared_ptr<RENDER::MaterialInterface> defaultMaterial);

	IKIGAI::UTILS::WeakPtr<IKIGAI::ECS::CameraComponent> findMainCamera();
	std::vector<RENDER::LightOGL> findLightData(std::vector<int>* objectIds = nullptr) const;
	std::vector<RENDER::LightOGL> findLightDataInFrustum(const RENDER::Frustum& frustum, std::vector<int>* objectIds = nullptr);

	std::vector<std::shared_ptr<IKIGAI::GUI::GuiObject>> guiObjs;

	void addToBVH(UTILS::WeakPtr<ECS::ComponentBase> component);
	void removeFromBVH(UTILS::WeakPtr<ECS::ComponentBase> component);
	void updateInBVH(UTILS::WeakPtr<ECS::ComponentBase> component);

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
	// Empty means the renderer's startup pipeline. Environment is supplied
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
	PhysicsFactories mPhysicsFactories;
#if USE_PHYSICS_3D
	std::unique_ptr<ECS::Physics3DSystem> mPhysics3D;
#endif
#if USE_PHYSICS_2D
	std::unique_ptr<ECS::Physics2DSystem> mPhysics2D;
#endif
	struct SpatialIndex;
	std::unique_ptr<SpatialIndex> mSpatial;
	SpatialIndex& spatial();
	ECS::World* mWorld = nullptr;
	std::unique_ptr<COROUTINE::CoroutineScope> mCoroutineScope;
	bool isExecute = false;
	std::vector<std::shared_ptr<ECS::Object>> objects;

	// std::unique_ptr<ECS::Object> skyboxObject;
	// std::unique_ptr<RESOURCES::CubeMap> skyboxTexture;
};
};
