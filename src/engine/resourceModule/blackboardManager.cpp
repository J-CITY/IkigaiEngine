#include "blackboardManager.h"
#include <iostream>

namespace IKIGAI::RESOURCES {
std::shared_ptr<const AI::BlackboardSchema> BlackboardLoader::compileFile(const std::string& path) {
	auto file = ServiceManager::Get<FileSystem>().getFile(path);
	if (!file || !file->isValid()) {
		throw std::runtime_error("Missing Blackboard: " + path);
	}
	return AI::BlackboardSchema::compile(nlohmann::json::parse(file->readStr()), path);
}
ResourcePtr<BlackboardResource> BlackboardLoader::createResource(const std::string& path) {
	auto resource = std::make_shared<BlackboardResource>();
	resource->schema = compileFile(path);
	addFileWatchSubscribe(path, { path }, resource);
	return resource;
}
bool BlackboardLoader::reloadResource(std::weak_ptr<BlackboardResource> weak, const std::string& path) {
	if (auto resource = weak.lock()) {
		try {
			auto candidate = compileFile(path);
			resource->schema = std::move(candidate);
			++resource->revision;
			return true;
		} catch (const std::exception& error) {
			std::cerr << "Blackboard reload rejected: " << error.what() << '\n';
		}
	}
	return false;
}
bool BlackboardLoader::reload(const std::string& path) {
	return reloadResource(loadResource(path), NormalizePath(path));
}
}
