#pragma once

#include <functional>
#include <string>
#include <spine/spine.h>
//#include "GLBatchRender.h"

#ifdef OPENGL_BACKEND

namespace IKIGAI
{
	namespace RENDER
	{
		class TextureGl;
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
        unsigned int blendSrc = 0;
        unsigned int blendDst = 0;
        RENDER::TextureGl* texture;
    };
struct GLBlendMode {
    GLBlendMode() = default;
    GLBlendMode(unsigned int src, unsigned int dst) : src(src), dst(dst) {}

    unsigned int src;
    unsigned int dst;
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

        ~SpineController() {
        }

        void setSkin(const std::string& name = "default");
        bool setAnimation(const std::string& name, bool loop = false, int trackIndex = 0, 
            std::function<void(spine::AnimationState* state, spine::EventType type, spine::TrackEntry* entry, spine::Event* event)> cb = [](spine::AnimationState* state, spine::EventType type, spine::TrackEntry* entry, spine::Event* event){});
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
    //private:
        static spine::SkeletonData* spineReadSkeletonJsonData(const spine::String& filename,
            spine::Atlas* atlas,
            float scale);

    //private:
        bool isPlaying = false;
        std::string path;
        std::string currentAnimation;
        std::string currentSkin = "default";
        float timeScale = 1.0f;
        //std::function<void(spine::AnimationState*, spine::EventType, spine::TrackEntry*, spine::Event*)> callBack;


        SpineTextureLoader* _textureLoader = nullptr;
        spine::SkeletonData* _skeletonData = nullptr;
        spine::Atlas* _atlas = nullptr;
        SkeletonDrawable* _drawable = nullptr;
    };


class SkeletonDrawable {
public:
    SkeletonDrawable(spine::SkeletonData *skeleton, spine::AnimationStateData *stateData = nullptr);
    ~SkeletonDrawable();

    void update(float deltaTime);
    void draw();

    void setUsePremultipliedAlpha(bool usePMA) {
        usePremultipliedAlpha = usePMA;
    };
    bool getUsePremultipliedAlpha() {
        return usePremultipliedAlpha;
    };

    spine::Skeleton *getSkeleton() const {
        return skeleton;
    }
    void setSkeleton(spine::Skeleton *sk) {
        skeleton = sk;
    }
    float getTimeScale() const {
        return timeScale;
    }
    void setTimeScale(float scale) {
        timeScale = scale;
    }

    spine::AnimationState *getState() const {
        
        return state;
    }
    void setState(spine::AnimationState *s) {
        state = s;
    }


//private:
    void drawOpengl();

//private:
    mutable bool ownsAnimationStateData;
    mutable spine::Vector<float> worldVertices;
    mutable spine::Vector<float> tempUvs;
    mutable spine::Vector<spine::Color> tempColors;
    mutable spine::Vector<unsigned short> quadIndices;
    mutable spine::SkeletonClipping clipper;
    mutable bool usePremultipliedAlpha;

    spine::Skeleton *skeleton;
    spine::AnimationState *state;
    float timeScale;
    std::vector<SpineVertex> vertexArray;

    SpineRenderState _states;
    GLBlendMode _blendMode;
    //SpineRender::GLBatchRender *_render;
};

class SpineTextureLoader : public spine::TextureLoader {
public:
    SpineTextureLoader() = default;
    void load(spine::AtlasPage &page, const spine::String &path) override;
    void unload(void *texture) override;
};

}
#endif
