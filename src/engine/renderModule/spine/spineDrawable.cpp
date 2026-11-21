#include "spineDrawable.h"

#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/render.h"
#include "utilsModule/serviceManager.h"
#include <map>
#include <utility>

using namespace IKIGAI;
using namespace spine;
using namespace IKIGAI::RENDER::SPINE;

#ifndef SPINE_MESH_VERTEX_COUNT_MAX
#define SPINE_MESH_VERTEX_COUNT_MAX 1000
#endif

SpineBlendMode blend_normal = SpineBlendMode(RENDER::BlendMode::SRC_ALPHA, RENDER::BlendMode::ONE_MINUS_SRC_ALPHA);
SpineBlendMode blend_additive = SpineBlendMode(RENDER::BlendMode::SRC_ALPHA, RENDER::BlendMode::ONE);
SpineBlendMode blend_multiply = SpineBlendMode(RENDER::BlendMode::DST_COLOR, RENDER::BlendMode::ONE_MINUS_SRC_ALPHA);
SpineBlendMode blend_screen = SpineBlendMode(RENDER::BlendMode::ONE, RENDER::BlendMode::ONE_MINUS_SRC_COLOR);

SpineBlendMode blend_normalPma = SpineBlendMode(RENDER::BlendMode::ONE, RENDER::BlendMode::ONE_MINUS_SRC_ALPHA);
SpineBlendMode blend_additivePma = SpineBlendMode(RENDER::BlendMode::ONE, RENDER::BlendMode::ONE);
SpineBlendMode blend_multiplyPma = SpineBlendMode(RENDER::BlendMode::DST_COLOR, RENDER::BlendMode::ONE_MINUS_SRC_ALPHA);
SpineBlendMode blend_screenPma = SpineBlendMode(RENDER::BlendMode::ONE, RENDER::BlendMode::ONE_MINUS_SRC_COLOR);

spine::SkeletonData* SpineController::spineReadSkeletonJsonData(const spine::String& filename, spine::Atlas* atlas, float scale) {
	spine::SkeletonJson json(atlas);
	json.setScale(scale);
	auto skeletonData = json.readSkeletonDataFile(filename);
	if (!skeletonData) {
		// LOG_ERROR("readSkeletonDataFile failed: %s\n", json.getError().buffer());
		return nullptr;
	}
	return skeletonData;
}

SpineController::SpineController(const std::string& pathSkel, const std::string& pathAtlas, float scale)
	: path(pathSkel) {
	create(pathSkel, pathAtlas, scale);
}

SpineController::~SpineController() {
	spineDestroy();
}

void SpineController::setSkin(const std::string& name) {
	spine::Skeleton* skeleton = _drawable->getSkeleton();
	skeleton->setSkin(name.c_str());
	skeleton->updateWorldTransform(spine::Physics_Update);
	currentSkin = name;
}
bool SpineController::setAnimation(const std::string& name, bool loop, int trackIndex,
	std::function<void(AnimationState* state, EventType type, TrackEntry* entry, Event* event)> cb) {

	if (!_drawable) {
		return false;
	}
	spine::Animation* animation = _skeletonData->findAnimation(name.c_str());
	if (animation == nullptr) {
		// LOG_ERROR("Failed to find animation: %s", animationName);
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
		// LOG_ERROR("Failed to find animation: %s", animationName);
		return false;
	}
	spine::Animation* animation2 = _skeletonData->findAnimation(name2.c_str());
	if (animation2 == nullptr) {
		// LOG_ERROR("Failed to find animation: %s", animationName);
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

void SpineController::setPMA(bool use) {
	_drawable->setUsePremultipliedAlpha(use);
}

void SpineController::setTimeScale(float scale) {
	timeScale = scale;
	_drawable->setTimeScale(timeScale);
}

bool SpineController::create(const std::string& pathSkel, const std::string& pathAtlas, float scale) {
	if (pathSkel.empty() || pathAtlas.empty()) {
		return false;
	}
	auto textureLoader = std::make_unique<SpineTextureLoader>();
	auto atlas = std::make_unique<spine::Atlas>(pathAtlas.c_str(), textureLoader.get());
	if (atlas->getPages().size() == 0) {
		// LOG_ERROR("Failed to load atlas");
		return false;
	}

	std::unique_ptr<spine::SkeletonData> skeletonData(spineReadSkeletonJsonData(pathSkel.c_str(), atlas.get(), scale));
	if (!skeletonData) {
		return false;
	}

	auto drawable = std::make_unique<SkeletonDrawable>(skeletonData.get());
	drawable->setTimeScale(timeScale);
	drawable->setUsePremultipliedAlpha(true);

	spine::Skeleton* skeleton = drawable->getSkeleton();
	skeleton->setPosition(0.0f, 0.0f);
	skeleton->setSkin("default");
	skeleton->updateWorldTransform(spine::Physics_Update);

	spineDestroy();
	_textureLoader = textureLoader.release();
	_atlas = atlas.release();
	_skeletonData = skeletonData.release();
	_drawable = drawable.release();
	return true;
}

void SpineController::spineDraw(float dt) {
	if (_drawable) {
		_drawable->update(dt);
		_drawable->draw();
	}
}

void SpineController::spineDestroy() {
	delete std::exchange(_drawable, nullptr);
	delete std::exchange(_skeletonData, nullptr);
	delete std::exchange(_atlas, nullptr);
	delete std::exchange(_textureLoader, nullptr);
	isPlaying = false;
}

void SpineController::stop() {
	_drawable->getState()->clearTracks();
	isPlaying = false;
}

void SpineController::update(float dt) {
	if (!isPlaying) {
		return;
	}
	if (_drawable) {
		_drawable->update(dt);
	}
}

SkeletonDrawable::SkeletonDrawable(spine::SkeletonData* skeletonData, spine::AnimationStateData* stateData)
	: timeScale(1)
	, vertexArray()
	, _blendMode(blend_normal)
	, worldVertices()
	, clipper() {
	// vertexArray.reserve(skeletonData->getBones().size() * 4, SpineVertex());

	spine::Bone::setYDown(true);
	worldVertices.ensureCapacity(SPINE_MESH_VERTEX_COUNT_MAX);
	skeleton = new (__FILE__, __LINE__) spine::Skeleton(skeletonData);
	tempUvs.ensureCapacity(16);
	tempColors.ensureCapacity(16);

	ownsAnimationStateData = stateData == 0;
	if (ownsAnimationStateData)
		stateData = new (__FILE__, __LINE__) AnimationStateData(skeletonData);

	state = new (__FILE__, __LINE__) AnimationState(stateData);

	quadIndices.add(0);
	quadIndices.add(1);
	quadIndices.add(2);
	quadIndices.add(2);
	quadIndices.add(3);
	quadIndices.add(0);
}

SkeletonDrawable::~SkeletonDrawable() {
	vertexArray.clear();
	if (ownsAnimationStateData)
		delete state->getData();
	delete state;
	delete skeleton;
}

void SkeletonDrawable::update(float deltaTime) {
	// skeleton->update(deltaTime);
	state->update(deltaTime * timeScale);
	state->apply(*skeleton);
	skeleton->updateWorldTransform(spine::Physics_Update);
}

void SkeletonDrawable::draw() {
	vertexArray.clear();

	// Early out if skeleton is invisible
	if (skeleton->getColor().a == 0)
		return;

	SpineVertex vertex;
	RENDER::TextureInterface* texture = nullptr;
	for (unsigned i = 0; i < skeleton->getSlots().size(); ++i) {
		Slot& slot = *skeleton->getDrawOrder()[i];
		Attachment* attachment = slot.getAttachment();
		if (!attachment)
			continue;

		// Early out if the slot color is 0 or the bone is not active
		if (slot.getColor().a == 0 || !slot.getBone().isActive()) {
			clipper.clipEnd(slot);
			continue;
		}

		Vector<float>* vertices = &worldVertices;
		int verticesCount = 0;
		Vector<float>* uvs = nullptr;
		Vector<unsigned short>* indices = nullptr;
		int indicesCount = 0;
		spine::Color* attachmentColor;

		if (attachment->getRTTI().isExactly(RegionAttachment::rtti)) {
			auto* regionAttachment = (RegionAttachment*)attachment;
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
			texture = (RENDER::TextureInterface*)((AtlasRegion*)regionAttachment->getRegion())->page->texture;
		}
		else if (attachment->getRTTI().isExactly(MeshAttachment::rtti)) {
			auto* mesh = (MeshAttachment*)attachment;
			attachmentColor = &mesh->getColor();

			// Early out if the slot color is 0
			if (attachmentColor->a == 0) {
				clipper.clipEnd(slot);
				continue;
			}

			worldVertices.setSize(mesh->getWorldVerticesLength(), 0);
			texture = (RENDER::TextureInterface*)((AtlasRegion*)mesh->getRegion())->page->texture;
			mesh->computeWorldVertices(slot, 0, mesh->getWorldVerticesLength(), worldVertices, 0, 2);
			verticesCount = mesh->getWorldVerticesLength() >> 1;
			uvs = &mesh->getUVs();
			indices = &mesh->getTriangles();
			indicesCount = mesh->getTriangles().size();
		}
		else if (attachment->getRTTI().isExactly(ClippingAttachment::rtti)) {
			auto* clip = (ClippingAttachment*)slot.getAttachment();
			clipper.clipStart(slot, clip);
			continue;
		}
		else
			continue;

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
		}
		else {
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
			vertex.y = (*vertices)[index + 1];
			vertex.u = (*uvs)[index];
			vertex.v = 1.0f - (*uvs)[index + 1];
			vertexArray.push_back(vertex);
		}
		drawOpengl();
		vertexArray.clear();
		clipper.clipEnd(slot);
	}
	drawOpengl();
	clipper.clipEnd();
}
void SkeletonDrawable::drawOpengl() {
	if (submit && !vertexArray.empty())
		submit(vertexArray, _states);
}

std::map<RENDER::TextureInterface*, std::shared_ptr<RENDER::TextureInterface>> textures;

void SpineTextureLoader::load(AtlasPage& page, const String& path) {
	auto pathStr = std::string(path.buffer(), path.length());
	auto texture = RESOURCES::ServiceManager::Get<RENDER::Renderer>().createTexture(pathStr, true);
	if (texture) {
		// texture->textureId = texture->textureId;
		//
		// texture->minFilter = cvtTextureFilter(page.minFilter);
		// texture->magFilter = cvtTextureFilter(page.magFilter);
		// texture->uWrap = cvtTextureWrap(page.uWrap);
		// texture->vWrap = cvtTextureWrap(page.vWrap);

		page.texture = texture.get();
		page.width = texture->getWidth();
		page.height = texture->getHeight();

		textures[texture.get()] = texture;
	}
	else {
		// LOG_ERROR("load texture failed: ", path.buffer());
	}
}

void SpineTextureLoader::unload(void* texture) {
	if (!texture)
		return;
	auto* tex = (RENDER::TextureInterface*)texture;

	textures.erase(tex);
	// SpineRender::GLBatchRender::releaseTexture(tex);
	// SAFE_DELETE(tex)
}

std::shared_ptr<RENDER::TextureInterface> SpineTextureLoader::getTexture(RENDER::TextureInterface* texture) {
	const auto found = textures.find(texture);
	return found == textures.end() ? nullptr : found->second;
}
