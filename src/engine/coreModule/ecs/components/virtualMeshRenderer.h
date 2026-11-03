#pragma once

#include "component.h"
#include "mathModule/math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace IKIGAI::RENDER {
class MaterialInterface;
class ModelInterface;
}

namespace IKIGAI::VG {
class VirtualMeshAsset;
}

namespace IKIGAI::ECS {
// Debug colouring of the virtual geometry G-buffer pass (written to Albedo).
enum class VirtualMeshDebugMode { NONE = 0, CLUSTER = 1, LOD = 2, GROUP = 3 };

// Static opaque mesh drawn from a cooked .vmesh by the virtual geometry renderer: clusters with an error-bounded
// LOD DAG, selected per frame by screen-space error and drawn with indirect draws into the G-buffer. Backends or
// pipelines without that path, shadow passes and ray tracing draw the fallback mesh through the regular path.
class VirtualMeshRenderer : public ComponentBase {
public:
	struct Descriptor : ComponentBase::Descriptor {
		std::string Type = {};
		// Cooked .vmesh file (virtual file system path).
		std::string Path = {};
		// One material per .vmesh material slot (importer material order, as MaterialRenderer).
		std::vector<std::string> Materials = {};
		// Screen-space error threshold in pixels (0 draws the original level-0 triangles).
		float ErrorThreshold = 1.0f;
		VirtualMeshDebugMode DebugMode = VirtualMeshDebugMode::NONE;
		// Optional regular model drawn when the .vmesh cannot be read.
		std::string FallbackModel = {};

		template <class C> static void serde(C& context, Descriptor& value) {
			using namespace serde::attribute;
			serde::serde_struct(context, value)
				.field(&Descriptor::Type, "VirtualMeshRendererType")
				.field(&Descriptor::Path, "Path")
				.field(&Descriptor::Materials, "Materials", default_ { std::vector<std::string> {} })
				.field(&Descriptor::ErrorThreshold, "ErrorThreshold", default_ { 1.0f })
				.field(&Descriptor::DebugMode, "DebugMode", default_ { VirtualMeshDebugMode::NONE })
				.field(&Descriptor::FallbackModel, "FallbackModel", default_ { std::string {} });
		}
	};

	explicit VirtualMeshRenderer(UTILS::Ref<Object> object);
	VirtualMeshRenderer(UTILS::Ref<Object> object, const Descriptor& descriptor);
	VirtualMeshRenderer(UTILS::Ref<Object> object, const ComponentBase::Descriptor& descriptor);
	~VirtualMeshRenderer() override;

	Descriptor getDescriptor() const;

	// Loads the .vmesh through VG::VirtualMeshLoader. A missing or invalid file leaves no asset, records the error
	// and uses FallbackModel when one is configured.
	void setPath(const std::string& path);
	const std::string& getPath() const;
	const std::shared_ptr<VG::VirtualMeshAsset>& getAsset() const;
	const std::string& getLoadError() const;

	void setMaterials(const std::vector<std::string>& paths);
	void setMaterial(uint32_t slot, std::shared_ptr<RENDER::MaterialInterface> material);
	// Material of a slot; slots without their own material use slot 0 (as the regular drawable collection).
	std::shared_ptr<RENDER::MaterialInterface> getMaterial(uint32_t slot) const;
	const std::vector<std::shared_ptr<RENDER::MaterialInterface>>& getMaterials() const;

	void setErrorThreshold(float pixels);
	float getErrorThreshold() const;
	void setDebugMode(VirtualMeshDebugMode mode);
	VirtualMeshDebugMode getDebugMode() const;

	// Model for the regular path: the asset fallback mesh, or the configured FallbackModel without an asset.
	std::shared_ptr<RENDER::ModelInterface> getFallbackModel() const;

	// Motion-vector history, render thread only. Returns the previous world matrix for this frame (the current
	// one after a gap or on the first frame) and records the current matrix. Repeated calls in one frame agree.
	MATH::Matrix4f advanceHistory(const MATH::Matrix4f& world, uint64_t frame);

	static auto GetMembers() {
		return std::tuple {};
	}

private:
	std::string mPath = {};
	std::vector<std::string> mMaterialPaths = {};
	std::vector<std::shared_ptr<RENDER::MaterialInterface>> mMaterials = {};
	float mErrorThreshold = 1.0f;
	VirtualMeshDebugMode mDebugMode = VirtualMeshDebugMode::NONE;
	std::string mFallbackModelPath = {};
	std::shared_ptr<VG::VirtualMeshAsset> mAsset = {};
	std::shared_ptr<RENDER::ModelInterface> mFallbackModel = {};
	std::string mLoadError = {};
	MATH::Matrix4f mHistoryWorld = MATH::Matrix4f::Identity;
	MATH::Matrix4f mHistoryPreviousWorld = MATH::Matrix4f::Identity;
	uint64_t mHistoryFrame = 0;
	bool mHistoryValid = false;
};

template <> inline std::string GetType<VirtualMeshRenderer>() {
	return "class IKIGAI::ECS::VirtualMeshRenderer";
}

template <> inline std::string GetComponentName<VirtualMeshRenderer>() {
	return "VirtualMeshRenderer";
}
}
