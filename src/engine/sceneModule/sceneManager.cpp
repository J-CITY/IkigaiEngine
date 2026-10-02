#include "sceneManager.h"

#include <coreModule/ecs/components/ambientSphereLight.h>
#include <coreModule/ecs/components/cameraComponent.h>
#include <coreModule/ecs/components/directionalLight.h>
#include <coreModule/ecs/object.h>

#include "utilsModule/log/loggerDefine.h"
#include <iostream>

using namespace IKIGAI;
using namespace IKIGAI::SCENE_SYSTEM;

SceneManager::SceneManager(const std::string& p_sceneRootFolder) : m_sceneRootFolder(p_sceneRootFolder) {
	loadEmptyScene();
}

SceneManager::~SceneManager() {
	unloadCurrentScene();
}

void SceneManager::update() {
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

	m_currentScene = std::make_unique<Scene>();

	sceneLoadEvent.run();

	m_currentScene->postLoad();
}

void SceneManager::loadDefaultScene() {
	unloadCurrentScene();

	m_currentScene = std::make_unique<Scene>();

	sceneLoadEvent.run();

	auto directionalLight = m_currentScene->createObject("Directional Light");
	directionalLight->addComponent<ECS::DirectionalLight>()->setIntensity(0.75f);
	directionalLight->getTransform()->setLocalPosition({0.0f, 10.0f, 0.0f});
	directionalLight->getTransform()->setLocalRotation(MATH::QuaternionF(MATH::Vector3{120.0f, -40.0f, 0.0f}));

	auto ambientLight = m_currentScene->createObject("Ambient Light");
	ambientLight->addComponent<ECS::AmbientSphereLight>()->setRadius(10000.0f);

	auto camera = m_currentScene->createObject("Main Camera");
	camera->addComponent<ECS::CameraComponent>();
	camera->getTransform()->setLocalRotation(MATH::QuaternionF(MATH::Vector3f{ 20.0f, 180.0f, 0.0f }));
	camera->getTransform()->setLocalPosition({0.0f, 3.0f, 8.0f});

	m_currentScene->postLoad();
}

#include <fstream>
#include <nlohmann/json.hpp>
#include <utilsModule/pathGetter.h>

template<typename T>
void addComponentImpl(const std::string& typeNameStr, std::shared_ptr<ECS::Object> obj, nlohmann::json& data) {
	if (typeNameStr == ECS::GetType<T>()) {
		auto c = obj->addComponent<T>();
		//auto result = IKIGAI::UTILS::FromJson<T>(*c, data);
		//if (result.isErr()) {
		//	LOG_ERROR << (result.unwrapErr().text);
		//}
	}
}

template<template<typename...> class Container, typename...ComponentType>
void addComponent(const std::string& typeNameStr, std::shared_ptr<ECS::Object> obj, nlohmann::json& data, Container<ComponentType...> opt) {
	(addComponentImpl<ComponentType>(typeNameStr, obj, data), ...);
}

void addComponent(const std::string& typeNameStr, std::shared_ptr<ECS::Object> obj, nlohmann::json& data) {
	addComponent(typeNameStr, obj, data, ECS::ComponentsTypeProviderType{});
}

//----------------------------

template<typename T>
void saveComponentImpl(const std::string& typeNameStr, UTILS::WeakPtr<ECS::ComponentBase> component, nlohmann::json& data) {
	if (typeNameStr == ECS::GetType<T>()) {
		auto _p = std::static_pointer_cast<T>(component);
		//data = IKIGAI::UTILS::ToJson(*_p);
		//data["Type"] = typeid(T).name();
	}
}

template<template<typename...> class Container, typename...ComponentType>
void saveComponent(const std::string& typeNameStr, UTILS::WeakPtr<ECS::ComponentBase> component, nlohmann::json& data, Container<ComponentType...> opt) {
	(saveComponentImpl<ComponentType>(typeNameStr, component, data), ...);
}

void saveComponent(UTILS::WeakPtr<ECS::ComponentBase> component, nlohmann::json& data) {
	saveComponent(component->getTypeidName(), component, data, ECS::ComponentsTypeProviderType{});
}

void SceneManager::loadFromFile(const std::string& sceneFilePath) {
#ifdef __EMSCRIPTEN__
	std::cout << "[web] SceneManager::loadFromFile " << sceneFilePath << std::endl;
	std::cout.flush();
#endif
	unloadCurrentScene();

	m_currentScene = std::make_unique<Scene>();
	m_currentSceneSourcePath = sceneFilePath;

	//TODO: load with vfs
	std::ifstream f(UTILS::GetRealPath(sceneFilePath));
	nlohmann::json data = nlohmann::json::parse(f);
	f.close();
	if (data.contains("Objects")) {
		for (auto& object : data["Objects"]) {
			auto newActorRes = IKIGAI::UTILS::FromJson<ECS::Object::Descriptor>(object);
			if (newActorRes.isErr()) {
				auto err = newActorRes.unwrapErr();
				LOG_ERROR << (err.text);
				std::cout << err.text;
				continue;
			}
			
			auto newActor = newActorRes.unwrap();
			auto obj = m_currentScene->createObject(newActor);
			//if (actor.contains("Components")) {
			//	for (auto& component: actor["Components"]) {
			//		addComponent(component["Type"], obj, component);
			//	}
			//}
		}
	}
	else
	{
		std::cout << "@!!!!!!" << std::endl;
	}

	m_currentScene->postLoad();
#ifdef __EMSCRIPTEN__
	std::cout << "[web] SceneManager::loadFromFile done" << std::endl;
	std::cout.flush();
#endif
}

void SceneManager::saveToFile() {
	saveToFile(m_currentSceneSourcePath);
}

void SceneManager::saveToFile(const std::string& sceneFilePath) {
	Scene::Descriptor sceneDescriptor;
	for (auto& obj : m_currentScene->getObjects()) {
		sceneDescriptor.Objects.push_back(obj->getDescriptor());
	}

	auto jsonStrRes = UTILS::ToJsonStr(sceneDescriptor);
	if (jsonStrRes.isErr()) {
		LOG_ERROR << "SceneManager::saveToFile Can not save: " << sceneFilePath;
		return;
	}
	auto jsonStr = jsonStrRes.unwrap();
	//write
	//TODO: load with vfs
	std::ofstream f(UTILS::GetRealPath(sceneFilePath));
	f << jsonStr << std::endl;
	f.close();
}

void SceneManager::unloadCurrentScene() {
	if (m_currentScene) {
		m_currentScene.release();
		m_currentScene = nullptr;
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

