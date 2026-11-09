#pragma once
#include "upscalerInterface.h"
#include "textureInterface.h"
#include <atomic>

namespace IKIGAI::RENDER {
enum class FrameGenerationType { OFF, FSR, DLSS, AUTO };
struct FrameGenerationSettings {
    FrameGenerationType technology = FrameGenerationType::OFF;
    unsigned multiplier = 2; // Real + generated frames; x2 means one generated.
    bool vsync = false;
    bool operator==(const FrameGenerationSettings&) const = default;
    template<class Context> constexpr static auto serde(Context& context, FrameGenerationSettings& v) {
        using namespace serde::attribute;
        serde::serde_struct(context,v)
            .field(&FrameGenerationSettings::technology,"technology",default_{FrameGenerationType::OFF})
            .field(&FrameGenerationSettings::multiplier,"multiplier",default_{2u})
            .field(&FrameGenerationSettings::vsync,"vsync",default_{false});
    }
};
struct FrameGenerationStatus {
    bool Supported = false, Active = false;
    FrameGenerationType Technology = FrameGenerationType::OFF;
    unsigned Multiplier = 1, MaxMultiplier = 1;
    std::string Implementation, Reason;
    uint64_t RealFrames = 0, GeneratedFrames = 0, PresentedFrames = 0;
};
struct FrameGenerationInputs {
    std::shared_ptr<TextureInterface> Color, Depth, MotionVectors, UI;
    MATH::Vector2i RenderSize{}, DisplaySize{};
    MATH::Vector2f Jitter{}, PreviousJitter{};
    MATH::Matrix4f View = MATH::Matrix4f::Identity, Projection = MATH::Matrix4f::Identity;
    MATH::Matrix4f PreviousViewProjection = MATH::Matrix4f::Identity;
    MATH::Vector3f Position{}, Up{0,1,0}, Right{1,0,0}, Forward{0,0,-1};
    float Near=.1f, Far=1000.f, VerticalFov=1.f, DeltaMilliseconds=16.67f;
    uint64_t RealFrameId=0;
    bool Reset = false;
};
class UnsupportedFrameGeneration : public std::runtime_error {
public: using std::runtime_error::runtime_error;
};
class FrameGenerationInterface {
public:
	virtual ~FrameGenerationInterface() = default;
	virtual void configure(const FrameGenerationSettings&, MATH::Vector2i render, MATH::Vector2i display) = 0;
	virtual void prepare(const FrameGenerationInputs&) = 0;
	virtual void suspend() = 0;
	// Called after the rendering fence and before input textures can be reused.
	virtual void waitForPresents() = 0;
	virtual void waitForFrame(uint64_t) {
		waitForPresents();
	}
	virtual void afterPresent() = 0;
	virtual FrameGenerationStatus status() const=0;
    virtual bool composesUI() const {return false;}
};
inline void ValidateFrameGenerationInputs(const FrameGenerationInputs& f) {
    if(!f.Color||!f.Depth||!f.MotionVectors||!f.UI)
        throw UnsupportedFrameGeneration("FRAME_GENERATION requires scene, depth, motion and UI textures");
    if(f.RenderSize.x<=0||f.RenderSize.y<=0||f.DisplaySize.x<=0||f.DisplaySize.y<=0 ||
       f.Color->getWidth()!=f.DisplaySize.x||f.Color->getHeight()!=f.DisplaySize.y ||
       f.UI->getWidth()!=f.DisplaySize.x||f.UI->getHeight()!=f.DisplaySize.y ||
       f.Depth->getWidth()!=f.RenderSize.x||f.Depth->getHeight()!=f.RenderSize.y ||
       f.MotionVectors->getWidth()!=f.RenderSize.x||f.MotionVectors->getHeight()!=f.RenderSize.y)
        throw UnsupportedFrameGeneration("FRAME_GENERATION input dimensions do not match render/display sizes");
    if(f.Depth->getFormat()!=PixelFormat::DEPTH32_FLOAT||f.MotionVectors->getFormat()!=PixelFormat::RG_FLOAT ||
       f.Color->getFormat()!=PixelFormat::RGBA_INT||f.UI->getFormat()!=PixelFormat::RGBA_INT)
        throw UnsupportedFrameGeneration("FRAME_GENERATION requires SDR RGBA8 scene/UI, D32 depth and RG32F motion");
    if(!std::isfinite(f.DeltaMilliseconds)||f.DeltaMilliseconds<0||!std::isfinite(f.Near)||f.Near<=0||
       !std::isfinite(f.Far)||f.Far<=f.Near||!std::isfinite(f.VerticalFov)||f.VerticalFov<=0)
        throw UnsupportedFrameGeneration("Invalid FRAME_GENERATION camera/time parameters");
}
}
