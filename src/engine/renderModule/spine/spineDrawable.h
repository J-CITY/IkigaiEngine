#pragma once

#include "renderModule/backends/interface/renderEnums.h"
#include <functional>
#include <memory>
#include <spine/spine.h>
#include <string>
#include <vector>

// #include "GLBatchRender.h"

namespace IKIGAI {
namespace RENDER {
	class TextureInterface;
}
}

namespace IKIGAI::RENDER::SPINE {
class SkeletonDrawable;
class SpineTextureLoader;

struct SpineTexture {
	int width = 0;
	int height = 0;
	unsigned int textureId = 0;
	unsigned int minFilter = 0;
	unsigned int magFilter = 0;
	unsigned int uWrap = 0;
	unsigned int vWrap = 0;
};
struct SpineRenderState {
	BlendMode blendSrc = BlendMode::SRC_ALPHA;
	BlendMode blendDst = BlendMode::ONE_MINUS_SRC_ALPHA;
	RENDER::TextureInterface* texture = nullptr;
};
struct SpineBlendMode {
	SpineBlendMode() = default;
	SpineBlendMode(BlendMode src, BlendMode dst)
		: src(src)
		, dst(dst) { }

	BlendMode src;
	BlendMode dst;
};
struct SpineVertex {
	float x = 0.0f;
	float y = 0.0f;
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
	float a = 0.0f;
	float u = 0.0f;
	float v = 0.0f;
};

class SpineController {
public:
	SpineController() = default;
	SpineController(const std::string& pathSkel, const std::string& pathAtlas, float scale = 1.0f);

	~SpineController();
	SpineController(const SpineController&) = delete;
	SpineController& operator=(const SpineController&) = delete;

	void setSkin(const std::string& name = "default");
	bool setAnimation(
		const std::string& name, bool loop = false, int trackIndex = 0,
		std::function<void(spine::AnimationState* state, spine::EventType type, spine::TrackEntry* entry, spine::Event* event)> cb
		= [](spine::AnimationState* state, spine::EventType type, spine::TrackEntry* entry, spine::Event* event) { });
	bool setMixForAnimations(const std::string& name1, const std::string& name2, float mix);
	void setDefaultMix(float mix);
	bool hasAnimation(const std::string& name);
	float getAnimationDuration(const std::string& name) const;
	void setPos(float x, float y);
	void setPMA(bool use);
	void setTimeScale(float scale);

	bool create(const std::string& pathSkel, const std::string& pathAtlas, float timeScale = 1.0f);
	void spineDraw(float dt);
	void spineDestroy();

	void stop();
	void update(float dt);
	// private:
	static spine::SkeletonData* spineReadSkeletonJsonData(const spine::String& filename, spine::Atlas* atlas, float scale);

	// private:
	bool isPlaying = false;
	std::string path;
	std::string currentAnimation;
	std::string currentSkin = "default";
	float timeScale = 1.0f;
	// std::function<void(spine::AnimationState*, spine::EventType, spine::TrackEntry*, spine::Event*)> callBack;

	SpineTextureLoader* _textureLoader = nullptr;
	spine::SkeletonData* _skeletonData = nullptr;
	spine::Atlas* _atlas = nullptr;
	SkeletonDrawable* _drawable = nullptr;
};

class SkeletonDrawable {
public:
	SkeletonDrawable(spine::SkeletonData* skeleton, spine::AnimationStateData* stateData = nullptr);
	~SkeletonDrawable();

	void update(float deltaTime);
	void draw();

	void setUsePremultipliedAlpha(bool usePMA) {
		usePremultipliedAlpha = usePMA;
	};
	bool getUsePremultipliedAlpha() {
		return usePremultipliedAlpha;
	};

	spine::Skeleton* getSkeleton() const {
		return skeleton;
	}
	void setSkeleton(spine::Skeleton* sk) {
		skeleton = sk;
	}
	float getTimeScale() const {
		return timeScale;
	}
	void setTimeScale(float scale) {
		timeScale = scale;
	}

	spine::AnimationState* getState() const {

		return state;
	}
	void setState(spine::AnimationState* s) {
		state = s;
	}

	// private:
	void drawOpengl();

	// private:
	mutable bool ownsAnimationStateData;
	mutable spine::Vector<float> worldVertices;
	mutable spine::Vector<float> tempUvs;
	mutable spine::Vector<spine::Color> tempColors;
	mutable spine::Vector<unsigned short> quadIndices;
	mutable spine::SkeletonClipping clipper;
	mutable bool usePremultipliedAlpha;

	spine::Skeleton* skeleton;
	spine::AnimationState* state;
	float timeScale;
	std::vector<SpineVertex> vertexArray;
	std::function<void(const std::vector<SpineVertex>&, const SpineRenderState&)> submit;

	SpineRenderState _states;
	SpineBlendMode _blendMode;
	// SpineRender::GLBatchRender *_render;
};

class SpineTextureLoader : public spine::TextureLoader {
public:
	SpineTextureLoader() = default;
	void load(spine::AtlasPage& page, const spine::String& path) override;
	void unload(void* texture) override;
	static std::shared_ptr<RENDER::TextureInterface> getTexture(RENDER::TextureInterface* texture);
};

}
