#include "vmeshCookModel.h"

#if defined(IKIGAI_VG_COOKER)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "coreModule/config.h"
#include "resourceModule/modelManager.h"
#include "resourceModule/parser/assimpParser.h"
#include "resourceModule/parser/importedModel.h"

namespace IKIGAI::VG {
namespace {
	std::vector<uint8_t> ReadNativeFile(const std::filesystem::path& path) {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file) {
			throw std::runtime_error("Cannot open " + path.string());
		}
		const std::streamsize size = file.tellg();
		std::vector<uint8_t> bytes(static_cast<size_t>(size > 0 ? size : 0));
		file.seekg(0);
		if (size > 0 && !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
			throw std::runtime_error("Cannot read " + path.string());
		}
		return bytes;
	}

	void CopyVector3(const MATH::Vector3f& source, float* target) {
		target[0] = source.x;
		target[1] = source.y;
		target[2] = source.z;
	}
}

VMeshCookInput MakeCookInput(const RESOURCES::ImportedModel& model) {
	VMeshCookInput input;
	input.MaterialSlotCount = uint32_t(model.MaterialNames.size());
	input.Meshes.reserve(model.Meshes.size());
	for (const RESOURCES::ImportedMesh& mesh : model.Meshes) {
		VMeshCookMesh cookMesh;
		cookMesh.MaterialSlot = mesh.MaterialIndex;
		cookMesh.Indices = mesh.Indices;
		cookMesh.Vertices.resize(mesh.Vertices.size());
		for (size_t i = 0; i < mesh.Vertices.size(); ++i) {
			const Vertex& source = mesh.Vertices[i];
			VMeshCookVertex& target = cookMesh.Vertices[i];
			CopyVector3(source.position, target.Position);
			CopyVector3(source.normal, target.Normal);
			CopyVector3(source.tangent, target.Tangent);
			CopyVector3(source.bitangent, target.Bitangent);
			target.Uv[0] = source.texCoord.x;
			target.Uv[1] = source.texCoord.y;
		}
		input.MaterialSlotCount = std::max(input.MaterialSlotCount, cookMesh.MaterialSlot + 1u);
		input.Meshes.push_back(std::move(cookMesh));
	}
	return input;
}

void CookImportedModel(
	const RESOURCES::ImportedModel& model, const VMeshCookSettings& settings, VMeshData& output, VMeshCookReport* report, uint64_t sourceHash) {
	VMeshCookInput input = MakeCookInput(model);
	input.SourceHash = sourceHash;
	CookVMesh(input, settings, output, report);
}

std::string ResolveCookModelPath(const std::string& modelPath) {
	namespace fs = std::filesystem;
	std::vector<fs::path> candidates;
	candidates.push_back(fs::u8path(modelPath));
	std::string relative = modelPath;
	while (!relative.empty() && (relative.front() == '/' || relative.front() == '\\')) {
		relative.erase(relative.begin());
	}
	candidates.push_back(fs::u8path(Config::ROOT + Config::USER_ASSETS_PATH + relative));
	candidates.push_back(fs::u8path(Config::ROOT + Config::ENGINE_ASSETS_PATH + relative));
	for (const fs::path& candidate : candidates) {
		std::error_code error;
		if (fs::is_regular_file(candidate, error)) {
			return candidate.lexically_normal().string();
		}
	}
	return {};
}

int RunVMeshCookCommand(const VMeshCookCommand& command) {
	try {
		using Clock = std::chrono::steady_clock;
		const auto start = Clock::now();
		if (command.OutputPath.empty()) {
			throw std::invalid_argument("--cook-output=<file.vmesh> is required");
		}
		const std::string modelPath = ResolveCookModelPath(command.ModelPath);
		if (modelPath.empty()) {
			throw std::invalid_argument("Model not found: " + command.ModelPath);
		}
		VMeshCookSettings settings;
		if (!command.SettingsPath.empty()) {
			const std::vector<uint8_t> json = ReadNativeFile(std::filesystem::u8path(command.SettingsPath));
			settings = ParseCookSettings(std::string(json.begin(), json.end()));
		}
		const std::vector<uint8_t> sourceBytes = ReadNativeFile(std::filesystem::u8path(modelPath));
		const uint64_t sourceHash = HashVMeshBytes(sourceBytes.data(), sourceBytes.size());

		RESOURCES::ImportedModel imported;
		if (!RESOURCES::AssimpParser::ImportModel(modelPath, RESOURCES::ModelLoader::getDefaultFlag(), imported)) {
			throw std::runtime_error("Assimp import failed: " + modelPath);
		}
		const double importSeconds = std::chrono::duration<double>(Clock::now() - start).count();

		VMeshData data;
		VMeshCookReport report;
		CookImportedModel(imported, settings, data, &report, sourceHash);
		WriteVMeshFile(command.OutputPath, data);
		const VMeshData verify = ReadVMeshFile(command.OutputPath);
		const auto fileSize = std::filesystem::file_size(std::filesystem::u8path(command.OutputPath));
		const VMeshStatistics statistics = ComputeVMeshStatistics(verify);

		std::ostringstream levels;
		for (size_t level = 0; level < statistics.ClustersPerLevel.size(); ++level) {
			levels << " L" << level << "=" << statistics.ClustersPerLevel[level] << "/" << statistics.TrianglesPerLevel[level];
		}
		std::cout << "[vmesh-cook] model " << modelPath << " -> " << command.OutputPath << "\n"
				  << "[vmesh-cook] import " << importSeconds << " s, cook " << report.TotalSeconds << " s (prepare " << report.PrepareSeconds
				  << ", clusters " << report.ClusterSeconds << ", hierarchy " << report.HierarchySeconds << ", fallback " << report.FallbackSeconds
				  << ")\n"
				  << "[vmesh-cook] input vertices " << report.InputVertexCount << ", input triangles " << report.InputTriangleCount
				  << ", welded vertices " << report.WeldedVertexCount << ", degenerate triangles removed " << report.DegenerateTriangleCount
				  << ", cross-material locked vertices " << report.CrossMaterialLockedVertexCount << ", stuck groups " << report.StuckGroupCount
				  << ", deferred groups " << report.DeferredGroupCount << ", sloppy groups " << report.SloppyGroupCount << ", retried groups "
				  << report.RetriedGroupCount << ", topology-rejected attempts " << report.TopologyRejectedGroupCount
				  << ", non-manifold locked vertices " << report.NonManifoldLockedVertexCount << "\n"
				  << "[vmesh-cook] file " << fileSize << " bytes, material slots " << verify.Header.MaterialSlotCount << ", vertices "
				  << statistics.VertexCount << ", indices " << statistics.IndexCount << ", clusters " << statistics.ClusterCount << " (leaf "
				  << statistics.LeafClusterCount << ", root " << statistics.RootClusterCount << "), groups " << statistics.GroupCount << ", levels "
				  << statistics.LodLevelCount << ", leaf triangles " << statistics.LeafTriangleCount << ", fallback triangles "
				  << statistics.FallbackTriangleCount << "\n"
				  << "[vmesh-cook] clusters/triangles per level:" << levels.str() << "\n"
				  << "VMESH_COOK_OK" << std::endl;
		return 0;
	} catch (const std::exception& error) {
		std::cerr << "[vmesh-cook] failed: " << error.what() << std::endl;
		std::cout << "VMESH_COOK_FAILED" << std::endl;
		return 1;
	}
}
}

#endif
