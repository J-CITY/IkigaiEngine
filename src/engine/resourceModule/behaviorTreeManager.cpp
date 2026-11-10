#include "behaviorTreeManager.h"
#include "blackboardManager.h"
#include <iostream>

namespace IKIGAI::RESOURCES {
std::shared_ptr<const AI::BehaviorTreeDefinition> BehaviorTreeLoader::compileFile(const std::string& path) {
	auto file = ServiceManager::Get<FileSystem>().getFile(path);
	if (!file || !file->isValid()) {
		throw std::runtime_error("Missing Behavior Tree: " + path);
	}
	auto document = nlohmann::json::parse(file->readStr());
	const auto descriptor = AI::readDescriptor<AI::BehaviorTreeDescriptor>(document);
	// Compile schema and dependent tree from one candidate, without publishing a
	// new schema into a running instance before its tree has passed validation.
	auto schema = BlackboardLoader::compileFile(descriptor.Blackboard);
	return AI::BehaviorTreeDefinition::compile(document, std::move(schema), AI::BehaviorRegistry::defaults(), path);
}
ResourcePtr<BehaviorTreeResource> BehaviorTreeLoader::createResource(const std::string& path) {
	auto resource = std::make_shared<BehaviorTreeResource>();
	resource->definition = compileFile(path);
	addFileWatchSubscribe(path, {}, resource);
	return resource;
}
void BehaviorTreeLoader::addFileWatchSubscribe(
	const std::string& path, const std::unordered_set<std::string>&, std::weak_ptr<BehaviorTreeResource> weak) {
	std::unordered_set<std::string> dependencies { path };
	if (auto resource = weak.lock()) {
		dependencies.insert(resource->definition->blackboardPath);
	}
	ResourceManager::addFileWatchSubscribe(path, dependencies, weak);
}
bool BehaviorTreeLoader::reloadResource(std::weak_ptr<BehaviorTreeResource> weak, const std::string& path) {
	if (auto resource = weak.lock()) {
		try {
			auto candidate = compileFile(path);
			resource->definition = std::move(candidate);
			++resource->revision;
			addFileWatchSubscribe(path, {}, resource);
			return true;
		} catch (const std::exception& error) {
			std::cerr << "Behavior Tree reload rejected: " << error.what() << '\n';
		}
	}
	return false;
}
bool BehaviorTreeLoader::reload(const std::string& path) {
	auto resource = loadResource(path);
	const auto normalized = NormalizePath(path);
	return reloadResource(resource, normalized);
}
}
