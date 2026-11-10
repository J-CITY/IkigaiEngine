#pragma once
#include "surfaceFrame.h"
#include "surfaceGenerator.h"
#include "surfaceJob.h"
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>

namespace IKIGAI::RENDER {
class ModelInterface;
}
namespace IKIGAI::SURFACE {
std::shared_ptr<RENDER::ModelInterface> upload(const Geometry& geometry);
struct CachedModel {
	std::shared_ptr<RENDER::ModelInterface> Model = {};
	size_t Bytes = 0;
	uint64_t LastUse = 0;
};
class Terrain {
public:
	using Key = std::tuple<unsigned, unsigned, unsigned>;
	explicit Terrain(const Settings& settings);
	~Terrain();
	Terrain(const Terrain&) = delete;
	Terrain& operator=(const Terrain&) = delete;
	const Settings& settings() const;
	const std::string& buildError() const;
	std::shared_ptr<RENDER::ModelInterface> model(unsigned x, unsigned z, unsigned lod, unsigned& budget);
	std::shared_ptr<RENDER::ModelInterface> water(unsigned x, unsigned z, unsigned& budget);
	void clearCache();
	std::optional<unsigned> residentLod(unsigned x, unsigned z, unsigned requested) const;
	std::vector<Key> residentRequests() const;
	void install(const Key& key, const Geometry& geometry);
	void beginFrame();
	bool canUpload();

private:
	bool recordFailedBuild(const Key& key, BackgroundJob& job);
	Settings mSettings = {};
	std::map<std::tuple<unsigned, unsigned, unsigned>, CachedModel> mModels = {};
	uint64_t mClock = 0;
	std::map<Key, std::shared_ptr<BackgroundJob>> mPending = {};
	std::set<Key> mFailedBuilds = {};
	std::string mBuildError = {};
	std::chrono::steady_clock::time_point mUploadEnd = {};
};

struct PreparedTile {
	Terrain::Key Key = {};
	Geometry Mesh = {};
};
struct PreparedSurface {
	std::vector<uint8_t> CoveragePixels = {};
	std::vector<PreparedTile> Tiles = {};
};
PreparedSurface prepareSurface(const Settings& settings, const std::vector<Terrain::Key>& requests, const std::atomic_bool& cancellation);

class VoxelWorld {
public:
	explicit VoxelWorld(const VoxelSettings& settings);
	const VoxelSettings& settings() const;
	Block get(int x, int y, int z) const;
	void set(int x, int y, int z, Block block);
	std::shared_ptr<RENDER::ModelInterface> model(unsigned x, unsigned y, unsigned z, unsigned& budget);
	void clearCache();
	void reset();
	std::vector<VoxelEdit> edits() const;

private:
	VoxelSettings mSettings = {};
	std::unordered_map<uint64_t, Block> mEdits = {};
	std::map<std::tuple<unsigned, unsigned, unsigned>, CachedModel> mModels = {};
	uint64_t mClock = 0;
	uint64_t key(int x, int y, int z) const;
	bool contains(int x, int y, int z) const;
};
}
