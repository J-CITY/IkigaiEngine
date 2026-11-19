#pragma once
#include <memory>
#include <string>
#include <vector>

#include "drawable.h"

namespace IKIGAI {
namespace RENDER {
	class ModelInterface;
	class Model;
}

class MeshGenerator {
public:
	static std::shared_ptr<RENDER::ModelInterface> CreateTerrainFromHeightmap(const std::string& fileName);
	static std::vector<std::vector<float>> GetHeightDataFromImage(const std::string& fileName);
	static std::shared_ptr<RENDER::ModelInterface> CreateSquare(unsigned rez, int width, int height);
	static std::shared_ptr<RENDER::ModelInterface> CreateQuad();
};

// Size of one chunk in blocks
constexpr int CX = 16;
constexpr int CY = 32;
constexpr int CZ = 16;

// Number of chunks in the world
constexpr int SCX = 32;
constexpr int SCY = 2;
constexpr int SCZ = 32;

// Sea level
constexpr int SEALEVEL = 4;

// Number of VBO slots for chunks
constexpr int CHUNKSLOTS = SCX * SCY * SCZ;

struct chunk {
	uint8_t blk[CX][CY][CZ];
	struct chunk *left, *right, *below, *above, *front, *back;
	int slot;

	// unsigned vbo;

	int elements;
	int lastused;
	bool changed;
	bool noised;
	bool initialized;
	int ax;
	int ay;
	int az;

	std::shared_ptr<RENDER::ModelInterface> mModel = nullptr;

	chunk();
	chunk(int x, int y, int z);
	uint8_t get(int x, int y, int z) const;
	bool isblocked(int x1, int y1, int z1, int x2, int y2, int z2);
	void set(int x, int y, int z, uint8_t type);
	void noise(int seed);
	void update();
	bool render();

	static float noise2d(float x, float y, int seed, int octaves, float persistence);
	static float noise3d_abs(float x, float y, float z, int seed, int octaves, float persistence);
};

struct superchunk {
	chunk* c[SCX][SCY][SCZ];
	time_t seed;

	superchunk();
	uint8_t get(int x, int y, int z) const;
	void set(int x, int y, int z, uint8_t type);
	void update(
		RENDER::OpaqueDrawables& drawList, MATH::Matrix4f vp, std::shared_ptr<RENDER::MaterialInterface> material, const MATH::Vector3f& campos);
};

}
