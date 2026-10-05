#pragma once

#include <cstddef>
#include <cstdint>

namespace IKIGAI::RENDER {

// Metal vertex fetch requires stricter alignment than the packed C++ Vertex:
// float2 offsets must be 8-byte aligned, float4/int4 offsets 16-byte aligned.
// Meshes uploaded for Metal use this layout. Locations match meshGl (0..6).
struct MetalMeshVertex {
	float position[3];
	float positionPad;
	float texCoord[2];
	float texCoordPad[2];
	float normal[3];
	float normalPad;
	float tangent[3];
	float tangentPad;
	float bitangent[3];
	float bitangentPad;
	int boneIds[4];
	float weights[4];
};

static_assert(offsetof(MetalMeshVertex, texCoord) % 8 == 0, "uv align");
static_assert(offsetof(MetalMeshVertex, boneIds) % 16 == 0, "bone align");
static_assert(offsetof(MetalMeshVertex, weights) % 16 == 0, "weight align");
static_assert(sizeof(MetalMeshVertex) % 16 == 0, "stride align");

inline size_t MetalMeshVertexOffset(size_t location) {
	switch (location) {
	case 0: return offsetof(MetalMeshVertex, position);
	case 1: return offsetof(MetalMeshVertex, texCoord);
	case 2: return offsetof(MetalMeshVertex, normal);
	case 3: return offsetof(MetalMeshVertex, tangent);
	case 4: return offsetof(MetalMeshVertex, bitangent);
	case 5: return offsetof(MetalMeshVertex, boneIds);
	case 6: return offsetof(MetalMeshVertex, weights);
	default: return static_cast<size_t>(-1);
	}
}

}
