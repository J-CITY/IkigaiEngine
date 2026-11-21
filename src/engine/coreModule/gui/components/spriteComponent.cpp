#include "spriteComponent.h"
#include "../../core/core.h"
#include "../../ecs/object.h"
#include "guiModule/font.h"
#include "renderModule/render.h"
#include "resourceModule/materialManager.h"
#include "resourceModule/particleManager.h"
#include "resourceModule/shaderManager.h"
#include "windowModule/window/window.h"
#include <algorithm>
#include <cmath>
#include <renderModule/gameRenderer.h>
#include <stdexcept>
#include <utilsModule/pathGetter.h>

#include "renderModule/vertex.h"
#include "resourceModule/textureManager.h"
#include "utilsModule/time/time.h"

using namespace IKIGAI;
using namespace IKIGAI::ECS;

RootGuiComponent::Descriptor RootGuiComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<RootGuiComponent>();
	return d;
}

RootGuiComponent::RootGuiComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor)
	: ComponentBase(obj) { }

RootGuiComponent::RootGuiComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
	: RootGuiComponent(obj, static_cast<const Descriptor&>(descriptor)) { }

// BATCHE END

ColorComponent::Descriptor ColorComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<ColorComponent>();
	d.Color = mColor;
	return d;
}

ColorComponent::ColorComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor)
	: ComponentBase(obj) {
	mColor = descriptor.Color;
}

ColorComponent::ColorComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
	: ColorComponent(obj, static_cast<const Descriptor&>(descriptor)) { }

SpriteComponent::SpriteComponent(Object& obj)
	: ComponentBase(obj) { }

SpriteComponent::SpriteComponent(Object& obj, const std::string& path)
	: ComponentBase(obj) {
	setTexture(path);
}

void SpriteComponent::setTexture(std::string path) {
	mExplicitAtlas = false;
	mPath = path;
	auto& loader = RESOURCES::ServiceManager::Get<RESOURCES::TextureLoader>();
	const auto resolved = loader.resolveSprite(path);
	mTexture = loader.createSprite(path);
	mTexturePiece = resolved.region;
	const auto piece = mTexture->getPiece(mTexturePiece);
	obj->getTransform()->getTransform().setLocalSize({ piece.mW, piece.mH });
}

void SpriteComponent::setTextureAtlas(std::string path) {
	mExplicitAtlas = true;
	mPath = path;
	mTexture = RESOURCES::ServiceManager::Get<RESOURCES::TextureLoader>().createAtlasFromFile(path, true);
	// obj->getTransform()->getTransform().setLocalSize(
	//	{ static_cast<RENDER::TextureGl*>(mTexture.get())->width, static_cast<RENDER::TextureGl*>(mTexture.get())->height });
}

std::string SpriteComponent::getTexture() {
	return mPath;
}

void SpriteComponent::setAtlasPiece(std::string name) {
	if (name.empty()) {
		return;
	}
	auto rect = mTexture->getPiece(name);
	obj->getTransform()->getTransform().setLocalSize({ rect.mW, rect.mH });
	mTexturePiece = name;
}

std::string SpriteComponent::getAtlasPiece() {
	return mTexturePiece;
}

SpriteComponent::Descriptor SpriteComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<SpriteComponent>();
	d.TexturePath = mExplicitAtlas ? "" : mPath;
	d.TextureAtlasPath = mExplicitAtlas ? mPath : "";
	d.TexturePiece = mTexturePiece;
	d.Is3D = mIs3D;
	return d;
}

SpriteComponent::SpriteComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor)
	: ComponentBase(obj) {
	mIs3D = descriptor.Is3D;
	if (!descriptor.TexturePath.empty()) {
		setTexture(descriptor.TexturePath);
	}
	else if (!descriptor.TextureAtlasPath.empty()) {
		setTextureAtlas(descriptor.TextureAtlasPath);
		setAtlasPiece(descriptor.TexturePiece);
	}
}

SpriteComponent::SpriteComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
	: SpriteComponent(obj, static_cast<const Descriptor&>(descriptor)) { }

void SpriteAnimateComponent::setGridSize(MATH::Vector2f sz) {
	mGridSize = sz;
}

MATH::Vector2f SpriteAnimateComponent::getGridSize() {
	return mGridSize;
}

void SpriteAnimateComponent::setFrameCount(int e) {
	mFrameCount = e;
}

int SpriteAnimateComponent::getFrameCount() const {
	return mFrameCount;
}

void SpriteAnimateComponent::updateAnim() {
	if (!mTexture || mGridSize.x <= 0 || mGridSize.y <= 0) {
		return;
	}
	mCutTime -= TIME::Timer::GetInstance().getDeltaTime().count() * mTimeScale;
	if (mCutTime < 0.0f) {
		mCutTime = mTime;
		nextFrame();
	}

	const auto region = mTexture->getPieceUV(mTexturePiece);

	const float frameWidth = 1.f / mGridSize.x;
	const float frameHeight = 1.f / mGridSize.y;

	const int row = mCurrentFreme / mGridSize.x;
	const int col = mCurrentFreme % (int)mGridSize.x;

	mUVRect.mX = region.mX + region.mW * frameWidth * col;
	mUVRect.mY = region.mY + region.mH * frameHeight * row;
	mUVRect.mW = region.mW * frameWidth;
	mUVRect.mH = region.mH * frameHeight;
}

void SpriteAnimateComponent::nextFrame() {
	if (mFrameCount == mCurrentFreme + 1) {
		mCurrentFreme = 0;
	}
	else {
		mCurrentFreme++;
	}
}

SpriteAnimateComponent::Descriptor SpriteAnimateComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<SpriteAnimateComponent>();
	d.TexturePath = mPath;
	d.TexturePiece = mTexturePiece;
	d.Is3D = mIs3D;
	d.GridSize = mGridSize;
	d.TimeScale = mTimeScale;
	return d;
}

SpriteAnimateComponent::SpriteAnimateComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor)
	: SpriteComponent(obj) {
	mIs3D = descriptor.Is3D;
	mTexturePiece = descriptor.TexturePiece;
	mGridSize = descriptor.GridSize;
	mTimeScale = descriptor.TimeScale;
	mFrameCount = descriptor.FrameCount;
	setTexture(descriptor.TexturePath);
}

SpriteAnimateComponent::SpriteAnimateComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
	: SpriteAnimateComponent(obj, static_cast<const Descriptor&>(descriptor)) { }

SpriteParticleComponent::SpriteParticleComponent(Object& obj)
	: SpriteComponent(obj) { }
SpriteParticleComponent::SpriteParticleComponent(Object& obj, const std::string& path)
	: SpriteComponent(obj, path) { }
SpriteParticleComponent::~SpriteParticleComponent() = default;
SpriteParticleComponent::SpriteParticleComponent(const SpriteParticleComponent& other)
	: SpriteComponent(other)
	, particles(other.particles)
	, freeParticlesId(other.freeParticlesId)
	, emmiters(other.emmiters)
	, mParticleAsset(other.mParticleAsset)
	, mRuntime(other.mRuntime ? std::make_unique<PARTICLES::Runtime<2>>(*other.mRuntime) : nullptr) { }
SpriteParticleComponent& SpriteParticleComponent::operator=(const SpriteParticleComponent& other) {
	if (this != &other) {
		auto runtime = other.mRuntime ? std::make_unique<PARTICLES::Runtime<2>>(*other.mRuntime) : nullptr;
		SpriteComponent::operator=(other);
		particles = other.particles;
		freeParticlesId = other.freeParticlesId;
		emmiters = other.emmiters;
		mParticleAsset = other.mParticleAsset;
		mRuntime = std::move(runtime);
	}
	return *this;
}

void SpriteParticleComponent::setEmmiters(std::vector<Emmiter> e) {
	using namespace PARTICLES;
	auto definition = std::make_shared<Definition>();
	auto uniform = [](Value minimum, Value maximum, int channels, const char* property) {
		for (int channel = 0; channel < channels; ++channel) {
			if (!std::isfinite(minimum[channel]) || !std::isfinite(maximum[channel]) || minimum[channel] > maximum[channel]) {
				throw std::invalid_argument(std::string("Invalid legacy particle range: ") + property);
			}
		}
		Source result;
		result.mode = SourceMode::Uniform;
		result.channels = channels;
		result.min = minimum;
		result.max = maximum;
		result.channel = hash(property);
		return result;
	};
	auto module = [](ModuleType type, const char* id) {
		Module result;
		result.type = type;
		result.id = hash(id);
		return result;
	};
	uint64_t capacity = 0;
	for (size_t i = 0; i < e.size(); ++i) {
		auto& emitter = e[i];
		emitter.curSpawnTime = emitter.pause;
		emitter.curParticleCount = 0;
		if (emitter.count <= 0) {
			continue;
		}
		if (emitter.spawnTime <= 0) {
			throw std::invalid_argument("Legacy particle SpawnTime must be positive; choose an explicit interval");
		}
		capacity += emitter.count;
		if (capacity > 1000000 || definition->emitters.size() >= 128) {
			throw std::invalid_argument("Legacy particle effect exceeds emitter or particle capacity");
		}
		if (!std::isfinite(emitter.spawnTime) || !std::isfinite(emitter.pause) || emitter.spawnTime < .001 || emitter.pause < 0) {
			throw std::invalid_argument("Legacy particle SpawnTime must be >= .001 and Pause >= 0");
		}
		if (emitter.spawnCount < 0 || emitter.spawnCount > 65536 || emitter.lifeTime.x <= 0) {
			throw std::invalid_argument("Legacy particles require SpawnCount in [0,65536] and a positive lifetime");
		}

		EmitterDefinition converted;
		converted.id = "legacy-" + std::to_string(i);
		converted.local = true;
		converted.capacity = static_cast<uint32_t>(emitter.count);
		converted.duration = emitter.spawnTime;
		converted.loops = 0;
		converted.delay = emitter.pause;
		Burst burst;
		burst.count = static_cast<uint32_t>(emitter.spawnCount);
		converted.bursts.push_back(burst);

		auto& renderer = converted.renderer;
		renderer.screen = !mIs3D;
		renderer.depthTest = mIs3D;
		renderer.piece = emitter.piece.empty() ? mTexturePiece : emitter.piece;
		if (mExplicitAtlas) {
			renderer.atlas = mPath;
		}
		else {
			renderer.texture = mPath;
		}

		auto shape = module(ModuleType::Shape, "shape");
		shape.shape.offset = { emitter.localPos.x, emitter.localPos.y };
		auto initialize = module(ModuleType::Initialize, "init");
		initialize.lifetime = uniform({ emitter.lifeTime.x }, { emitter.lifeTime.y }, 1, "Lifetime");
		initialize.size = uniform({ emitter.size.x, emitter.size.x }, { emitter.size.y, emitter.size.y }, 2, "Size");
		initialize.velocity.channels = 2;
		initialize.color.channels = 4;
		initialize.color.min = initialize.color.max = { emitter.color.x, emitter.color.y, emitter.color.z, emitter.color.w };
		initialize.orientation.channels = 3;
		auto launch = module(ModuleType::DirectionalVelocity, "launch");
		launch.value = uniform({ emitter.angleForce.x }, { emitter.angleForce.y }, 1, "Value");
		launch.rotation = uniform({ emitter.angle.x }, { emitter.angle.y }, 1, "Rotation");

		auto acceleration = module(ModuleType::Acceleration, "gravity");
		acceleration.value.mode = SourceMode::Uniform;
		acceleration.value.channels = 2;
		acceleration.value.channel = hash("Value");
		// Both axes use the same weight even when their gravity signs differ.
		acceleration.value.min = { emitter.gravity.x * emitter.weight.x, emitter.gravity.y * emitter.weight.x };
		acceleration.value.max = { emitter.gravity.x * emitter.weight.y, emitter.gravity.y * emitter.weight.y };
		const auto finite = [](float value) { return std::isfinite(value); };
		if (!std::all_of(shape.shape.offset.begin(), shape.shape.offset.end(), finite)
			|| !std::all_of(initialize.color.min.begin(), initialize.color.min.end(), finite)
			|| !std::all_of(acceleration.value.min.begin(), acceleration.value.min.end(), finite)
			|| !std::all_of(acceleration.value.max.begin(), acceleration.value.max.end(), finite)) {
			throw std::invalid_argument("Legacy particle position, color and acceleration must be finite");
		}
		converted.spawn.push_back(std::move(shape));
		converted.spawn.push_back(std::move(initialize));
		converted.spawn.push_back(std::move(launch));
		converted.update.push_back(std::move(acceleration));
		converted.update.push_back(module(ModuleType::IntegrateMotion, "motion"));
		definition->emitters.push_back(std::move(converted));
	}
	std::shared_ptr<RESOURCES::ParticleAsset> asset;
	std::unique_ptr<PARTICLES::Runtime<2>> runtime;
	if (!definition->emitters.empty()) {
		asset = RESOURCES::ServiceManager::Get<RESOURCES::ParticleLoader>().createInline(std::move(definition));
		runtime = std::make_unique<PARTICLES::Runtime<2>>(asset->definition, 1);
	}
	emmiters = std::move(e);
	mParticleAsset = std::move(asset);
	mRuntime = std::move(runtime);
	particles.clear();
	freeParticlesId.clear();
}
std::vector<SpriteParticleComponent::Emmiter> SpriteParticleComponent::getEmmiters() {
	return emmiters;
}
void SpriteParticleComponent::spawnParticle(Emmiter& emitter, int id) {
	if (!mRuntime) {
		return;
	}
	for (size_t i = 0; i < mRuntime->definition().emitters.size(); ++i) {
		if (mRuntime->definition().emitters[i].id == "legacy-" + std::to_string(id)) {
			mRuntime->emit(i, uint32_t(std::max(0, emitter.spawnCount)));
			return;
		}
	}
}
void SpriteParticleComponent::update() {
	tick(TIME::Timer::GetInstance().getDeltaTime().count());
}
void SpriteParticleComponent::tick(double dt) {
	if (!mRuntime) {
		return;
	}
	mRuntime->update(dt, obj->getTransform()->getTransform().getWorldMatrix());
	particles.clear();
	for (auto& e : emmiters) {
		e.curParticleCount = 0;
	}
	for (size_t i = 0; i < mRuntime->emitters().size(); ++i) {
		const int id = std::stoi(mRuntime->definition().emitters[i].id.substr(7));
		const auto& current = mRuntime->emitters()[i];
		emmiters[id].curParticleCount = int(current.particles.size());
		for (const auto& p : current.particles) {
			Particle legacy;
			legacy.pos = { p.position[0], p.position[1], 0 };
			legacy.speed = { p.velocity[0], p.velocity[1], 0 };
			legacy.life = p.lifetime - p.age;
			legacy.size = p.size[0];
			legacy.weight = 1;
			legacy.emmiterId = id;
			legacy.isAlive = true;
			particles.push_back(legacy);
		}
	}
}

SpriteParticleComponent::Descriptor SpriteParticleComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<SpriteParticleComponent>();
	d.TexturePath = mExplicitAtlas ? "" : mPath;
	d.TextureAtlasPath = mExplicitAtlas ? mPath : "";
	d.TexturePiece = mTexturePiece;
	d.Is3D = mIs3D;
	d.Emmiters = emmiters;
	return d;
}

SpriteParticleComponent::SpriteParticleComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor)
	: SpriteComponent(obj) {
	mIs3D = descriptor.Is3D;
	if (!descriptor.TexturePath.empty()) {
		setTexture(descriptor.TexturePath);
	}
	else if (!descriptor.TextureAtlasPath.empty()) {
		setTextureAtlas(descriptor.TextureAtlasPath);
		setAtlasPiece(descriptor.TexturePiece);
	}
	setEmmiters(descriptor.Emmiters);
}

SpriteParticleComponent::SpriteParticleComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
	: SpriteParticleComponent(obj, static_cast<const Descriptor&>(descriptor)) { }

LabelComponent::LabelComponent(Object& obj)
	: ComponentBase(obj) { }

LabelComponent::LabelComponent(Object& obj, std::string label, std::shared_ptr<GUI::Font> font)
	: ComponentBase(obj)
	, mFont(font) {
	mLabel = label;

	float width = 0;
	float height = 0;
	for (auto c : label) {
		GUI::Character ch = font->Characters[c];
		width += (ch.Advance >> 6);
		height = std::max(height, static_cast<float>(ch.Size.y));
	}
	obj.getTransform()->getTransform().setLocalSize({ width, height });

	// TODO: uncoment it
	// auto shader = material->getShader();
	////shader = RESOURCES::ShaderLoader::CreateFromFile("Shaders/gui/text.glsl");
	// MATHGL::Matrix4 projection = MATHGL::Matrix4::CreateOrthographic(0.0f, static_cast<float>(800), static_cast<float>(600), 0.0f, 0, 1);
	// shader->bind();
	// shader->setUniformInt("u_engine_text", 0);
	// shader->setUniformMat4("u_engine_projection", projection);

	// glGenVertexArrays(1, &VAO);
	// glGenBuffers(1, &VBO);
	// glBindVertexArray(VAO);
	// glBindBuffer(GL_ARRAY_BUFFER, VBO);
	// glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, NULL, GL_DYNAMIC_DRAW);
	// glEnableVertexAttribArray(0);
	// glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
	// glBindBuffer(GL_ARRAY_BUFFER, 0);
	// glBindVertexArray(0);
}

LabelComponent::Descriptor LabelComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<LabelComponent>();
	d.Is3D = mIs3D;
	d.Label = mLabel;
	d.Font = mFont ? mFont->path : "";
	return d;
}

LabelComponent::LabelComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor)
	: ComponentBase(obj) {
	mIs3D = descriptor.Is3D;
	mLabel = descriptor.Label;
	mFont = GUI::LoadLabelFont(descriptor.Font);
}

LabelComponent::LabelComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
	: LabelComponent(obj, static_cast<const Descriptor&>(descriptor)) { }

void SpineComponent::setSpine(SpineRefl data) {
	skelPath = data.skelPath;
	atlasPath = data.atlasPath;
	spine = std::make_shared<RENDER::SPINE::SpineController>(skelPath, atlasPath);

	spine->setAnimation("idle", true);
}

SpineRefl SpineComponent::getSpine() {
	return { skelPath, atlasPath };
}

SpineComponent::SpineComponent(Object& obj)
	: ComponentBase(obj) { }

SpineComponent::SpineComponent(Object& obj, std::string skelPath, std::string atlasPath)
	: ComponentBase(obj)
	, skelPath(skelPath)
	, atlasPath(atlasPath) {
	spine = std::make_shared<RENDER::SPINE::SpineController>(skelPath, atlasPath);
}

SpineComponent::Descriptor SpineComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<SpineComponent>();
	d.Is3D = mIs3D;
	d.SkelPath = skelPath;
	d.AtlasPath = atlasPath;
	return d;
}

SpineComponent::SpineComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor)
	: SpineComponent(obj, descriptor.SkelPath, descriptor.AtlasPath) {
	mIs3D = descriptor.Is3D;
}

SpineComponent::SpineComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
	: SpineComponent(obj, static_cast<const Descriptor&>(descriptor)) { }

// void LabelComponentGui::draw() {
//	if (!isEnabled) {
//		return;
//	}
//	glEnable(GL_CULL_FACE);
//	glEnable(GL_BLEND);
//	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//	//TODO: uncoment it
//	//auto shader = material->getShader();
//	//shader->bind();
//	//shader->setUniformVec3("textColor", {color.x, color.y, color.z});
//
//	glBindVertexArray(VAO);
//
//	glBindTexture(GL_TEXTURE_2D, font->texture);
//	//TODO: uncoment it
//	std::string::const_iterator c;
//	//shader->setUniformMat4("u_engine_model", obj.transform->globalModel);
//
//	float x = 0;//model(0, 3);
//	float y = 0;//model(1, 3);
//	for (auto c : label) {
//		Character ch = font->Characters[c];
//
//		float xpos = x + ch.Bearing.x * obj.transform->globalScale.x;
//		float ypos = y - (ch.Size.y - ch.Bearing.y) * obj.transform->globalScale.y;
//
//		float w = ch.Size.x * obj.transform->globalScale.x;
//		float h = ch.Size.y * obj.transform->globalScale.y;
//
//
//		// ��������� VBO ��� ������� �������
//		float vertices[6][4] = {
//			{ xpos,     ypos + h, ch.Start.x / 1024.0,               (ch.Start.y + ch.Size.y) / 1024.0 },
//			{ xpos + w, ypos,     (ch.Start.x + ch.Size.x) / 1024.0,  ch.Start.y / 1024.0},
//			{ xpos,     ypos,     ch.Start.x / 1024.0,               ch.Start.y / 1024.0 },
//			{ xpos,     ypos + h, ch.Start.x / 1024.0,               (ch.Start.y + ch.Size.y) / 1024.0  },
//			{ xpos + w, ypos + h, (ch.Start.x + ch.Size.x) / 1024.0, (ch.Start.y + ch.Size.y) / 1024.0 },
//			{ xpos + w, ypos,     (ch.Start.x + ch.Size.x) / 1024.0, ch.Start.y / 1024.0 }
//		};
//
//		// ������������� �������� ����� ������ ��������������
//		//glBindTexture(GL_TEXTURE_2D, ch.textureID);
//
//		// ��������� ���������� ������ VBO
//		glBindBuffer(GL_ARRAY_BUFFER, VBO);
//		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
//		glBindBuffer(GL_ARRAY_BUFFER, 0);
//
//		// �������� �������������
//		glDrawArrays(GL_TRIANGLES, 0, 6);
//
//		// ������ ���������� �������� ��� ����������� ���������� ����� (�������� ��������, ��� ������ �������� ���������� � ��������, ������������
// 1/64 �������) 		x += (ch.Advance >> 6) * obj.transform->globalScale.x; // ��������� ����� �� 6, ����� �������� �������� � �������� (2^6 = 64)
//	}
//	glBindVertexArray(0);
//	glBindTexture(GL_TEXTURE_2D, 0);
// }

InteractionComponent::InteractionComponent(Object& obj)
	: ComponentBase(obj) { }

InteractionComponent::InteractionComponent(Object& obj, float w, float h)
	: ComponentBase(obj)
	, mWidth(w)
	, mHeight(h) {
	// obj.transform->size = size;
	obj.getTransform()->getTransform().setLocalSize({ w, h });
}

InteractionComponent::Descriptor InteractionComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<InteractionComponent>();
	d.Width = mWidth;
	d.Height = mHeight;
	return d;
}

InteractionComponent::InteractionComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor)
	: InteractionComponent(obj, descriptor.Width, descriptor.Height) { }

InteractionComponent::InteractionComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
	: InteractionComponent(obj, static_cast<const Descriptor&>(descriptor)) { }

bool InteractionComponent::contains(float x, float y) {
	auto left = obj->getTransform()->getWorldPosition().x;
	auto top = obj->getTransform()->getWorldPosition().y;
	auto width = obj->getTransform()->getTransform().getLocalSize().x * obj->getTransform()->getWorldScale().x;
	auto height = obj->getTransform()->getTransform().getLocalSize().y * obj->getTransform()->getWorldScale().y;
	float minX = std::min(left, (left + width));
	float maxX = std::max(left, (left + width));
	float minY = std::min(top, (top + height));
	float maxY = std::max(top, (top + height));

	return (x >= minX) && (x < maxX) && (y >= minY) && (y < maxY);
}

ClipComponent::ClipComponent(Object& obj)
	: ComponentBase(obj) { }

// void InteractionComponent::onUpdate(float dt) {
//	auto ev = GuiEventType::NONE;
//	auto mpos = RESOURCES::ServiceManager::Get<INPUT_SYSTEM::InputManager>().getMousePosition();
//	if (contains(mpos.x, mpos.y)) {
//		if (RESOURCES::ServiceManager::Get<INPUT_SYSTEM::InputManager>().isMouseButtonPressed(INPUT_SYSTEM::EMouseButton::MOUSE_BUTTON_1)) {
//			ev = GuiEventType::PRESS;
//		}
//		else if (RESOURCES::ServiceManager::Get<INPUT_SYSTEM::InputManager>().isMouseButtonReleased(INPUT_SYSTEM::EMouseButton::MOUSE_BUTTON_1)) {
//			ev = GuiEventType::RELEASE;
//		}
//		else {
//			ev = GuiEventType::COVER;
//		}
//	}
//	else {
//		ev = GuiEventType::UNCOVER;
//	}
//
//	if (cur == ev && (cur == GuiEventType::PRESS || cur == GuiEventType::PRESS_CONTINUE)) {
//		if (onPressContinue) onPressContinue();
//	}
//
//	if (cur == ev) {
//		return;
//	}
//	cur = ev;
//	switch (ev) {
//	case GuiEventType::COVER: if (onCover) onCover();  break;
//	case GuiEventType::PRESS: if (onPress) onPress();  break;
//	case GuiEventType::RELEASE: if (onRelease) onRelease();  break;
//	case GuiEventType::UNCOVER: if (onUncover) onUncover();  break;
//	default: break;
//	}
// }
//
// void InteractionComponent::onPreUpdate(float dt) {
//	obj.transform->calculate();
//	if (obj.parent) {
//		globalX = obj.parent->childOffsetX;
//		globalY = obj.parent->childOffsetY;
//	}
// };
//
ClipComponent::ClipComponent(Object& obj, float w, float h)
	: ComponentBase(obj)
	, mWidth(w)
	, mHeight(h) {
	obj.getTransform()->getTransform().setLocalSize({ w, h });
}

ClipComponent::Descriptor ClipComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<ClipComponent>();
	d.Width = mWidth;
	d.Height = mHeight;
	return d;
}

ClipComponent::ClipComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor)
	: ClipComponent(obj, descriptor.Width, descriptor.Height) { }

ClipComponent::ClipComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
	: ClipComponent(obj, static_cast<const Descriptor&>(descriptor)) { }

ScrollComponent::ScrollComponent(Object& obj)
	: ComponentBase(obj) { }

ScrollComponent::ScrollComponent(Object& obj, float w, float h)
	: ComponentBase(obj)
	, mWidth(w)
	, mHeight(h) {
	obj.getTransform()->getTransform().setLocalSize({ w, h });
}

ScrollComponent::Descriptor ScrollComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<ScrollComponent>();
	d.Width = mWidth;
	d.Height = mHeight;
	return d;
}

ScrollComponent::ScrollComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor)
	: ScrollComponent(obj, descriptor.Width, descriptor.Height) { }

ScrollComponent::ScrollComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
	: ScrollComponent(obj, static_cast<const Descriptor&>(descriptor)) { }

LayoutComponent::Descriptor LayoutComponent::getDescriptor() const {
	Descriptor d;
	d.Type = GetType<LayoutComponent>();
	d.Layout = mType;
	d.Alignment = mAlignment;
	d.HorizontalOffset = mHorizontalOffset;
	d.VerticalOffset = mVerticalOffset;
	return d;
}

LayoutComponent::LayoutComponent(UTILS::Ref<ECS::Object> obj, const Descriptor& descriptor)
	: LayoutComponent(obj, descriptor.Layout) {
	mAlignment = descriptor.Alignment;
	mHorizontalOffset = descriptor.HorizontalOffset;
	mVerticalOffset = descriptor.VerticalOffset;
}

LayoutComponent::LayoutComponent(UTILS::Ref<ECS::Object> obj, const ComponentBase::Descriptor& descriptor)
	: LayoutComponent(obj, static_cast<const Descriptor&>(descriptor)) { }

////void ClipComponent::draw() {
////
////}
//
// void ClipComponent::onPreUpdate(float dt) {
//	obj.transform->calculate();
//	globalX = obj.transform->globalModel(0, 3);
//	globalY = obj.transform->globalModel(1, 3);
//	obj.childOffsetX = globalX;
//	obj.childOffsetY = globalY;
//	obj.transform->globalModel(0, 3) = 0;
//	obj.transform->globalModel(1, 3) = 0;
//}

// #include <rttr/registration>
////TODO: think about save texture/textureAtlas
// RTTR_REGISTRATION
//{
//	rttr::registration::class_<IKIGAI::ECS::SpriteComponent>("SpriteComponent")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("TexturePath", &IKIGAI::ECS::SpriteComponent::getTexture, &IKIGAI::ECS::SpriteComponent::setTexture)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR | MetaInfo::OPTIONAL_PARAM),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::STRING)
//	)
//	.property("TextureAtlasPath", &IKIGAI::ECS::SpriteComponent::getTexture, &IKIGAI::ECS::SpriteComponent::setTextureAtlas)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR | MetaInfo::OPTIONAL_PARAM),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::STRING)
//	)
//	.property("TexturePiece", &IKIGAI::ECS::SpriteComponent::getAtlasPiece, &IKIGAI::ECS::SpriteComponent::setAtlasPiece)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR | MetaInfo::OPTIONAL_PARAM),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::STRING)
//	);
//
//	rttr::registration::class_<IKIGAI::ECS::SpriteAnimateComponent>("SpriteAnimateComponent")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("TexturePath", &IKIGAI::ECS::SpriteAnimateComponent::getTexture, &IKIGAI::ECS::SpriteAnimateComponent::setTexture)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR | MetaInfo::OPTIONAL_PARAM),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::STRING)
//	)
//	.property("TextureAtlasPath", &IKIGAI::ECS::SpriteAnimateComponent::getTexture, &IKIGAI::ECS::SpriteAnimateComponent::setTextureAtlas)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR | MetaInfo::OPTIONAL_PARAM),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::STRING)
//	)
//	.property("TexturePiece", &IKIGAI::ECS::SpriteAnimateComponent::getAtlasPiece, &IKIGAI::ECS::SpriteAnimateComponent::setAtlasPiece)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR | MetaInfo::OPTIONAL_PARAM),
//		rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::STRING)
//	)
//	.property("GridSize", &IKIGAI::ECS::SpriteAnimateComponent::getGridSize, &IKIGAI::ECS::SpriteAnimateComponent::setGridSize)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("FrameCount", &IKIGAI::ECS::SpriteAnimateComponent::getFrameCount, &IKIGAI::ECS::SpriteAnimateComponent::setFrameCount)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("TimeScale", &IKIGAI::ECS::SpriteAnimateComponent::mTimeScale)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	);
//
//	rttr::registration::class_<IKIGAI::ECS::SpriteParticleComponent>("SpriteParticleComponent")
//		(
//			rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//			)
//		.property("TexturePath", &IKIGAI::ECS::SpriteParticleComponent::getTexture, &IKIGAI::ECS::SpriteParticleComponent::setTexture)
//		(
//			rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR | MetaInfo::OPTIONAL_PARAM),
//			rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::STRING)
//			)
//		.property("TextureAtlasPath", &IKIGAI::ECS::SpriteParticleComponent::getTexture, &IKIGAI::ECS::SpriteParticleComponent::setTextureAtlas)
//		(
//			rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR | MetaInfo::OPTIONAL_PARAM),
//			rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::STRING)
//			)
//		.property("TexturePiece", &IKIGAI::ECS::SpriteParticleComponent::getAtlasPiece, &IKIGAI::ECS::SpriteParticleComponent::setAtlasPiece)
//		(
//			rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR | MetaInfo::OPTIONAL_PARAM),
//			rttr::metadata(EditorMetaInfo::EDIT_WIDGET, EditorMetaInfo::WidgetType::STRING)
//			)
//		.property("Emmiters", &IKIGAI::ECS::SpriteParticleComponent::getEmmiters, &IKIGAI::ECS::SpriteParticleComponent::setEmmiters)
//		(
//			rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)
//		);
//
//	rttr::registration::class_<IKIGAI::ECS::SpineRefl>("SpineRefl")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("Skel", &IKIGAI::ECS::SpineRefl::skelPath)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("Atlas", &IKIGAI::ECS::SpineRefl::atlasPath)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	);
//
//	rttr::registration::class_<IKIGAI::ECS::SpineComponent>("SpineComponent")
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("Spine", &IKIGAI::ECS::SpineComponent::getSpine, &IKIGAI::ECS::SpineComponent::setSpine)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	);
//
//	rttr::registration::class_<IKIGAI::ECS::SpriteParticleComponent::Emmiter>("Emmiter")
//	(
//	rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE)
//	)
//	.property("Count", &IKIGAI::ECS::SpriteParticleComponent::Emmiter::count)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)
//	)
//	.property("LocalPos", &IKIGAI::ECS::SpriteParticleComponent::Emmiter::localPos)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)
//		)
//	.property("Gravity", &IKIGAI::ECS::SpriteParticleComponent::Emmiter::gravity)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)
//		)
//	.property("Color", &IKIGAI::ECS::SpriteParticleComponent::Emmiter::color)
//	(
//		rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)
//		)
//		.property("Scale", &IKIGAI::ECS::SpriteParticleComponent::Emmiter::size)
//		(
//			rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)
//			)
//		.property("Weight", &IKIGAI::ECS::SpriteParticleComponent::Emmiter::weight)
//		(
//			rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)
//			)
//		.property("Angle", &IKIGAI::ECS::SpriteParticleComponent::Emmiter::angle)
//		(
//			rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)
//			)
//		.property("AngleForce", &IKIGAI::ECS::SpriteParticleComponent::Emmiter::angleForce)
//		(
//			rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)
//			)
//		.property("LifeTime", &IKIGAI::ECS::SpriteParticleComponent::Emmiter::lifeTime)
//		(
//			rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)
//			)
//		.property("Piece", &IKIGAI::ECS::SpriteParticleComponent::Emmiter::piece)
//		(
//			rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)
//			)
//		.property("SpawnTime", &IKIGAI::ECS::SpriteParticleComponent::Emmiter::spawnTime)
//		(
//			rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)
//			)
//		.property("SpawnCount", &IKIGAI::ECS::SpriteParticleComponent::Emmiter::spawnCount)
//		(
//			rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)
//			)
//		.property("Pause", &IKIGAI::ECS::SpriteParticleComponent::Emmiter::curSpawnTime)
//		(
//			rttr::metadata(MetaInfo::FLAGS, MetaInfo::SERIALIZABLE | MetaInfo::USE_IN_EDITOR_COMPONENT_INSPECTOR)
//			)
//	;
//
//	//MATH TODO: move somewhere
//	rttr::registration::class_<IKIGAI::MATHGL::Vector2f>("Vector2f")
//		.property("X", &IKIGAI::MATHGL::Vector2f::x)
//		.property("Y", &IKIGAI::MATHGL::Vector2f::y);
//
//	rttr::registration::class_<IKIGAI::MATHGL::Vector2i>("Vector2i")
//		.property("X", &IKIGAI::MATHGL::Vector2i::x)
//		.property("Y", &IKIGAI::MATHGL::Vector2i::y);
// }

#include "coreModule/ecs/componentCatalog.h"
namespace {
[[maybe_unused]] const bool kRegisterComponents = [] {
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::RootGuiComponent>(IKIGAI::ECS::StorageType::Sparse, true);
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::SpriteComponent>(IKIGAI::ECS::StorageType::Sparse, true);
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::SpriteAnimateComponent>(IKIGAI::ECS::StorageType::Sparse, true);
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::SpriteParticleComponent>();
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::LabelComponent>(IKIGAI::ECS::StorageType::Sparse, true);
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::SpineComponent>();
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::InteractionComponent>(IKIGAI::ECS::StorageType::Sparse, true);
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::ClipComponent>(IKIGAI::ECS::StorageType::Sparse, true);
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::ScrollComponent>(IKIGAI::ECS::StorageType::Sparse, true);
	IKIGAI::ECS::ComponentCatalog::enroll<IKIGAI::ECS::LayoutComponent>(IKIGAI::ECS::StorageType::Sparse, true);
	return true;
}();
}
