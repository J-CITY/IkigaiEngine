#pragma once
#include "component.h"
#include "surfaceModule/surfaceFrame.h"
#include "surfaceModule/surfaceSettings.h"

namespace IKIGAI::RENDER {
class MaterialInterface;
}
namespace IKIGAI::SURFACE {
class Terrain;
}

namespace IKIGAI::ECS {
class SurfaceComponent : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type = {};
		SURFACE::Settings Surface = {};
		template <class C> static void serde(C& context, Descriptor& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "SurfaceComponentType")
				.field(&Descriptor::Surface, "Surface", default_ { SURFACE::Settings {} });
		}
	};
	explicit SurfaceComponent(UTILS::Ref<Object> object);
	SurfaceComponent(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	Descriptor getDescriptor() const;
	void apply(const SURFACE::Settings& settings);
	void addStroke(unsigned editLayerId, const SURFACE::BrushStroke& stroke);
	void setEditLayers(const std::vector<SURFACE::EditLayer>& layers);
	bool undoEdit();
	bool redoEdit();
	const SURFACE::Settings& settings() const;
	SURFACE::Terrain& terrain();
	const std::vector<std::shared_ptr<RENDER::MaterialInterface>>& materials() const;
	uint64_t revision() const;
	uint64_t requestedRevision() const;
	void pollUpdate();
	void publishUpdate(bool dependenciesReady);
	void failUpdate(std::string error);
	bool updatePending() const;
	const std::string& updateError() const;
	SURFACE::FrameState frame = {};
	static auto GetMembers() {
		return std::tuple {};
	}

private:
	struct SurfaceUpdate;
	std::shared_ptr<SurfaceUpdate> mUpdate = {};
	SURFACE::Settings mSettings = {};
	uint64_t mRequestedRevision = 0;
	std::string mUpdateError = {};
	std::shared_ptr<SURFACE::Terrain> mTerrain = {};
	std::vector<std::shared_ptr<RENDER::MaterialInterface>> mMaterials = {};
	uint64_t mRevision = 0;
	std::vector<std::vector<SURFACE::EditLayer>> mUndoEdits = {};
	std::vector<std::vector<SURFACE::EditLayer>> mRedoEdits = {};
};
template <> inline std::string GetType<SurfaceComponent>() {
	return "class IKIGAI::ECS::SurfaceComponent";
}
template <> inline std::string GetComponentName<SurfaceComponent>() {
	return "SurfaceComponent";
}
}
