#pragma once
#include "mathModule/math.h"
#include "renderModule/backends/interface/renderEnums.h"
#include <memory>
#include <vector>
namespace IKIGAI::ECS {
class Object;
class EnvironmentComponent;
}
namespace IKIGAI::SCENE_SYSTEM {
class Scene;
}
namespace IKIGAI::RENDER {
class Renderer;
class TextureInterface;
class MaterialInterface;
class MeshInterface;
class ParticleRenderer {
public:
	struct Statistics {
		size_t particles = 0, draws = 0, uploadBytes = 0;
	};
	Statistics statistics() const {
		return mStatistics;
	}
	void drawScreen(ECS::Object& object, Renderer& renderer, const MATH::Matrix4f& projection, const Scissor& clip, bool clipped,
		const MATH::Matrix4f& correction);
	void drawWorld(SCENE_SYSTEM::Scene& scene, Renderer& renderer, const MATH::Matrix4f& view, const MATH::Matrix4f& projection);
	void drawWeather(ECS::EnvironmentComponent& environment, Renderer& renderer, const MATH::Matrix4f& view, const MATH::Matrix4f& projection,
		std::shared_ptr<TextureInterface> fallback, std::shared_ptr<TextureInterface> sceneDepth);

private:
	std::shared_ptr<MaterialInterface> mWeatherMaterial = {};
	struct Instance {
		MATH::Vector3f origin, right, up;
		MATH::Vector4f color, uv;
		MATH::Vector2f pivot;
		MATH::Vector3f forward;
	};
	struct Item {
		Instance instance;
		std::shared_ptr<TextureInterface> texture;
		std::shared_ptr<MaterialInterface> material;
		std::shared_ptr<MeshInterface> mesh;
		int layer = 0, order = 0;
		float depth = 0;
		int blend = 0;
		bool depthTest = false;
	};
	Statistics mStatistics;
	template <int D, class C>
	void gather(const C& component, std::vector<Item>& items, bool screen, MATH::Matrix4f transform, MATH::Matrix4f view,
		const MATH::Matrix4f& correction = MATH::Matrix4f::Identity);
	void draw(std::vector<Item>& items, Renderer& renderer, const MATH::Matrix4f& projection, const Scissor& clip, bool clipped,
		std::shared_ptr<TextureInterface> sceneDepth = {});
};
}
