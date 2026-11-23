#include "support/expect.h"

#include "sceneModule/sceneDocument.h"

#include <stdexcept>
#include <string>

namespace {
using namespace IKIGAI::SCENE_SYSTEM::DOCUMENT;
using IKIGAI::TEST::expect;
using IKIGAI::TEST::expectThrows;
using IKIGAI::TEST::require;

Json object(int id, int parent) {
	const Json transform { { "TransformComponentType", "TransformComponent" } };
	return { { "Id", id }, { "ParentId", parent }, { "Components", Json::array({ transform }) } };
}

void checkHierarchyValidation() {
	const auto load = [](const Json& objects) {
		return Expand({ { "Objects", objects } },
			[](const std::string&) -> Json { throw std::logic_error("A scene without prefab references must not load an asset"); });
	};
	const auto valid = Json::array({ object(1, -1), object(2, 1) });
	expect(load(valid).objects == valid, "scene.document.preservesValidHierarchy");
	expectThrows<std::runtime_error>([&] { (void)load(Json::array({ object(1, -1), object(1, -1) })); }, "scene.document.rejectsDuplicateId");
	expectThrows<std::runtime_error>([&] { (void)load(Json::array({ object(1, 2), object(2, 1) })); }, "scene.document.rejectsParentCycle");
	expectThrows<std::runtime_error>([&] { (void)load(Json::array({ object(1, 2) })); }, "scene.document.rejectsMissingParent");
}

void checkPrefabExpansion() {
	auto prefabRoot = object(1, -1);
	prefabRoot["Components"].push_back({ { "RenderTargetComponentType", "RenderTargetComponent" }, { "CameraObjectId", 2 }, { "Width", 2 } });
	const Json asset { { "PrefabVersion", 2 }, { "RootId", 1 }, { "Objects", Json::array({ prefabRoot, object(2, 1) }) } };
	const Json instance { { "Prefab", "prefabs\\room.prefab.json" }, { "Id", 100 }, { "ParentId", 40 }, { "IdMap", { { "2", 103 } } } };
	const Json scene { { "Objects", Json::array({ object(40, -1), instance }) } };
	std::string requestedPath;
	const auto expanded = Expand(scene, [&](const std::string& path) {
		requestedPath = path;
		return asset;
	});
	expect(requestedPath == "/prefabs/room.prefab.json", "scene.prefab.normalizesResourcePath");
	require(expanded.objects.size() == 3 && expanded.instances.size() == 1, "scene.prefab.expandsTwoMembers");
	const auto& root = expanded.objects.at(1);
	const auto& child = expanded.objects.at(2);
	expect(root.at("Id") == 100 && root.at("ParentId") == 40, "scene.prefab.rootUsesInstanceIdAndParent");
	expect(child.at("Id") == 103 && child.at("ParentId") == 100, "scene.prefab.childUsesPersistedIdAndRemappedParent");
	const auto& renderTarget = root.at("Components").at(1);
	expect(renderTarget.at("CameraObjectId") == 103, "scene.prefab.remapsDeclaredEntityReference");
	expect(renderTarget.at("Width") == 2, "scene.prefab.preservesUnrelatedInteger");
}
}

int main(int argc, char** argv) {
	return IKIGAI::TEST::runCases(argc, argv, { { "hierarchy", checkHierarchyValidation }, { "prefab_expansion", checkPrefabExpansion } });
}
