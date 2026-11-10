#include "skeletonStateGraphManager.h"
#include "fileWatcher.h"
#include "serviceManager.h"
#include "skeletonAnimationManager.h"
#include "skeletonManager.h"
#include "utilsModule/memory/resourceAllocator.h"
#include <iostream>

namespace IKIGAI::RESOURCES {
namespace {
	using Json = nlohmann::json;
	Json readDocument(const std::string& path) {
		auto file = ServiceManager::Get<FileSystem>().getFile(path);
		if (!file || !file->isValid()) {
			throw std::runtime_error("Missing animation resource: " + path);
		}
		auto bytes = file->read();
		return Json::parse(bytes.begin(), bytes.end());
	}
	// External blendspaces are lowered to composable nodes before validation.
	// Clip paths already declared in the graph retain their preparation/phase settings.
	void expandBlendspaces(Json& doc) {
		std::function<void(Json&, int)> expand;
		expand = [&](Json& node, int depth) {
			if (depth > 32) {
				throw std::runtime_error("State graph node nesting exceeds 32");
			}
			if (!node.is_object()) {
				return;
			}
			if (node.contains("Resource")) {
				const auto path = node.at("Resource").get<std::string>();
				if (!path.ends_with(".blendspace1d") && !path.ends_with(".blendspace2d")) {
					throw std::runtime_error("Unsupported graph node resource: " + path);
				}
				auto space = readDocument(path);
				if (space.at("SkeletonPath") != doc.at("SkeletonPath")) {
					throw std::runtime_error("Blendspace uses a different skeleton: " + path);
				}
				doc["Dependencies"].push_back(path);
				auto clipName = [&](const std::string& clipPath) {
					for (const auto& [name, c] : doc.at("Clips").items()) {
						if (c.at("Path") == clipPath) {
							return name;
						}
					}
					doc["Clips"][clipPath] = { { "Path", clipPath }, { "Loop", true } };
					return clipPath;
				};
				auto rowNode = [&](const Json& row) {
					Json points = Json::array();
					for (const auto& pair : row.at("Nodes")) {
						points.push_back({ { "Value", pair.at(0) }, { "Node", clipName(pair.at(1)) } });
					}
					std::sort(points.begin(), points.end(),
						[](const Json& a, const Json& b) { return a.at("Value").get<float>() < b.at("Value").get<float>(); });
					return Json { { "Blendspace1D", points }, { "Parameter", node.at("X") } };
				};
				if (path.ends_with(".blendspace1d")) {
					node = rowNode(space.at("Nodes").at(0));
				}
				else {
					Json rows = Json::array();
					for (const auto& row : space.at("Nodes")) {
						rows.push_back({ { "Value", row.at("Value") }, { "Node", rowNode(row) } });
					}
					std::sort(rows.begin(), rows.end(),
						[](const Json& a, const Json& b) { return a.at("Value").get<float>() < b.at("Value").get<float>(); });
					node = { { "Blendspace1D", rows }, { "Parameter", node.at("Y") } };
				}
				return;
			}
			if (node.contains("Blend")) {
				for (auto& child : node.at("Blend")) {
					expand(child, depth + 1);
				}
			}
			if (node.contains("Select")) {
				expand(node.at("True"), depth + 1);
				expand(node.at("False"), depth + 1);
			}
			if (node.contains("Blendspace1D")) {
				for (auto& p : node.at("Blendspace1D")) {
					expand(p.at("Node"), depth + 1);
				}
			}
		};
		for (auto& state : doc.at("States")) {
			expand(state.at("Node"), 0);
		}
	}
}

ResourcePtr<SKELETON::SkeletalStateGraph> SkeletonStateGraphLoader::CreateFromResource(
	const std::string& path, UTILS::IAllocator* allocator, ResourceDeleter deleter) {
	auto doc = readDocument(path);
	if (doc.value("Version", 0) != 2) {
		throw std::runtime_error("Unsupported or missing animation graph Version (expected 2): " + path);
	}
	expandBlendspaces(doc);
	auto graph = MEMORY::MakeSharedWithDeleter<SKELETON::SkeletalStateGraph, MEMORY::Category::Skeleton>(allocator, deleter);
	auto skeleton = ServiceManager::Get<SkeletonLoader>().loadResource(doc.at("SkeletonPath").get<std::string>());
	graph->definition = SKELETON::StateGraphDefinition::compile(doc, skeleton, [skeleton](const std::string& clip) {
		return clip.ends_with(".animation") ? SkeletonAnimationLoader::CreateFromResource(clip, skeleton)
											: SkeletonAnimationLoader::CreateFromFile(clip, skeleton);
	});
	return graph;
}

bool SkeletonStateGraphLoader::reloadResource(std::weak_ptr<SKELETON::SkeletalStateGraph> weakRes, const std::string& path) {
	if (auto res = weakRes.lock()) {
		try {
			auto candidate = CreateFromResource(path);
			res->definition = candidate->definition;
			++res->revision;
			addFileWatchSubscribe(path, {}, res);
			return true;
		} catch (const std::exception& e) {
			std::cerr << "State graph reload rejected (" << path << "): " << e.what() << std::endl;
			return false;
		}
	}
	return false;
}

ResourcePtr<SKELETON::SkeletalStateGraph> SkeletonStateGraphLoader::createResource(const std::string& path) {
	return createResource(path, ELoadingType::RESOURCE);
}
void SkeletonStateGraphLoader::addFileWatchSubscribe(
	const std::string& path, const std::unordered_set<std::string>&, std::weak_ptr<SKELETON::SkeletalStateGraph> weak) {
	std::unordered_set<std::string> dependencies { path };
	if (auto graph = weak.lock(); graph && graph->definition) {
		dependencies.insert(graph->definition->document.at("SkeletonPath").get<std::string>());
		for (const auto& clip : graph->definition->dependencies) {
			dependencies.insert(clip);
			if (clip.ends_with(".animation")) {
				try {
					auto bytes = ServiceManager::Get<FileSystem>().getFile(clip)->read();
					auto desc = nlohmann::json::parse(bytes.begin(), bytes.end());
					dependencies.insert(desc.at("PathAnimation").get<std::string>());
				} catch (const std::exception&) { /* Invalid reload keeps the last valid dependency set. */
				}
			}
		}
	}
	ResourceManager::addFileWatchSubscribe(path, dependencies, weak);
}

ResourcePtr<SKELETON::SkeletalStateGraph> SkeletonStateGraphLoader::createResource(const std::string& path, ELoadingType type) {
	return createResource(path, type, std::any());
}

ResourcePtr<SKELETON::SkeletalStateGraph> SkeletonStateGraphLoader::createResource(const std::string& path, ELoadingType type, std::any data) {
	if (type == ELoadingType::RESOURCE) {
		auto graph = CreateFromResource(path);
		addFileWatchSubscribe(path, { path }, graph);
		return graph;
	}
	return nullptr;
}
}
