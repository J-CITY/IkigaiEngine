#include "surfaceRuntime.h"
#include "renderModule/backends/interface/modelInterface.h"
#include "renderModule/backends/meshFactory.h"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace IKIGAI::SURFACE {
namespace {
	template <class Map, class Key>
	std::shared_ptr<RENDER::ModelInterface> cacheModel(Map& models, const Key& key, const Geometry& geometry, unsigned megabytes, uint64_t stamp) {
		size_t bytes = 0;
		for (const auto& part : geometry.Parts) {
			bytes += part.Vertices.size() * sizeof(Vertex) + part.Indices.size() * sizeof(unsigned);
		}
		const size_t limit = size_t(megabytes) * 1024 * 1024;
		if (bytes > limit) {
			throw std::runtime_error("Surface mesh exceeds CacheMegabytes; lower resolution/chunk size or increase the cache");
		}
		size_t used = 0;
		for (const auto& [cachedKey, cached] : models) {
			used += cached.Bytes;
		}
		while (!models.empty() && used + bytes > limit) {
			const auto oldest
				= std::min_element(models.begin(), models.end(), [](const auto& a, const auto& b) { return a.second.LastUse < b.second.LastUse; });
			used -= oldest->second.Bytes;
			models.erase(oldest);
		}
		auto result = upload(geometry);
		models.emplace(key, CachedModel { result, bytes, stamp });
		return result;
	}
}
void FrameState::beginFrame(unsigned budget) {
	Budget = budget;
	PreviousWorld = LastWorld;
}
const MATH::Matrix4f& FrameState::previous(const MATH::Matrix4f& world) {
	if (!Initialized) {
		PreviousWorld = world;
		Initialized = true;
	}
	LastWorld = world;
	return PreviousWorld;
}
std::shared_ptr<RENDER::ModelInterface> upload(const Geometry& geometry) {
	auto result = RENDER::CreateEmptyModel();
	for (const auto& part : geometry.Parts) {
		if (!part.Indices.empty()) {
			result->getMeshes().push_back(RENDER::CreateMesh(part.Vertices, part.Indices, part.MaterialId));
		}
	}
	result->computeBoundingSphere();
	return result;
}
Terrain::Terrain(const Settings& settings)
	: mSettings(settings) {
	settings.validate();
	beginFrame();
}
Terrain::~Terrain() {
	mPending.clear();
}
void Terrain::beginFrame() {
	mUploadEnd = std::chrono::steady_clock::time_point::max();
}
bool Terrain::canUpload() {
	if (mUploadEnd == std::chrono::steady_clock::time_point::max()) {
		mUploadEnd = std::chrono::steady_clock::now() + std::chrono::microseconds(int64_t(mSettings.UploadMilliseconds * 1000));
	}
	return std::chrono::steady_clock::now() < mUploadEnd;
}
std::vector<Terrain::Key> Terrain::residentRequests() const {
	std::vector<Key> keys;
	for (const auto& [key, model] : mModels) {
		keys.push_back(key);
	}
	return keys;
}
void Terrain::install(const Key& key, const Geometry& geometry) {
	cacheModel(mModels, key, geometry, mSettings.CacheMegabytes, ++mClock);
}
PreparedSurface prepareSurface(const Settings& settings, const std::vector<Terrain::Key>& requests, const std::atomic_bool& cancellation) {
	PreparedSurface result;
	if (settings.Blend.Enabled) {
		result.CoveragePixels = buildCoverageMap(settings, &cancellation);
	}
	std::set<Terrain::Key> keys;
	for (const auto& [x, z, lod] : requests) {
		if (x >= settings.TilesX || z >= settings.TilesZ) {
			continue;
		}
		const bool water = lod == std::numeric_limits<unsigned>::max();
		if (water && !settings.Water.Enabled) {
			continue;
		}
		keys.insert({ x, z, water ? lod : std::min(lod, unsigned(settings.LodDistances.size())) });
	}
	size_t bytes = 0;
	for (const auto& key : keys) {
		checkBuildCancellation(&cancellation);
		const auto [x, z, lod] = key;
		auto geometry = lod == std::numeric_limits<unsigned>::max() ? buildWater(settings, x, z) : buildTile(settings, x, z, lod, &cancellation);
		for (const auto& part : geometry.Parts) {
			bytes += part.Vertices.size() * sizeof(Vertex) + part.Indices.size() * sizeof(unsigned);
		}
		if (bytes > size_t(settings.CacheMegabytes) * 1024 * 1024) {
			throw std::runtime_error("Updated resident terrain exceeds CacheMegabytes");
		}
		result.Tiles.push_back({ key, std::move(geometry) });
	}
	return result;
}
const Settings& Terrain::settings() const {
	return mSettings;
}
std::shared_ptr<RENDER::ModelInterface> Terrain::model(unsigned x, unsigned z, unsigned lod, unsigned& budget) {
	const auto key = std::tuple { x, z, lod };
	if (const auto found = mModels.find(key); found != mModels.end()) {
		found->second.LastUse = ++mClock;
		return found->second.Model;
	}
	if (mSettings.BackgroundUpdates) {
		auto pending = mPending.find(key);
		if (pending == mPending.end() && mPending.size() < mSettings.PendingBuilds) {
			mPending.emplace(key, std::make_shared<BackgroundJob>([settings = mSettings, x, z, lod](const auto& cancellation) -> std::any {
				return buildTile(settings, x, z, lod, &cancellation);
			}));
			pending = mPending.find(key);
		}
		if (pending != mPending.end() && pending->second->ready() && budget && canUpload()) {
			auto geometry = std::any_cast<Geometry>(pending->second->take());
			mPending.erase(pending);
			--budget;
			return cacheModel(mModels, key, geometry, mSettings.CacheMegabytes, ++mClock);
		}
		for (unsigned alternative = 0; alternative <= mSettings.LodDistances.size(); ++alternative) {
			if (const auto found = mModels.find({ x, z, alternative }); found != mModels.end()) {
				found->second.LastUse = ++mClock;
				return found->second.Model;
			}
		}
		return {};
	}
	if (budget == 0) {
		// Keep the last available geometry while the requested LOD waits for upload.
		for (unsigned alternative = 0; alternative <= mSettings.LodDistances.size(); ++alternative) {
			if (const auto found = mModels.find({ x, z, alternative }); found != mModels.end()) {
				found->second.LastUse = ++mClock;
				return found->second.Model;
			}
		}
		return {};
	}
	--budget;
	return cacheModel(mModels, key, buildTile(mSettings, x, z, lod), mSettings.CacheMegabytes, ++mClock);
}
std::shared_ptr<RENDER::ModelInterface> Terrain::water(unsigned x, unsigned z, unsigned& budget) {
	if (!mSettings.Water.Enabled) {
		return {};
	}
	const auto key = std::tuple { x, z, std::numeric_limits<unsigned>::max() };
	if (const auto found = mModels.find(key); found != mModels.end()) {
		found->second.LastUse = ++mClock;
		return found->second.Model;
	}
	if (!budget || (mSettings.BackgroundUpdates && !canUpload())) {
		return {};
	}
	--budget;
	return cacheModel(mModels, key, buildWater(mSettings, x, z), mSettings.CacheMegabytes, ++mClock);
}
void Terrain::clearCache() {
	mModels.clear();
	mPending.clear();
}
std::optional<unsigned> Terrain::residentLod(unsigned x, unsigned z, unsigned requested) const {
	if (mModels.contains({ x, z, requested })) {
		return requested;
	}
	for (unsigned alternative = 0; alternative <= mSettings.LodDistances.size(); ++alternative) {
		if (mModels.contains({ x, z, alternative })) {
			return alternative;
		}
	}
	return {};
}
VoxelWorld::VoxelWorld(const VoxelSettings& settings)
	: mSettings(settings) {
	settings.validate();
}
const VoxelSettings& VoxelWorld::settings() const {
	return mSettings;
}
bool VoxelWorld::contains(int x, int y, int z) const {
	return x >= 0 && y >= 0 && z >= 0 && x < int(mSettings.ChunksX * mSettings.ChunkSize) && y < int(mSettings.ChunksY * mSettings.ChunkSize)
		&& z < int(mSettings.ChunksZ * mSettings.ChunkSize);
}
uint64_t VoxelWorld::key(int x, int y, int z) const {
	return (uint64_t(x) * mSettings.ChunksY * mSettings.ChunkSize + uint64_t(y)) * mSettings.ChunksZ * mSettings.ChunkSize + uint64_t(z);
}
Block VoxelWorld::get(int x, int y, int z) const {
	if (!contains(x, y, z)) {
		return Block::Air;
	}
	const auto found = mEdits.find(key(x, y, z));
	return found == mEdits.end() ? generatedBlock(mSettings, x, y, z) : found->second;
}
void VoxelWorld::set(int x, int y, int z, Block block) {
	if (!contains(x, y, z) || unsigned(block) > unsigned(Block::Water)) {
		throw std::out_of_range("Invalid voxel coordinate/block type");
	}
	if (get(x, y, z) == block) {
		return;
	}
	const auto editKey = key(x, y, z);
	if (!mEdits.contains(editKey) && mEdits.size() >= 1000000) {
		throw std::runtime_error("Voxel edit budget exceeded");
	}
	if (block == generatedBlock(mSettings, x, y, z)) {
		mEdits.erase(editKey);
	}
	else {
		mEdits.insert_or_assign(editKey, block);
	}
	// An edit at a boundary changes the neighbour's exposed faces as well.
	for (const auto delta : { MATH::Vector3f {}, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } }) {
		const int neighbourX = x + int(delta.x);
		const int neighbourY = y + int(delta.y);
		const int neighbourZ = z + int(delta.z);
		if (contains(neighbourX, neighbourY, neighbourZ)) {
			mModels.erase({ unsigned(neighbourX) / mSettings.ChunkSize, unsigned(neighbourY) / mSettings.ChunkSize,
				unsigned(neighbourZ) / mSettings.ChunkSize });
		}
	}
}
std::shared_ptr<RENDER::ModelInterface> VoxelWorld::model(unsigned x, unsigned y, unsigned z, unsigned& budget) {
	if (x >= mSettings.ChunksX || y >= mSettings.ChunksY || z >= mSettings.ChunksZ) {
		throw std::out_of_range("Invalid voxel chunk");
	}
	const auto key = std::tuple { x, y, z };
	if (const auto found = mModels.find(key); found != mModels.end()) {
		found->second.LastUse = ++mClock;
		return found->second.Model;
	}
	if (!budget) {
		return {};
	}
	--budget;
	return cacheModel(mModels, key, buildVoxelChunk(mSettings, x, y, z, [this](int bx, int by, int bz) { return get(bx, by, bz); }),
		mSettings.CacheMegabytes, ++mClock);
}
void VoxelWorld::clearCache() {
	mModels.clear();
}
void VoxelWorld::reset() {
	mEdits.clear();
	clearCache();
}
std::vector<VoxelEdit> VoxelWorld::edits() const {
	std::vector<std::pair<uint64_t, Block>> ordered(mEdits.begin(), mEdits.end());
	std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	std::vector<VoxelEdit> result;
	const uint64_t depth = mSettings.ChunksZ * mSettings.ChunkSize;
	const uint64_t height = mSettings.ChunksY * mSettings.ChunkSize;
	for (const auto& [key, block] : ordered) {
		result.push_back({ int(key / depth / height), int(key / depth % height), int(key % depth), block });
	}
	return result;
}
}
