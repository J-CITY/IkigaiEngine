#pragma once

#include <cstdint>
#include <string>

#include "virtualGeometryModule/cook/vmeshCooker.h"

namespace IKIGAI::RESOURCES {
struct ImportedModel;
}

// Engine-side entry points of the cooker: ImportedModel adapter and the command line tool. Implemented only when
// IKIGAI_VG_COOKER is defined.
namespace IKIGAI::VG {
// Converts the CPU import result (model space, node transforms not applied, material slot = importer material index).
VMeshCookInput MakeCookInput(const RESOURCES::ImportedModel& model);

void CookImportedModel(const RESOURCES::ImportedModel& model, const VMeshCookSettings& settings, VMeshData& output, VMeshCookReport* report = nullptr,
	uint64_t sourceHash = 0);

struct VMeshCookCommand {
	std::string ModelPath {}; // native path, or an asset path resolved against assets/game and assets/engine
	std::string OutputPath {}; // native path of the .vmesh to write
	std::string SettingsPath {}; // optional JSON with VMeshCookSettings fields
};

// Resolves the model path to a native file, or returns an empty string.
std::string ResolveCookModelPath(const std::string& modelPath);

// Imports the model with the default model loader flags, cooks, writes, reads the file back with validation and
// prints statistics. Prints VMESH_COOK_OK on success. Returns a process exit code (0 on success).
int RunVMeshCookCommand(const VMeshCookCommand& command);
}
