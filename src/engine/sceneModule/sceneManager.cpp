#include "sceneManager.h"
#include "sceneLoadingContext.h"
#include "utilsModule/serialization/typedDescriptor.h"

#include <coreModule/ecs/components/ambientSphereLight.h>
#include <coreModule/ecs/components/cameraComponent.h>
#include <coreModule/ecs/components/directionalLight.h>
#include <coreModule/ecs/object.h>

#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "utilsModule/log/loggerDefine.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <utilsModule/pathGetter.h>

using namespace IKIGAI;
using namespace IKIGAI::SCENE_SYSTEM;

SceneManager::SceneManager(const std::string& p_sceneRootFolder, PhysicsFactories physics)
	: mPhysicsFactories(std::move(physics))
	, m_sceneRootFolder(p_sceneRootFolder) {
	loadEmptyScene();
}

SceneManager::~SceneManager() {
	shutdownLoading();
	unloadCurrentScene();
}

void SceneManager::update() {
	startQueuedLoad();
	if (m_delayedLoadCall) {
#ifdef __EMSCRIPTEN__
		std::cout << "[web] SceneManager delayed load" << std::endl;
		std::cout.flush();
#endif
		m_delayedLoadCall();
		m_delayedLoadCall = 0;
#ifdef __EMSCRIPTEN__
		std::cout << "[web] SceneManager delayed load done" << std::endl;
		std::cout.flush();
#endif
	}
}

void SceneManager::loadEmptyScene() {
	unloadCurrentScene();

	m_currentScene = std::make_unique<Scene>(nullptr, mPhysicsFactories);

	sceneLoadEvent.run();

	m_currentScene->postLoad();
}

void SceneManager::loadDefaultScene() {
	unloadCurrentScene();

	m_currentScene = std::make_unique<Scene>(nullptr, mPhysicsFactories);

	sceneLoadEvent.run();

	auto directionalLight = m_currentScene->createObject("Directional Light");
	directionalLight->addComponent<ECS::DirectionalLight>()->setIntensity(0.75f);
	directionalLight->getTransform()->setLocalPosition({ 0.0f, 10.0f, 0.0f });
	directionalLight->getTransform()->setLocalRotation(MATH::QuaternionF(MATH::Vector3 { 120.0f, -40.0f, 0.0f }));

	auto ambientLight = m_currentScene->createObject("Ambient Light");
	ambientLight->addComponent<ECS::AmbientSphereLight>()->setRadius(10000.0f);

	auto camera = m_currentScene->createObject("Main Camera");
	camera->addComponent<ECS::CameraComponent>();
	camera->getTransform()->setLocalRotation(MATH::QuaternionF(MATH::Vector3f { 20.0f, 180.0f, 0.0f }));
	camera->getTransform()->setLocalPosition({ 0.0f, 3.0f, 8.0f });

	m_currentScene->postLoad();
}

template <typename T> void addComponentImpl(const std::string& typeNameStr, std::shared_ptr<ECS::Object> obj, nlohmann::json& data) {
	if (typeNameStr == ECS::GetType<T>()) {
		auto c = obj->addComponent<T>();
		// auto result = IKIGAI::UTILS::FromJson<T>(*c, data);
		// if (result.isErr()) {
		//	LOG_ERROR << (result.unwrapErr().text);
		// }
	}
}

template <template <typename...> class Container, typename... ComponentType>
void addComponent(const std::string& typeNameStr, std::shared_ptr<ECS::Object> obj, nlohmann::json& data, Container<ComponentType...> opt) {
	(addComponentImpl<ComponentType>(typeNameStr, obj, data), ...);
}

void addComponent(const std::string& typeNameStr, std::shared_ptr<ECS::Object> obj, nlohmann::json& data) {
	addComponent(typeNameStr, obj, data, ECS::ComponentsTypeProviderType {});
}

//----------------------------

template <typename T> void saveComponentImpl(const std::string& typeNameStr, UTILS::WeakPtr<ECS::ComponentBase> component, nlohmann::json& data) {
	if (typeNameStr == ECS::GetType<T>()) {
		auto _p = std::static_pointer_cast<T>(component);
		// data = IKIGAI::UTILS::ToJson(*_p);
		// data["Type"] = typeid(T).name();
	}
}

template <template <typename...> class Container, typename... ComponentType>
void saveComponent(
	const std::string& typeNameStr, UTILS::WeakPtr<ECS::ComponentBase> component, nlohmann::json& data, Container<ComponentType...> opt) {
	(saveComponentImpl<ComponentType>(typeNameStr, component, data), ...);
}

void saveComponent(UTILS::WeakPtr<ECS::ComponentBase> component, nlohmann::json& data) {
	saveComponent(component->getTypeidName(), component, data, ECS::ComponentsTypeProviderType {});
}

void SceneManager::loadFromFile(const std::string& sceneFilePath) {
	if (isPrefabLoading() || (mTransitionTask && !mTransitionTask.done())) {
		throw std::logic_error("A scene operation is already running");
	}
	auto& fs = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
	auto read = [&](const std::string& path) {
		auto file = fs.getFile(path);
		if (!file || !file->isValid()) {
			throw std::runtime_error("Scene or prefab file not found: " + path);
		}
		return nlohmann::json::parse(file->readStr());
	};
	const auto document = read(sceneFilePath);
	const auto pipeline = document.value("RenderPipeline", std::string {});
#if USE_PHYSICS_3D
	const auto destruction = DATA::readDescriptor<DESTRUCTION::RuntimeSettings>(document.value("Destruction", nlohmann::json::object()));
	destruction.validate();
#endif
	auto expanded = DOCUMENT::Expand(document, read);
	// Parse component descriptors before replacing the current scene as well.
	for (auto value : expanded.objects) {
		auto parsed = UTILS::FromJson<ECS::Object::Descriptor>(value);
		if (parsed.isErr()) {
			throw std::runtime_error(parsed.unwrapErr().text);
		}
	}
	mTransitionState = SceneTransitionState::Idle;
	unloadCurrentScene();
	m_currentScene = std::make_unique<Scene>(nullptr, mPhysicsFactories);
	m_currentScene->renderPipelineAsset = pipeline;
#if USE_PHYSICS_3D
	m_currentScene->destructionSettings = destruction;
#endif
	m_currentScene->appendDocument(expanded);
	m_currentScene->isSceneReady = true;
	m_currentSceneSourcePath = sceneFilePath;
	sceneLoadEvent.run();
}

void SceneManager::saveToFile() {
	saveToFile(m_currentSceneSourcePath);
}

nlohmann::json SceneManager::serializeScene() const {
	if (!m_currentScene) {
		throw std::runtime_error("No current scene");
	}
	auto document = DOCUMENT::Compact(m_currentScene->serializeObjects(), m_currentScene->prefabInstances);
	if (!m_currentScene->renderPipelineAsset.empty()) {
		document["RenderPipeline"] = m_currentScene->renderPipelineAsset;
	}
#if USE_PHYSICS_3D
	document["Destruction"] = serde::serialize<nlohmann::json>(m_currentScene->destructionSettings);
#endif
	return document;
}

void SceneManager::saveToFile(const std::string& sceneFilePath) {
	if (isEditingLocked()) {
		throw std::logic_error("Cannot save during a scene operation");
	}
	RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().writeTextAtomic(sceneFilePath, serializeScene().dump(2));
}

void SceneManager::createPrefab(int rootId, const std::string& path) {
	if (isEditingLocked() || !m_currentScene) {
		throw std::logic_error("Scene is not ready for prefab export");
	}
	auto& scene = *m_currentScene;
	const auto normalized = DOCUMENT::Path(path);
	auto& fs = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
	if (fs.isFileExist(normalized)) {
		throw std::runtime_error("Prefab already exists: " + normalized);
	}
	auto root = scene.findObjectByID(ECS::Entity(ECS::Entity::ID(rootId)));
	if (!root) {
		throw std::runtime_error("Prefab root not found");
	}
	std::set<int> members;
	std::vector<std::shared_ptr<ECS::Object>> stack { root };
	while (!stack.empty()) {
		auto object = stack.back();
		stack.pop_back();
		if (!members.insert(object->getIDInt()).second) {
			throw std::runtime_error("Scene hierarchy cycle");
		}
		if (scene.prefabFor(object->getIDInt())) {
			throw std::runtime_error("Unpack existing prefab before exporting this subtree");
		}
		for (auto child : object->getChildren()) {
			stack.push_back(child);
		}
	}
	auto objects = nlohmann::json::array();
	for (auto value : scene.serializeObjects()) {
		if (members.contains(DOCUMENT::Id(value.at("Id")))) {
			objects.push_back(std::move(value));
		}
	}
	DOCUMENT::IdMap ids;
	auto asset = DOCUMENT::MakePrefab(objects, rootId, ids);
	DOCUMENT::ValidatePrefab(asset);
	DOCUMENT::Instance instance;
	instance.entry = { { "Id", rootId }, { "ParentId", root->getParentId() }, { "Prefab", normalized }, { "IdMap", DOCUMENT::WriteMap(ids) },
		{ "RootOverrides", DOCUMENT::RootValues(objects.front()) } };
	instance.baseline = objects;
	// Allocate metadata before writing; failure never leaves the scene linked.
	scene.prefabInstances.reserve(scene.prefabInstances.size() + 1);
	fs.writeTextAtomic(normalized, asset.dump(2));
	scene.prefabInstances.push_back(std::move(instance));
}

void SceneManager::unloadCurrentScene() {
	if (isPrefabLoading() || (mTransitionTask && !mTransitionTask.done())) {
		throw std::logic_error("Cancel the active scene operation before synchronous unload");
	}
	if (m_currentScene) {
		m_currentScene.reset();
		sceneUnloadEvent.run();
	}
}

bool SceneManager::hasCurrentScene() const {
	return m_currentScene != nullptr;
}

Scene& SceneManager::getCurrentScene() {
	return *m_currentScene;
}

std::string SceneManager::getCurrentSceneSourcePath() const {
	return m_currentSceneSourcePath;
}

void SceneManager::setCurrentSceneSourcePath(const std::string& name) {
	m_currentSceneSourcePath = "Scenes/" + name + ".xml";
}
