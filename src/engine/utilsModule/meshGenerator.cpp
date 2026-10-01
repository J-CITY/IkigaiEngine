#include "meshGenerator.h"

#include <renderModule/backends/interface/meshInterface.h>
#include <renderModule/backends/interface/modelInterface.h>
#include <renderModule/backends/meshFactory.h>
#include "stdLoader.h"
#include "log/loggerDefine.h"
#include "renderModule/vertex.h"


using namespace IKIGAI;

std::shared_ptr<RENDER::ModelInterface> MeshGenerator::CreateTerrainFromHeightmap(const std::string& fileName) {
	std::shared_ptr<RENDER::ModelInterface> model = RENDER::CreateEmptyModel();

	const auto heightData = GetHeightDataFromImage(fileName);
	if (heightData.size() == 0) {
		//LOG_ERROR << ("Heightmap size empty");
		return model;
	}

	auto rows = heightData.size();
	auto columns = heightData[0].size();
	auto numVertices = rows * columns;

	std::vector<Vertex> vertices;

	const auto textureStepU = 0.1f;
	const auto textureStepV = 0.1f;
	for (auto i = 0; i < rows; i++) {
		for (auto j = 0; j < columns; j++) {
			const auto factorRow = static_cast<float>(i) / static_cast<float>(rows - 1);
			const auto factorColumn = static_cast<float>(j) / static_cast<float>(columns - 1);
			const auto& fVertexHeight = heightData[i][j];
			vertices.push_back(
			{
					MATH::Vector3{-0.5f + factorColumn, fVertexHeight, -0.5f + factorRow},
					MATH::Vector2f{textureStepU * j, textureStepV * i}
				}
			);
		}
	}


	std::vector<unsigned> indices;

	for (auto i = 0; i < rows - 1; i++) {
		for (auto j = 0; j < columns; j++) {
			const auto index0 = i * columns + j;
			const auto index1 = (i+1) * columns + j;

			if (indices.empty()) {
				indices.push_back(index0);
				indices.push_back(index1);
			}
			else {
				const auto prev0 = indices[indices.size() - 1];
				if (indices.size() > 2) {
					const auto prev1 = indices[indices.size() - 3];
					indices.push_back(prev1);
					indices.push_back(prev0);
				}
				indices.push_back(index0);
				indices.push_back(index0);
				indices.push_back(prev0);
				indices.push_back(index1);
			}
		}
	}

	std::shared_ptr<RENDER::MeshInterface> mesh = RENDER::CreateMesh(vertices, indices, 0);

	model->getMeshes().push_back(mesh);
	return model;
}

std::vector<std::vector<float>> MeshGenerator::GetHeightDataFromImage(const std::string& fileName) {
    UTILS::STBiSetFlipVerticallyOnLoad(true);
    int width, height, bytesPerPixel;
    const auto imageData = UTILS::STBiLoadf(fileName.c_str(), &width, &height, &bytesPerPixel, 0);
    if (imageData == nullptr) {
        LOG_ERROR << ("Failed to load heightmap image " + fileName);
        return std::vector<std::vector<float>>();
    }

    std::vector<std::vector<float>> result(height, std::vector<float>(width));
    auto pixelPtr = &imageData[0];
    for (auto i = 0; i < height; i++) {
        for (auto j = 0; j < width; j++) {
            result[i][j] = (*pixelPtr) / 255.0f;
            pixelPtr += bytesPerPixel;
        }
    }
    UTILS::STBiImageFree(imageData);
    return result;
}

std::shared_ptr<RENDER::ModelInterface> MeshGenerator::CreateSquare(unsigned rez, int width, int height) {
	std::shared_ptr<RENDER::ModelInterface> model = RENDER::CreateEmptyModel();
	auto numVertices = rez * rez;

	std::vector<Vertex> vertices;

	const auto textureStepU = 0.1f;
	const auto textureStepV = 0.1f;
	for (auto i = 0; i < rez; i++) {
		for (auto j = 0; j < rez; j++) {
			//const auto factorRow = static_cast<float>(i) / static_cast<float>(rez - 1);
			//const auto factorColumn = static_cast<float>(j) / static_cast<float>(rez - 1);
			//const auto& fVertexHeight = 0.0f;
			//vertices.push_back(
			//	{
			//			MATH::Vector3{-0.5f + factorColumn, fVertexHeight, -0.5f + factorRow},
			//			MATH::Vector2f{textureStepU * j, textureStepV * i}
			//	}
			//);

			vertices.push_back(
				{
						MATH::Vector3{-width / 2.0f + width * i / (float)rez, 0.0f, -height / 2.0f + height * j / (float)rez},
						MATH::Vector2f{i / (float)rez, j / (float)rez}
				}
			);
			vertices.push_back(
				{
						MATH::Vector3{-width / 2.0f + width * (i + 1) / (float)rez, 0.0f, -height / 2.0f + height * j / (float)rez},
						MATH::Vector2f{(i + 1) / (float)rez, j / (float)rez}
				}
			);
			vertices.push_back(
				{
						MATH::Vector3{-width / 2.0f + width * i / (float)rez, 0.0f, -height / 2.0f + height * (j + 1) / (float)rez},
						MATH::Vector2f{i / (float)rez, (j + 1) / (float)rez}
				}
			);
			vertices.push_back(
				{
						MATH::Vector3{-width / 2.0f + width * (i + 1) / (float)rez, 0.0f, -height / 2.0f + height * (j + 1) / (float)rez},
						MATH::Vector2f{(i + 1) / (float)rez, (j + 1) / (float)rez}
				}
			);
			
		}
	}


	std::vector<unsigned> indices;

	//for (auto i = 0; i < rez - 1; i++) {
	//	for (auto j = 0; j < rez; j++) {
	//		const auto index0 = i * rez + j;
	//		const auto index1 = (i + 1) * rez + j;
	//
	//		if (indices.empty()) {
	//			indices.push_back(index0);
	//			indices.push_back(index1);
	//		}
	//		else {
	//			const auto prev0 = indices[indices.size() - 1];
	//			if (indices.size() > 2) {
	//				const auto prev1 = indices[indices.size() - 3];
	//				indices.push_back(prev1);
	//				indices.push_back(prev0);
	//			}
	//			indices.push_back(index0);
	//			indices.push_back(index0);
	//			indices.push_back(prev0);
	//			indices.push_back(index1);
	//		}
	//	}
	//}

	std::shared_ptr<RENDER::MeshInterface> mesh = RENDER::CreateMesh(vertices, indices, 0);
	model->getMeshes().push_back(mesh);
	return model;
}

std::shared_ptr<RENDER::ModelInterface> MeshGenerator::CreateQuad() {
	std::shared_ptr<RENDER::ModelInterface> model = RENDER::CreateEmptyModel();

	std::vector<Vertex> vertices;
	std::vector<unsigned> indices = {
		0, 1, 2,
		0, 2, 3
	};

	//vertices.push_back({MATH::Vector3{1.0f, 1.0f, 0.0f}, MATH::Vector2f{1.0f, 1.0f}});
	//vertices.push_back({MATH::Vector3{1.0f, -1.0f, 0.0f}, MATH::Vector2f{1.0f, 0.0f}});
	//vertices.push_back({MATH::Vector3{-1.0f, -1.0f, 0.0f}, MATH::Vector2f{0.0f, 0.0f}});
	//vertices.push_back({MATH::Vector3{-1.0f, 1.0f, 0.0f}, MATH::Vector2f{0.0f, 1.0f}});


	vertices.push_back({MATH::Vector3{-1.0f, 1.0f, 0.0f}, MATH::Vector2f{0.0f, 1.0f}});
	vertices.push_back({MATH::Vector3{-1.0f, -1.0f, 0.0f}, MATH::Vector2f{0.0f, 0.0f}});
	vertices.push_back({MATH::Vector3{1.5f, -1.0f, 0.0f}, MATH::Vector2f{1.0f, 0.0f}});
	vertices.push_back({MATH::Vector3{1.5f, 1.0f, 0.0f}, MATH::Vector2f{1.0f, 1.0f}});


	std::shared_ptr<RENDER::MeshInterface> mesh = RENDER::CreateMesh(vertices, indices, 0);
	model->getMeshes().push_back(mesh);
	return model;
}

// CUNCK

#include <coreModule/glmWrapper.hpp>

static const int transparent[16] = {2, 0, 0, 0, 1, 0, 0, 0, 3, 4, 0, 0, 0, 0, 0, 0};
static const char* blocknames[16] = {
	"air", "dirt", "topsoil", "grass", "leaves", "wood", "stone", "sand",
	"water", "glass", "brick", "ore", "woodrings", "white", "black", "x-y"
};




static chunk* chunk_slot[CHUNKSLOTS] = {0};

int now = 0;;


chunk::chunk() : ax(0), ay(0), az(0) {
		memset(blk, 0, sizeof(blk));
		left = right = below = above = front = back = 0;
		lastused = now;
		slot = 0;
		changed = true;
		initialized = false;
		noised = false;
	}

chunk::chunk(int x, int y, int z) : ax(x), ay(y), az(z) {
		memset(blk, 0, sizeof blk);
		left = right = below = above = front = back = 0;
		lastused = now;
		slot = 0;
		changed = true;
		initialized = false;
		noised = false;
	}

	uint8_t chunk::get(int x, int y, int z) const {
		if (x < 0)
			return left ? left->blk[x + CX][y][z] : 0;
		if (x >= CX)
			return right ? right->blk[x - CX][y][z] : 0;
		if (y < 0)
			return below ? below->blk[x][y + CY][z] : 0;
		if (y >= CY)
			return above ? above->blk[x][y - CY][z] : 0;
		if (z < 0)
			return front ? front->blk[x][y][z + CZ] : 0;
		if (z >= CZ)
			return back ? back->blk[x][y][z - CZ] : 0;
		return blk[x][y][z];
	}

	bool chunk::isblocked(int x1, int y1, int z1, int x2, int y2, int z2) {
		// Invisible blocks are always "blocked"
		if (!blk[x1][y1][z1])
			return true;

		// Leaves do not block any other block, including themselves
		if (transparent[get(x2, y2, z2)] == 1)
			return false;

		// Non-transparent blocks always block line of sight
		if (!transparent[get(x2, y2, z2)])
			return true;

		// Otherwise, LOS is only blocked by blocks if the same transparency type
		return transparent[get(x2, y2, z2)] == transparent[blk[x1][y1][z1]];
	}

	void chunk::set(int x, int y, int z, uint8_t type) {
		// If coordinates are outside this chunk, find the right one.
		if (x < 0) {
			if (left)
				left->set(x + CX, y, z, type);
			return;
		}
		if (x >= CX) {
			if (right)
				right->set(x - CX, y, z, type);
			return;
		}
		if (y < 0) {
			if (below)
				below->set(x, y + CY, z, type);
			return;
		}
		if (y >= CY) {
			if (above)
				above->set(x, y - CY, z, type);
			return;
		}
		if (z < 0) {
			if (front)
				front->set(x, y, z + CZ, type);
			return;
		}
		if (z >= CZ) {
			if (back)
				back->set(x, y, z - CZ, type);
			return;
		}

		// Change the block
		blk[x][y][z] = type;
		changed = true;

		// When updating blocks at the edge of this chunk,
		// visibility of blocks in the neighbouring chunk might change.
		if (x == 0 && left)
			left->changed = true;
		if (x == CX - 1 && right)
			right->changed = true;
		if (y == 0 && below)
			below->changed = true;
		if (y == CY - 1 && above)
			above->changed = true;
		if (z == 0 && front)
			front->changed = true;
		if (z == CZ - 1 && back)
			back->changed = true;
	}

	float chunk::noise2d(float x, float y, int seed, int octaves, float persistence) {
		float sum = 0;
		float strength = 1.0;
		float scale = 1.0;

		for (int i = 0; i < octaves; i++) {
			sum += strength * glm::simplex(glm::vec2(x, y) * scale);
			scale *= 2.0;
			strength *= persistence;
		}

		return sum;
	}

	float chunk::noise3d_abs(float x, float y, float z, int seed, int octaves, float persistence) {
		float sum = 0;
		float strength = 1.0;
		float scale = 1.0;

		for (int i = 0; i < octaves; i++) {
			sum += strength * fabs(glm::simplex(glm::vec3(x, y, z) * scale));
			scale *= 2.0;
			strength *= persistence;
		}

		return sum;
	}

	void chunk::noise(int seed) {
		if (noised)
			return;
		else
			noised = true;

		for (int x = 0; x < CX; x++) {
			for (int z = 0; z < CZ; z++) {
				// Land height
				float n = noise2d((x + ax * CX) / 256.0, (z + az * CZ) / 256.0, seed, 5, 0.8) * 4;
				int h = n * 2;
				int y = 0;

				// Land blocks
				for (y = 0; y < CY; y++) {
					// Are we above "ground" level?
					if (y + ay * CY >= h) {
						// If we are not yet up to sea level, fill with water blocks
						if (y + ay * CY < SEALEVEL) {
							blk[x][y][z] = 8;
							continue;
							// Otherwise, we are in the air
						} else {
							// A tree!
							if (get(x, y - 1, z) == 3 && (rand() & 0xff) == 0) {
								// Trunk
								h = (rand() & 0x3) + 3;
								for (int i = 0; i < h; i++)
									set(x, y + i, z, 5);

								// Leaves
								for (int ix = -3; ix <= 3; ix++) {
									for (int iy = -3; iy <= 3; iy++) {
										for (int iz = -3; iz <= 3; iz++) {
											if (ix * ix + iy * iy + iz * iz < 8 + (rand() & 1) && !get(x + ix, y + h + iy, z + iz))
												set(x + ix, y + h + iy, z + iz, 4);
										}
									}
								}
							}
							break;
						}
					}

					// Random value used to determine land type
					float r = noise3d_abs((x + ax * CX) / 16.0, (y + ay * CY) / 16.0, (z + az * CZ) / 16.0, -seed, 2, 1);

					// Sand layer
					if (n + r * 5 < 4)
						blk[x][y][z] = 7;
					// Dirt layer, but use grass blocks for the top
					else if (n + r * 5 < 8)
						blk[x][y][z] = (h < SEALEVEL || y + ay * CY < h - 1) ? 1 : 3;
					// Rock layer
					else if (r < 1.25)
						blk[x][y][z] = 6;
					// Sometimes, ores!
					else
						blk[x][y][z] = 11;
				}
			}
		}
		changed = true;
	}

	void chunk::update() {
		std::vector<Vertex> vertex(CX * CY * CZ * 18);
		int i = 0;
		int merged = 0;
		bool vis = false;;

		// View from negative x

		for (int x = CX - 1; x >= 0; x--) {
			for (int y = 0; y < CY; y++) {
				for (int z = 0; z < CZ; z++) {
					// Line of sight blocked?
					if (isblocked(x, y, z, x - 1, y, z)) {
						vis = false;
						continue;
					}

					uint8_t top = blk[x][y][z];
					uint8_t bottom = blk[x][y][z];
					uint8_t side = blk[x][y][z];

					// Grass block has dirt sides and bottom
					if (top == 3) {
						bottom = 1;
						side = 2;
						// Wood blocks have rings on top and bottom
					} else if (top == 5) {
						top = bottom = 12;
					}

					// Same block as previous one? Extend it.
					if (vis && z != 0 && blk[x][y][z] == blk[x][y][z - 1]) {
						vertex[i - 5] = Vertex(MATH::Vector3f(x, y, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i - 2] = Vertex(MATH::Vector3f(x, y, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i - 1] = Vertex(MATH::Vector3f(x, y + 1, z + 1), MATH::Vector2f(side, 0.0f));
						merged++;
						// Otherwise, add a new quad.
					} else {
						vertex[i++] = Vertex(MATH::Vector3f(x, y, z), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x, y, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x, y + 1, z), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x, y + 1, z), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x, y, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x, y + 1, z + 1), MATH::Vector2f(side, 0.0f));
					}

					vis = true;
				}
			}
		}

		// View from positive x

		for (int x = 0; x < CX; x++) {
			for (int y = 0; y < CY; y++) {
				for (int z = 0; z < CZ; z++) {
					if (isblocked(x, y, z, x + 1, y, z)) {
						vis = false;
						continue;
					}

					uint8_t top = blk[x][y][z];
					uint8_t bottom = blk[x][y][z];
					uint8_t side = blk[x][y][z];

					if (top == 3) {
						bottom = 1;
						side = 2;
					} else if (top == 5) {
						top = bottom = 12;
					}

					if (vis && z != 0 && blk[x][y][z] == blk[x][y][z - 1]) {
						vertex[i - 4] = Vertex(MATH::Vector3f(x + 1, y, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i - 2] = Vertex(MATH::Vector3f(x + 1, y + 1, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i - 1] = Vertex(MATH::Vector3f(x + 1, y, z + 1), MATH::Vector2f(side, 0.0f));
						merged++;
					} else {
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y, z), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y + 1, z), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y + 1, z), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y + 1, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y, z + 1), MATH::Vector2f(side, 0.0f));
					}
					vis = true;
				}
			}
		}

		// View from negative y

		for (int x = 0; x < CX; x++) {
			for (int y = CY - 1; y >= 0; y--) {
				for (int z = 0; z < CZ; z++) {
					if (isblocked(x, y, z, x, y - 1, z)) {
						vis = false;
						continue;
					}

					uint8_t top = blk[x][y][z];
					uint8_t bottom = blk[x][y][z];

					if (top == 3) {
						bottom = 1;
					} else if (top == 5) {
						top = bottom = 12;
					}

					if (vis && z != 0 && blk[x][y][z] == blk[x][y][z - 1]) {
						vertex[i - 4] = Vertex(MATH::Vector3f(x, y, z + 1), MATH::Vector2f(bottom + 128, 0.0f));
						vertex[i - 2] = Vertex(MATH::Vector3f(x + 1, y, z + 1), MATH::Vector2f(bottom + 128, 0.0f));
						vertex[i - 1] = Vertex(MATH::Vector3f(x, y, z + 1), MATH::Vector2f(bottom + 128, 0.0f));
						merged++;
					} else {
						vertex[i++] = Vertex(MATH::Vector3f(x, y, z), MATH::Vector2f(bottom + 128, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y, z), MATH::Vector2f(bottom + 128, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x, y, z + 1), MATH::Vector2f(bottom + 128, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y, z), MATH::Vector2f(bottom + 128, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y, z + 1), MATH::Vector2f(bottom + 128, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x, y, z + 1), MATH::Vector2f(bottom + 128, 0.0f));
					}
					vis = true;
				}
			}
		}

		// View from positive y

		for (int x = 0; x < CX; x++) {
			for (int y = 0; y < CY; y++) {
				for (int z = 0; z < CZ; z++) {
					if (isblocked(x, y, z, x, y + 1, z)) {
						vis = false;
						continue;
					}

					uint8_t top = blk[x][y][z];
					uint8_t bottom = blk[x][y][z];

					if (top == 3) {
						bottom = 1;
					} else if (top == 5) {
						top = bottom = 12;
					}

					if (vis && z != 0 && blk[x][y][z] == blk[x][y][z - 1]) {
						vertex[i - 5] = Vertex(MATH::Vector3f(x, y + 1, z + 1), MATH::Vector2f(top + 128, 0.0f));
						vertex[i - 2] = Vertex(MATH::Vector3f(x, y + 1, z + 1), MATH::Vector2f(top + 128, 0.0f));
						vertex[i - 1] = Vertex(MATH::Vector3f(x + 1, y + 1, z + 1), MATH::Vector2f(top + 128, 0.0f));
						merged++;
					} else {
						vertex[i++] = Vertex(MATH::Vector3f(x, y + 1, z), MATH::Vector2f(top + 128, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x, y + 1, z + 1), MATH::Vector2f(top + 128, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y + 1, z), MATH::Vector2f(top + 128, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y + 1, z), MATH::Vector2f(top + 128, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x, y + 1, z + 1), MATH::Vector2f(top + 128, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y + 1, z + 1), MATH::Vector2f(top + 128, 0.0f));
					}
					vis = true;
				}
			}
		}

		// View from negative z

		for (int x = 0; x < CX; x++) {
			for (int z = CZ - 1; z >= 0; z--) {
				for (int y = 0; y < CY; y++) {
					if (isblocked(x, y, z, x, y, z - 1)) {
						vis = false;
						continue;
					}

					uint8_t top = blk[x][y][z];
					uint8_t bottom = blk[x][y][z];
					uint8_t side = blk[x][y][z];

					if (top == 3) {
						bottom = 1;
						side = 2;
					} else if (top == 5) {
						top = bottom = 12;
					}

					if (vis && y != 0 && blk[x][y][z] == blk[x][y - 1][z]) {
						vertex[i - 5] = Vertex(MATH::Vector3f(x, y + 1, z), MATH::Vector2f(side, 0.0f));
						vertex[i - 3] = Vertex(MATH::Vector3f(x, y + 1, z), MATH::Vector2f(side, 0.0f));
						vertex[i - 2] = Vertex(MATH::Vector3f(x + 1, y + 1, z), MATH::Vector2f(side, 0.0f));
						merged++;
					} else {
						vertex[i++] = Vertex(MATH::Vector3f(x, y, z), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x, y + 1, z), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y, z), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x, y + 1, z), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y + 1, z), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y, z), MATH::Vector2f(side, 0.0f));
					}
					vis = true;
				}
			}
		}

		// View from positive z

		for (int x = 0; x < CX; x++) {
			for (int z = 0; z < CZ; z++) {
				for (int y = 0; y < CY; y++) {
					if (isblocked(x, y, z, x, y, z + 1)) {
						vis = false;
						continue;
					}

					uint8_t top = blk[x][y][z];
					uint8_t bottom = blk[x][y][z];
					uint8_t side = blk[x][y][z];

					if (top == 3) {
						bottom = 1;
						side = 2;
					} else if (top == 5) {
						top = bottom = 12;
					}

					if (vis && y != 0 && blk[x][y][z] == blk[x][y - 1][z]) {
						vertex[i - 4] = Vertex(MATH::Vector3f(x, y + 1, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i - 3] = Vertex(MATH::Vector3f(x, y + 1, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i - 1] = Vertex(MATH::Vector3f(x + 1, y + 1, z + 1), MATH::Vector2f(side, 0.0f));
						merged++;
					} else {
						vertex[i++] = Vertex(MATH::Vector3f(x, y, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x, y + 1, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x, y + 1, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y, z + 1), MATH::Vector2f(side, 0.0f));
						vertex[i++] = Vertex(MATH::Vector3f(x + 1, y + 1, z + 1), MATH::Vector2f(side, 0.0f));
					}
					vis = true;
				}
			}
		}

		changed = false;
		elements = i;

		// If this chunk is empty, no need to allocate a chunk slot.
		if (!elements)
			return;

		// If we don't have an active slot, find one
		if (chunk_slot[slot] != this) {
			int lru = 0;
			for (int i = 0; i < CHUNKSLOTS; i++) {
				// If there is an empty slot, use it
				if (!chunk_slot[i]) {
					lru = i;
					break;
				}
				// Otherwise try to find the least recently used slot
				if (chunk_slot[i]->lastused < chunk_slot[lru]->lastused)
					lru = i;
			}

			// If the slot is empty, create a new VBO
			if (!chunk_slot[lru]) {
				mModel = RENDER::CreateEmptyModel();
				std::vector<Vertex> v;
				std::vector<unsigned> i;
				mModel->getMeshes().push_back(RENDER::CreateMesh(v, i, 0));
				// Otherwise, steal it from the previous slot owner
			} else {
				mModel = chunk_slot[lru]->mModel;
				chunk_slot[lru]->changed = true;
			}

			slot = lru;
			chunk_slot[slot] = this;
		}
		// Upload vertices (GL-specific bufferData path is currently disabled).
		//glBindBuffer(GL_ARRAY_BUFFER, vbo);
		//glBufferData(GL_ARRAY_BUFFER, i * sizeof * vertex, vertex, GL_STATIC_DRAW);
	}

	bool chunk::render() {
		if (changed)
			update();

		lastused = now;

		if (!elements)
			return false;
		return true;
		//glBindBuffer(GL_ARRAY_BUFFER, vbo);
		//glVertexAttribPointer(attribute_coord, 4, GL_BYTE, GL_FALSE, 0, 0);
		//glDrawArrays(GL_TRIANGLES, 0, elements);
	}


	superchunk::superchunk() {
		seed = time(NULL);
		for (int x = 0; x < SCX; x++)
			for (int y = 0; y < SCY; y++)
				for (int z = 0; z < SCZ; z++)
					c[x][y][z] = new chunk(x - SCX / 2, y - SCY / 2, z - SCZ / 2);

		for (int x = 0; x < SCX; x++)
			for (int y = 0; y < SCY; y++)
				for (int z = 0; z < SCZ; z++) {
					if (x > 0)
						c[x][y][z]->left = c[x - 1][y][z];
					if (x < SCX - 1)
						c[x][y][z]->right = c[x + 1][y][z];
					if (y > 0)
						c[x][y][z]->below = c[x][y - 1][z];
					if (y < SCY - 1)
						c[x][y][z]->above = c[x][y + 1][z];
					if (z > 0)
						c[x][y][z]->front = c[x][y][z - 1];
					if (z < SCZ - 1)
						c[x][y][z]->back = c[x][y][z + 1];
				}
	}

	uint8_t superchunk::get(int x, int y, int z) const {
		int cx = (x + CX * (SCX / 2)) / CX;
		int cy = (y + CY * (SCY / 2)) / CY;
		int cz = (z + CZ * (SCZ / 2)) / CZ;

		if (cx < 0 || cx >= SCX || cy < 0 || cy >= SCY || cz <= 0 || cz >= SCZ)
			return 0;

		return c[cx][cy][cz]->get(x & (CX - 1), y & (CY - 1), z & (CZ - 1));
	}

	void superchunk::set(int x, int y, int z, uint8_t type) {
		int cx = (x + CX * (SCX / 2)) / CX;
		int cy = (y + CY * (SCY / 2)) / CY;
		int cz = (z + CZ * (SCZ / 2)) / CZ;

		if (cx < 0 || cx >= SCX || cy < 0 || cy >= SCY || cz <= 0 || cz >= SCZ)
			return;

		c[cx][cy][cz]->set(x & (CX - 1), y & (CY - 1), z & (CZ - 1), type);
	}

	void superchunk::update(RENDER::OpaqueDrawables& drawList, MATH::Matrix4f pv, std::shared_ptr<RENDER::MaterialInterface> material, const MATH::Vector3f& campos) {
		float ud = std::numeric_limits<float>::max();
		int ux = -1;
		int uy = -1;
		int uz = -1;
		
		for (int x = 0; x < SCX; x++) {
			for (int y = 0; y < SCY; y++) {
				for (int z = 0; z < SCZ; z++) {
					auto pos = MATH::Vector3f(c[x][y][z]->ax * CX, c[x][y][z]->ay * CY, c[x][y][z]->az * CZ);
					float distanceToActor = MATH::Vector3f::Distance(pos, campos);
					MATH::Matrix4f model = MATH::Matrix4f::Translation(pos);
					auto mvp = pv * model;

					// Is this chunk on the screen?
					auto center = mvp * MATH::Vector4f(CX / 2.f, CY / 2.f, CZ / 2.f, 1.f);

					float d = MATH::Vector4f::Length(center);
					center.x /= center.w;
					center.y /= center.w;

					// If it is behind the camera, don't bother drawing it
					if (center.z < -CY / 2)
						continue;

					// If it is outside the screen, don't bother drawing it
					if (fabsf(center.x) > 1 + fabsf(CY * 2 / center.w) || fabsf(center.y) > 1 + fabsf(CY * 2 / center.w))
						continue;

					// If this chunk is not initialized, skip it
					if (!c[x][y][z]->initialized) {
						// But if it is the closest to the camera, mark it for initialization
						if (ux < 0 || d < ud) {
							ud = d;
							ux = x;
							uy = y;
							uz = z;
						}
						continue;
					}

					//glUniformMatrix4fv(uniform_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
					auto _c = c[x][y][z];
					if (c[x][y][z]->render()) {
						RENDER::Drawable drawable;
						drawable.mesh = c[x][y][z]->mModel->getMeshes()[0];
						drawable.world = model;
						drawable.material = material;
						drawList.emplace(distanceToActor, drawable);
					}
				}
			}
		}

		if (ux >= 0) {
			c[ux][uy][uz]->noise(seed);
			if (c[ux][uy][uz]->left)
				c[ux][uy][uz]->left->noise(seed);
			if (c[ux][uy][uz]->right)
				c[ux][uy][uz]->right->noise(seed);
			if (c[ux][uy][uz]->below)
				c[ux][uy][uz]->below->noise(seed);
			if (c[ux][uy][uz]->above)
				c[ux][uy][uz]->above->noise(seed);
			if (c[ux][uy][uz]->front)
				c[ux][uy][uz]->front->noise(seed);
			if (c[ux][uy][uz]->back)
				c[ux][uy][uz]->back->noise(seed);
			c[ux][uy][uz]->initialized = true;
		}
	}


