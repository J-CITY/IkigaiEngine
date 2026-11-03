#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mathModule/math.h"
#include "renderModule/backends/interface/modelInterface.h"
#include "renderModule/morphTarget.h"
#include "renderModule/vertex.h"

namespace IKIGAI::RESOURCES {
// CPU-side result of a model import. Plain data only: it owns no renderer,
// GPU or scene objects, so it can be produced off the main thread and later
// turned into RENDER::ModelInterface/MeshInterface objects by
// AssimpParser::CreateModelResources on the render thread.

struct ImportedBone {
	std::string Name {};
	RENDER::BoneInfo Info {};
};

struct ImportedMesh {
	// Name of the scene node that references this mesh (used to bind morph
	// animation channels to meshes).
	std::string NodeName {};
	// Accumulated node transform. Informational only: the importer does not
	// apply node transforms to vertices.
	MATH::Matrix4f NodeTransform {};
	unsigned MaterialIndex = 0;
	std::vector<Vertex> Vertices {};
	std::vector<uint32_t> Indices {};
	std::vector<RENDER::MorphTarget> MorphTargets {};
	// Object-space axis-aligned bounds of Vertices (zero for an empty mesh).
	MATH::Vector3f BoundsMin { 0.0f, 0.0f, 0.0f };
	MATH::Vector3f BoundsMax { 0.0f, 0.0f, 0.0f };
};

struct ImportedMorphKey {
	double Time = 0.0;
	std::vector<unsigned> Values {};
	std::vector<double> Weights {};
};

struct ImportedMorphChannel {
	std::string NodeName {};
	std::vector<ImportedMorphKey> Keys {};
};

struct ImportedMorphAnimation {
	std::string Name {};
	double Duration = 0.0;
	double TicksPerSecond = 0.0;
	std::vector<ImportedMorphChannel> Channels {};
};

struct ImportedModel {
	// Scene material names in material index order.
	std::vector<std::string> MaterialNames {};
	// Meshes in depth-first scene node order (the final model mesh order).
	std::vector<ImportedMesh> Meshes {};
	// Skin bones in first-use order, one entry per bone name.
	std::vector<ImportedBone> Bones {};
	// Raw morph animation channels; resolved against the created meshes.
	std::vector<ImportedMorphAnimation> MorphAnimations {};
};

struct ModelBuildOptions {
	bool ImportMaterials = true;
	bool ImportMorphAnimations = true;
};
}
