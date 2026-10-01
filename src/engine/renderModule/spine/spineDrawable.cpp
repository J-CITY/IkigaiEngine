#include "spineDrawable.h"

#include "utilsModule/log/loggerDefine.h"
#include "renderModule/backends/interface/driverInterface.h"
#ifdef OPENGL_BACKEND
#include <fstream>
#include <map>
#include <coreModule/graphicsWrapper.hpp>

#include "renderModule/backends/gl/textureGl.h"
#include <utilsModule/pathGetter.h>
#include <spine/TextureLoader.h>

#include "utilsModule/format.h"
using namespace IKIGAI;
using namespace spine;
using namespace IKIGAI::RENDER::SPINE;


#ifndef SPINE_MESH_VERTEX_COUNT_MAX
#define SPINE_MESH_VERTEX_COUNT_MAX 1000
#endif

GLBlendMode blend_normal = GLBlendMode(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
GLBlendMode blend_additive = GLBlendMode(GL_SRC_ALPHA, GL_ONE);
GLBlendMode blend_multiply = GLBlendMode(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
GLBlendMode blend_screen = GLBlendMode(GL_ONE, GL_ONE_MINUS_SRC_COLOR);

GLBlendMode blend_normalPma = GLBlendMode(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
GLBlendMode blend_additivePma = GLBlendMode(GL_ONE, GL_ONE);
GLBlendMode blend_multiplyPma = GLBlendMode(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
GLBlendMode blend_screenPma = GLBlendMode(GL_ONE, GL_ONE_MINUS_SRC_COLOR);


spine::SkeletonData* SpineController::spineReadSkeletonJsonData(const spine::String& filename,
    spine::Atlas* atlas,
    float scale) {
    spine::SkeletonJson json(atlas);
    json.setScale(scale);
    auto skeletonData = json.readSkeletonDataFile(filename);
    if (!skeletonData) {
        //LOG_ERROR("readSkeletonDataFile failed: %s\n", json.getError().buffer());
        return nullptr;
    }
    return skeletonData;
}

SpineController::SpineController(const std::string& pathSkel, const std::string& pathAtlas, float scale): path(path)
{
	create(UTILS::GetRealPath(pathSkel), UTILS::GetRealPath(pathAtlas), scale);
}

void SpineController::setSkin(const std::string& name)
{
    spine::Skeleton* skeleton = _drawable->getSkeleton();
    skeleton->setSkin(name.c_str());
    skeleton->updateWorldTransform(spine::Physics_Update);
    currentSkin = name;
}
bool SpineController::setAnimation(const std::string& name, bool loop, int trackIndex,
    std::function<void(AnimationState* state, EventType type, TrackEntry* entry, Event* event)> cb) {

    spine::Animation* animation = _skeletonData->findAnimation(name.c_str());
    if (animation == nullptr) {
        //LOG_ERROR("Failed to find animation: %s", animationName);
        return false;
    }
    auto entry = _drawable->getState()->setAnimation(trackIndex, animation, loop);
    if (entry) {
        entry->setListener(cb);
    }
    currentAnimation = name;
    isPlaying = true;
    return true;
}

bool SpineController::setMixForAnimations(const std::string& name1, const std::string& name2, float mix) {
    spine::Animation* animation1 = _skeletonData->findAnimation(name1.c_str());
    if (animation1 == nullptr) {
        //LOG_ERROR("Failed to find animation: %s", animationName);
        return false;
    }
    spine::Animation* animation2 = _skeletonData->findAnimation(name2.c_str());
    if (animation2 == nullptr) {
        //LOG_ERROR("Failed to find animation: %s", animationName);
        return false;
    }

    _drawable->getState()->getData()->setMix(name1.c_str(), name2.c_str(), mix);
    return true;
}
void SpineController::setDefaultMix(float mix) {
    _drawable->getState()->getData()->setDefaultMix(mix);
}

bool SpineController::hasAnimation(const std::string& name) {
    spine::Animation* animation = _skeletonData->findAnimation(name.c_str());
    if (animation == nullptr) {
        return false;
    }
    return true;
}

float SpineController::getAnimationDuration(const std::string& name) const {
    spine::Animation* animation = _skeletonData->findAnimation(name.c_str());
    if (animation == nullptr) {
        return 0.0f;
    }
    return animation->getDuration();
}


void SpineController::setPos(float x, float y) {
    spine::Skeleton* skeleton = _drawable->getSkeleton();
    skeleton->setPosition(x, y);
    skeleton->updateWorldTransform(spine::Physics_Update);
}

void SpineController::setPMA(bool use)
{
    _drawable->setUsePremultipliedAlpha(use);
}

void SpineController::setTimeScale(float scale) {
    timeScale = scale;
    _drawable->setTimeScale(timeScale);
}

bool SpineController::create(const std::string& pathSkel, const std::string& pathAtlas, float scale) {
    if (RENDER::DriverInterface::settings.backend != RENDER::RenderSettings::Backend::OPENGL) {
        LOG_ERROR << "Spine is currently supported only with the OpenGL backend";
        return false;
    }
    _textureLoader = new SpineTextureLoader();
    _atlas = new spine::Atlas(pathAtlas.c_str(), _textureLoader);
    if (_atlas->getPages().size() == 0) {
        //LOG_ERROR("Failed to load atlas");
        return false;
    }

    _skeletonData = spineReadSkeletonJsonData(pathSkel.c_str(), _atlas, scale);
    if (_skeletonData == nullptr) {
        return false;
    }

    _drawable = new SkeletonDrawable(_skeletonData);
    _drawable->setTimeScale(timeScale);
    _drawable->setUsePremultipliedAlpha(true);

    spine::Skeleton* skeleton = _drawable->getSkeleton();
    skeleton->setPosition(0.0f, 0.0f);
    skeleton->setSkin("default");
    skeleton->updateWorldTransform(spine::Physics_Update);

    return true;
}

void SpineController::spineDraw(float dt) {
    if (_drawable) {
        _drawable->update(dt);
        _drawable->draw();
    }
}

void SpineController::spineDestroy() {
    delete _skeletonData;
    delete _atlas;
    delete _drawable;
    delete _textureLoader;
}

void SpineController::stop()
{
    _drawable->getState()->clearTracks();
	isPlaying = false;
}

void SpineController::update(float dt)
{
    if (!isPlaying)
    {
        return;
    }
    if (_drawable) {
        _drawable->update(dt);
    }
}


SkeletonDrawable::SkeletonDrawable(spine::SkeletonData *skeletonData, spine::AnimationStateData *stateData) :
    timeScale(1),
    vertexArray(),
    _blendMode(blend_normal),
     worldVertices(), clipper() {
    //vertexArray.reserve(skeletonData->getBones().size() * 4, SpineVertex());

    spine::Bone::setYDown(true);
    worldVertices.ensureCapacity(SPINE_MESH_VERTEX_COUNT_MAX);
    skeleton = new(__FILE__, __LINE__) spine::Skeleton(skeletonData);
    tempUvs.ensureCapacity(16);
    tempColors.ensureCapacity(16);

    ownsAnimationStateData = stateData == 0;
    if (ownsAnimationStateData) 
        stateData = new(__FILE__, __LINE__) AnimationStateData(skeletonData);

    state = new(__FILE__, __LINE__) AnimationState(stateData);

    quadIndices.add(0);
    quadIndices.add(1);
    quadIndices.add(2);
    quadIndices.add(2);
    quadIndices.add(3);
    quadIndices.add(0);
}

SkeletonDrawable::~SkeletonDrawable() {
    vertexArray.clear();
    if (ownsAnimationStateData) delete state->getData();
    delete state;
    delete skeleton;
}

void SkeletonDrawable::update(float deltaTime) {
    //skeleton->update(deltaTime);
    state->update(deltaTime * timeScale);
    state->apply(*skeleton);
    skeleton->updateWorldTransform(spine::Physics_Update);
}

void SkeletonDrawable::draw() {
    vertexArray.clear();

    // Early out if skeleton is invisible
    if (skeleton->getColor().a == 0) return;

	SpineVertex vertex;
    RENDER::TextureGl* texture = nullptr;
    for (unsigned i = 0; i < skeleton->getSlots().size(); ++i) {
        Slot &slot = *skeleton->getDrawOrder()[i];
        Attachment *attachment = slot.getAttachment();
        if (!attachment) continue;

        // Early out if the slot color is 0 or the bone is not active
        if (slot.getColor().a == 0 || !slot.getBone().isActive()) {
            clipper.clipEnd(slot);
            continue;
        }

        Vector<float> *vertices = &worldVertices;
        int verticesCount = 0;
        Vector<float> *uvs = nullptr;
        Vector<unsigned short> *indices = nullptr;
        int indicesCount = 0;
        spine::Color *attachmentColor;

        if (attachment->getRTTI().isExactly(RegionAttachment::rtti)) {
            auto *regionAttachment = (RegionAttachment *) attachment;
            attachmentColor = &regionAttachment->getColor();

            // Early out if the slot color is 0
            if (attachmentColor->a == 0) {
                clipper.clipEnd(slot);
                continue;
            }

            worldVertices.setSize(8, 0);
            regionAttachment->computeWorldVertices(slot, worldVertices, 0, 2);
            verticesCount = 4;
            uvs = &regionAttachment->getUVs();
            indices = &quadIndices;
            indicesCount = 6;
            texture = (RENDER::TextureGl*) ((AtlasRegion *) regionAttachment->getRegion())->page->texture;

        } else if (attachment->getRTTI().isExactly(MeshAttachment::rtti)) {
            auto *mesh = (MeshAttachment *) attachment;
            attachmentColor = &mesh->getColor();

            // Early out if the slot color is 0
            if (attachmentColor->a == 0) {
                clipper.clipEnd(slot);
                continue;
            }

            worldVertices.setSize(mesh->getWorldVerticesLength(), 0);
            texture = (RENDER::TextureGl*) ((AtlasRegion *) mesh->getRegion())->page->texture;
            mesh->computeWorldVertices(slot, 0, mesh->getWorldVerticesLength(), worldVertices, 0, 2);
            verticesCount = mesh->getWorldVerticesLength() >> 1;
            uvs = &mesh->getUVs();
            indices = &mesh->getTriangles();
            indicesCount = mesh->getTriangles().size();

        } else if (attachment->getRTTI().isExactly(ClippingAttachment::rtti)) {
            auto *clip = (ClippingAttachment *) slot.getAttachment();
            clipper.clipStart(slot, clip);
            continue;
        } else continue;

        float r = skeleton->getColor().r * slot.getColor().r * attachmentColor->r;
        float g = skeleton->getColor().g * slot.getColor().g * attachmentColor->g;
        float b = skeleton->getColor().b * slot.getColor().b * attachmentColor->b;
        float a = skeleton->getColor().a * slot.getColor().a * attachmentColor->a;
        vertex.r = r;
        vertex.g = g;
        vertex.b = b;
        vertex.a = a;

        spine::Color light;
        light.r = r;
        light.g = g;
        light.b = b;
        light.a = a;

        if (!usePremultipliedAlpha) {
            switch (slot.getData().getBlendMode()) {
                case BlendMode_Normal:
                    _blendMode = blend_normal;
                    break;
                case BlendMode_Additive:
                    _blendMode = blend_additive;
                    break;
                case BlendMode_Multiply:
                    _blendMode = blend_multiply;
                    break;
                case BlendMode_Screen:
                    _blendMode = blend_screen;
                    break;
                default:
                    _blendMode = blend_normal;
            }
        } else {
            switch (slot.getData().getBlendMode()) {
                case BlendMode_Normal:
                    _blendMode = blend_normalPma;
                    break;
                case BlendMode_Additive:
                    _blendMode = blend_additivePma;
                    break;
                case BlendMode_Multiply:
                    _blendMode = blend_multiplyPma;
                    break;
                case BlendMode_Screen:
                    _blendMode = blend_screenPma;
                    break;
                default:
                    _blendMode = blend_normalPma;
            }
        }

        _states.texture = texture;
        _states.blendSrc = _blendMode.src;
        _states.blendDst = _blendMode.dst;

        if (clipper.isClipping()) {
            clipper.clipTriangles(worldVertices, *indices, *uvs, 2);
            vertices = &clipper.getClippedVertices();
            verticesCount = clipper.getClippedVertices().size() >> 1;
            uvs = &clipper.getClippedUVs();
            indices = &clipper.getClippedTriangles();
            indicesCount = clipper.getClippedTriangles().size();
        }

        for (int ii = 0; ii < indicesCount; ++ii) {
            int index = (*indices)[ii] * 2;
            vertex.x = (*vertices)[index];
            vertex.y = (*vertices)[index + 1] + 500;
            vertex.u = (*uvs)[index];
            vertex.v = 1.0f - (*uvs)[index + 1];
            vertexArray.push_back(vertex);
        }
        clipper.clipEnd(slot);
    }
    drawOpengl();
    clipper.clipEnd();
}
void SkeletonDrawable::drawOpengl() {
    //_render->draw(vertexArray.buffer(), vertexArray.size(), &_states);
}

static unsigned int cvtTextureFilter(TextureFilter filter) {
    switch (filter) {
        case TextureFilter_Nearest: return GL_NEAREST;
        case TextureFilter_Linear: return GL_LINEAR;
        case TextureFilter_MipMapNearestNearest: return GL_NEAREST_MIPMAP_NEAREST;
        case TextureFilter_MipMapLinearNearest: return GL_LINEAR_MIPMAP_NEAREST;
        case TextureFilter_MipMapNearestLinear: return GL_NEAREST_MIPMAP_LINEAR;
        case TextureFilter_MipMapLinearLinear: return GL_LINEAR_MIPMAP_LINEAR;
        default: return GL_LINEAR;
    }

    return GL_LINEAR;
}

static unsigned int cvtTextureWrap(TextureWrap wrap) {
    switch (wrap) {
        case TextureWrap_MirroredRepeat: return GL_MIRRORED_REPEAT;
        case TextureWrap_ClampToEdge: return GL_CLAMP_TO_EDGE;
        case TextureWrap_Repeat: return GL_REPEAT;
    }

    return GL_REPEAT;
}

std::map<RENDER::TextureGl*, std::shared_ptr<RENDER::TextureGl>> textures;

void SpineTextureLoader::load(AtlasPage& page, const String& path) {
    auto pathStr = std::string(path.buffer(), path.length());
    auto texture = RENDER::TextureGl::Create(UTILS::GetRealPath(pathStr), true);
    if (texture) {
        //texture->textureId = texture->textureId;
        //
        //texture->minFilter = cvtTextureFilter(page.minFilter);
        //texture->magFilter = cvtTextureFilter(page.magFilter);
        //texture->uWrap = cvtTextureWrap(page.uWrap);
        //texture->vWrap = cvtTextureWrap(page.vWrap);

        page.texture = texture.get();
        page.width = texture->getWidth();
        page.height = texture->getHeight();


        textures[texture.get()] = texture;
    } else {
        //LOG_ERROR("load texture failed: ", path.buffer());
    }
}

void SpineTextureLoader::unload(void *texture) {
    if (!texture) return;
    auto *tex = (RENDER::TextureGl*)texture;

    textures.erase(tex);
	//SpineRender::GLBatchRender::releaseTexture(tex);
    //SAFE_DELETE(tex)
}

namespace spine
{
//TODO: load by my resource system

    class SpineLoaderExtension : public DefaultSpineExtension {
    protected:
        char* _readFile(const String& path, int* length) override {
            LOG_INFO << UTILS::format("AndroidSpineExtension _readFile: {}", path.buffer());

            auto realPath = UTILS::GetRealPath(path.buffer());
            char* readBuffer = nullptr;

            std::ifstream file(realPath, std::ios::binary);
            if (!file.is_open()) {
                LOG_ERROR << "Error opening file!";
                *length = 0;
            	return nullptr;
            }
            file.seekg(0, std::ios::end);
            size_t fileSize = file.tellg();
            file.seekg(0, std::ios::beg);

            char* buffer = new char[fileSize + 1];
            file.read(buffer, fileSize);
            file.close();
            buffer[fileSize] = 0;
            *length = static_cast<int>(fileSize);
            
            return readBuffer;
        }
    };

    SpineExtension* getDefaultExtension() {
        return new SpineLoaderExtension();
    }
}
#endif
