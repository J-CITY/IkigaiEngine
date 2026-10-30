#pragma once
#include "component.h"
#include "surfaceModule/surfaceFrame.h"
#include "surfaceModule/surfaceSettings.h"

namespace IKIGAI::SURFACE {
class VoxelWorld;
}

namespace IKIGAI::ECS {
class ChunkModelRenderer : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type = {};
		// Retained for old scenes; voxel terrain is configured by Voxels.
		std::string Path = {};
		SURFACE::VoxelSettings Voxels = {};
		std::vector<SURFACE::VoxelEdit> Edits = {};
		template <class C> static void serde(C& context, Descriptor& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "ChunkModelRendererType")
				.field(&Descriptor::Path, "Path", default_ { std::string {} })
				.field(&Descriptor::Voxels, "Voxels", default_ { SURFACE::VoxelSettings {} })
				.field(&Descriptor::Edits, "Edits", default_ { std::vector<SURFACE::VoxelEdit> {} });
		}
	};
	explicit ChunkModelRenderer(UTILS::Ref<Object> object);
	ChunkModelRenderer(UTILS::Ref<Object> object, const Descriptor& descriptor);
	ChunkModelRenderer(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	Descriptor getDescriptor() const;
	void apply(const SURFACE::VoxelSettings& settings);
	const SURFACE::VoxelSettings& settings() const;
	SURFACE::Block getBlock(int x, int y, int z) const;
	void setBlock(int x, int y, int z, SURFACE::Block block);
	// Owned per component: independent worlds never share mesh slots or edits.
	std::shared_ptr<SURFACE::VoxelWorld> mModel = {};
	SURFACE::FrameState frame = {};
	static auto GetMembers() {
		return std::tuple {};
	}

private:
	std::string mPath = {};
};
template <> inline std::string GetType<ChunkModelRenderer>() {
	return "class IKIGAI::ECS::ChunkModelRenderer";
}
template <> inline std::string GetComponentName<ChunkModelRenderer>() {
	return "ChunkModelRenderer";
}
}
