#include "particleRenderer.h"
#include "coreModule/ecs/components/environmentComponent.h"
#include "coreModule/ecs/components/particleSystemComponent.h"
#include "coreModule/gui/components/spriteComponent.h"
#include "renderModule/backends/interface/materialInterface.h"
#include "renderModule/backends/interface/meshInterface.h"
#include "renderModule/backends/interface/textureInterface.h"
#include "renderModule/backends/meshFactory.h"
#include "renderModule/render.h"
#include "resourceModule/materialManager.h"
#include "resourceModule/particleManager.h"
#include "sceneModule/scene.h"
#include <algorithm>

namespace IKIGAI::RENDER {
using namespace MATH;
template <int D, class C>
void ParticleRenderer::gather(
	const C& component, std::vector<Item>& items, bool screen, Matrix4f ownerTransform, Matrix4f view, const Matrix4f& correction) {
	if (!component.runtime() || !component.asset()) {
		return;
	}
	const auto& runtime = *component.runtime();
	const auto& asset = *component.asset();
	auto camera = Matrix4f::Inverse(Matrix4f::Transpose(view));
	const Vector3f cameraRight { camera(0, 0), camera(1, 0), camera(2, 0) }, cameraUp { camera(0, 1), camera(1, 1), camera(2, 1) },
		cameraForward { camera(0, 2), camera(1, 2), camera(2, 2) };
	for (size_t i = 0; i < runtime.emitters().size(); ++i) {
		const auto& e = runtime.definition().emitters[i];
		const auto& r = e.renderer;
		if (r.screen != screen) {
			continue;
		}
		auto transform = ownerTransform;
		const auto& resources = asset.renderers[i];
		if (!resources.texture || !resources.material) {
			continue;
		}
		auto baseUV = resources.texture->getPieceUV(resources.piece);
		if (e.local && !e.inheritScale) {
			for (int c = 0; c < 3; ++c) {
				float length = 0;
				for (int row = 0; row < 3; ++row) {
					length += transform(row, c) * transform(row, c);
				}
				length = std::sqrt(length);
				if (length > 1e-8f) {
					for (int row = 0; row < 3; ++row) {
						transform(row, c) /= length;
					}
				}
			}
		}
		for (const auto& p : runtime.emitters()[i].particles) {
			Item item;
			item.texture = resources.texture;
			item.material = resources.material;
			item.mesh = resources.mesh;
			item.layer = r.layer;
			item.order = r.order;
			item.blend = int(r.blend);
			item.depthTest = r.depthTest;
			auto& instance = item.instance;
			Vector3f position { p.position[0] + p.offset[0], p.position[1] + p.offset[1], p.planeZ };
			Vector3f velocity { p.velocity[0], p.velocity[1], 0 };
			if constexpr (D == 3) {
				position.z = p.position[2] + p.offset[2];
				velocity.z = p.velocity[2];
			}
			if (e.local) {
				const auto v = transform * Vector4f(position.x, position.y, position.z, 1);
				position = { v.x, v.y, v.z };
				const auto speed = transform * Vector4f(velocity.x, velocity.y, velocity.z, 0);
				velocity = { speed.x, speed.y, speed.z };
			}
			else if (screen) {
				const auto v = correction * Vector4f(position.x, position.y, position.z, 1);
				position = { v.x, v.y, v.z };
			}
			instance.origin = position;
			Vector3f right { 1, 0, 0 }, up { 0, 1, 0 };
			if constexpr (D == 3) {
				right = cameraRight;
				up = cameraUp;
				if (r.alignment == PARTICLES::Alignment::AxisLocked) {
					up = { 0, 1, 0 };
					right = Vector3f::Normalize(Vector3f::Cross(up, cameraForward));
					if (Vector3f::Length(right) < 1e-6f) {
						right = cameraRight;
					}
				}
				if (r.alignment == PARTICLES::Alignment::Velocity && Vector3f::Length(velocity) > 1e-6f) {
					const auto projected = cameraRight * Vector3f::Dot(velocity, cameraRight) + cameraUp * Vector3f::Dot(velocity, cameraUp);
					if (Vector3f::Length(projected) > 1e-6f) {
						up = Vector3f::Normalize(projected);
						right = Vector3f::Normalize(Vector3f::Cross(up, cameraForward));
					}
				}
			}
			else if (e.local) {
				right = { transform(0, 0), transform(1, 0), transform(2, 0) };
				up = { transform(0, 1), transform(1, 1), transform(2, 1) };
			}
			else if (screen) {
				auto matrix = correction;
				right = { matrix(0, 0), matrix(1, 0), matrix(2, 0) };
				up = { matrix(0, 1), matrix(1, 1), matrix(2, 1) };
			}
			const float angle = p.rotation * PI / 180, c = std::cos(angle), s = std::sin(angle);
			const auto originalRight = right;
			right = right * c + up * s;
			up = up * c - originalRight * s;
			float sx = p.size[0], sy = p.size[1];
			if constexpr (D == 3) {
				if (e.local && e.inheritScale) {
					float x = 0, y = 0;
					for (int row = 0; row < 3; ++row) {
						x += transform(row, 0) * transform(row, 0);
						y += transform(row, 1) * transform(row, 1);
					}
					sx *= std::sqrt(x);
					sy *= std::sqrt(y);
				}
			}
			instance.right = right * sx;
			instance.up = up * sy;
			if constexpr (D == 3) {
				if (item.mesh) {
					auto matrix = PARTICLES::meshTransform(p, e, ownerTransform);
					position = instance.origin = { matrix(0, 3), matrix(1, 3), matrix(2, 3) };
					instance.right = { matrix(0, 0), matrix(1, 0), matrix(2, 0) };
					instance.up = { matrix(0, 1), matrix(1, 1), matrix(2, 1) };
					instance.forward = { matrix(0, 2), matrix(1, 2), matrix(2, 2) };
				}
			}
			instance.pivot = r.pivot;
			const float alpha = p.color[3], premultiply = r.blend == PARTICLES::Blend::Premultiplied ? alpha : 1;
			instance.color = { p.color[0] * premultiply, p.color[1] * premultiply, p.color[2] * premultiply, alpha };
			const int frames = r.columns * r.rows, frame = int(std::clamp(p.frame, 0.f, float(frames - 1)));
			const float width = baseUV.mW / r.columns, height = baseUV.mH / r.rows;
			instance.uv = { baseUV.mX + (frame % r.columns) * width, 1 - baseUV.mY - (frame / r.columns) * height, width, -height };
			const auto v = Matrix4f::Transpose(view) * Vector4f(position.x, position.y, position.z, 1);
			item.depth = -v.z;
			items.push_back(std::move(item));
		}
	}
}
void ParticleRenderer::drawScreen(
	ECS::Object& object, Renderer& renderer, const Matrix4f& projection, const Scissor& clip, bool clipped, const Matrix4f& correction) {
	std::vector<Item> items;
	const auto transform = correction * object.getTransform()->getTransform().getWorldMatrix();
	if (auto c = object.getComponent<ECS::ParticleSystem2DComponent>()) {
		gather<2>(*c, items, true, transform, Matrix4f::Identity, correction);
	}
	if (auto c = object.getComponent<ECS::SpriteParticleComponent>()) {
		gather<2>(*c, items, true, transform, Matrix4f::Identity, correction);
	}
	draw(items, renderer, projection, clip, clipped);
}
void ParticleRenderer::drawWorld(SCENE_SYSTEM::Scene& scene, Renderer& renderer, const Matrix4f& view, const Matrix4f& projection) {
	std::vector<Item> items;
	for (const auto& object : scene.getObjects()) {
		if (object->getIsActive() && object->getIsVisible()) {
			const auto transform = object->getTransform()->getTransform().getWorldMatrix();
			if (auto c = object->getComponent<ECS::ParticleSystem2DComponent>()) {
				gather<2>(*c, items, false, transform, view);
			}
			if (auto c = object->getComponent<ECS::ParticleSystem3DComponent>()) {
				gather<3>(*c, items, false, transform, view);
			}
			if (auto c = object->getComponent<ECS::SpriteParticleComponent>()) {
				gather<2>(*c, items, false, transform, view);
			}
		}
	}
	draw(items, renderer, projection, {}, false);
}
void ParticleRenderer::drawWeather(ECS::EnvironmentComponent& environment, Renderer& renderer, const Matrix4f& view, const Matrix4f& projection,
	std::shared_ptr<TextureInterface> fallback, std::shared_ptr<TextureInterface> sceneDepth) {
	if (environment.weather().particles().empty()) {
		return;
	}
	if (!mWeatherMaterial) {
		mWeatherMaterial = RESOURCES::ServiceManager::Get<RESOURCES::MaterialLoader>().loadResource("/materials/weather/precipitation.mat");
	}
	const auto worldView = Matrix4f::Transpose(view);
	auto inverseView = Matrix4f::Inverse(worldView);
	const Vector3f cameraRight { inverseView(0, 0), inverseView(1, 0), inverseView(2, 0) };
	const Vector3f cameraUp { inverseView(0, 1), inverseView(1, 1), inverseView(2, 1) };
	const Vector3f cameraForward { inverseView(0, 2), inverseView(1, 2), inverseView(2, 2) };
	std::vector<Item> items;
	items.reserve(environment.weather().particles().size());
	const float daylight = environment.runtime().gpu().CloudShape.w;
	for (const auto& particle : environment.weather().particles()) {
		if (particle.Lifetime <= 0) {
			continue;
		}
		Item item {};
		item.material = mWeatherMaterial;
		item.texture = fallback;
		item.depthTest = true;
		item.instance.origin = particle.Position;
		item.instance.pivot = { .5f, .5f };
		item.instance.uv = { float(particle.Kind) * 2, 0, 1, 1 };
		item.instance.right = cameraRight * particle.Size;
		item.instance.up = cameraUp * particle.Size;
		if (particle.Kind == 0) {
			const auto up = Vector3f::Normalize(particle.Velocity);
			auto right = Vector3f::Cross(up, cameraForward);
			if (right.dot(right) < 1e-6f) {
				right = cameraRight;
			}
			item.instance.right = Vector3f::Normalize(right) * particle.Size;
			item.instance.up = up * .65f;
		}
		float alpha = particle.Kind == 0 ? .4f : .85f;
		if (particle.Kind == 2) {
			alpha *= std::clamp(particle.Lifetime * 4, 0.0f, 1.0f);
		}
		const float light = .08f + daylight * .92f;
		item.instance.color = { light * .85f, light * .92f, light, alpha };
		item.depth = -(worldView * Vector4f(particle.Position.x, particle.Position.y, particle.Position.z, 1)).z;
		items.push_back(item);
	}
	draw(items, renderer, projection, {}, false, sceneDepth);
}
void ParticleRenderer::draw(std::vector<Item>& items, Renderer& renderer, const Matrix4f& projection, const Scissor& clip, bool clipped,
	std::shared_ptr<TextureInterface> sceneDepth) {
	mStatistics = {};
	if (items.empty()) {
		return;
	}
	std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
		if (a.layer != b.layer) {
			return a.layer < b.layer;
		}
		if (a.order != b.order) {
			return a.order < b.order;
		}
		return a.depth > b.depth;
	});
	const std::vector<Vector2f> corners { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	const std::vector<uint32_t> indices { 0, 1, 2, 2, 3, 0 };
	const auto ubo = renderer.createUniformBuffer<Matrix4f>(projection);
	for (size_t i = 0; i < items.size();) {
		const auto& first = items[i];
		size_t end = i + 1;
		while (end < items.size() && items[end].texture == first.texture && items[end].material == first.material && items[end].blend == first.blend
			&& items[end].depthTest == first.depthTest && items[end].mesh == first.mesh) {
			++end;
		}
		std::vector<Instance> instances;
		instances.reserve(end - i);
		for (size_t k = i; k < end; ++k) {
			instances.push_back(items[k].instance);
		}
		MeshData data;
		data.vertexCount = 4;
		data.indices = indices;
		data.layout.streams = { { 0, sizeof(Vector2f), VertexInputRate::PerVertex }, { 1, sizeof(Instance), VertexInputRate::PerInstance } };
		data.layout.attributes = { { 0, 0, VertexFormat::Float2, 0 }, { 1, 1, VertexFormat::Float3, offsetof(Instance, origin) },
			{ 2, 1, VertexFormat::Float3, offsetof(Instance, right) }, { 3, 1, VertexFormat::Float3, offsetof(Instance, up) },
			{ 4, 1, VertexFormat::Float4, offsetof(Instance, color) }, { 5, 1, VertexFormat::Float4, offsetof(Instance, uv) },
			{ 6, 1, VertexFormat::Float2, offsetof(Instance, pivot) } };
		data.streams = { VertexStreamData::From(0, corners), VertexStreamData::From(1, instances) };
		if (first.mesh) {
			const auto& vertices = first.mesh->rayTracingVertices();
			data.vertexCount = vertices.size();
			data.indices = first.mesh->rayTracingIndices();
			data.layout.streams[0].stride = sizeof(Vertex);
			data.layout.attributes[0] = { 0, 0, VertexFormat::Float3, offsetof(Vertex, position) };
			data.layout.attributes.back() = { 7, 1, VertexFormat::Float3, offsetof(Instance, forward) };
			data.layout.attributes.push_back({ 8, 0, VertexFormat::Float2, offsetof(Vertex, texCoord) });
			data.streams[0] = VertexStreamData::From(0, vertices);
		}
		auto mesh = CreateMesh(data);
		renderer.setShader(first.material->getShader());
		first.material->bind(first.texture, true);
		renderer.setUniformBuffer("engine_Particles", ubo);
		renderer.setTexture("Image", first.texture);
		if (sceneDepth) {
			renderer.setTexture("SceneDepth", sceneDepth);
		}
		renderer.setCull(CullFace::NONE);
		renderer.setDepth({ first.depthTest ? DepthFunction::LESS_EQUAL : DepthFunction::ALWAYS, false });
		if (first.blend == int(PARTICLES::Blend::Additive)) {
			renderer.setBlending({ BlendMode::SRC_ALPHA, BlendMode::ONE });
		}
		else if (first.blend == int(PARTICLES::Blend::Premultiplied)) {
			renderer.setBlending({ BlendMode::ONE, BlendMode::ONE_MINUS_SRC_ALPHA });
		}
		else {
			renderer.setBlending({ BlendMode::SRC_ALPHA, BlendMode::ONE_MINUS_SRC_ALPHA, BlendMode::ONE, BlendMode::ONE_MINUS_SRC_ALPHA });
		}
		if (clipped) {
			renderer.setScissor(clip);
		}
		else {
			renderer.resetScissor();
		}
		renderer.draw(mesh, PrimitiveMode::TRIANGLES, uint32_t(instances.size()));
		mStatistics.particles += instances.size();
		mStatistics.uploadBytes += instances.size() * sizeof(Instance);
		++mStatistics.draws;
		i = end;
	}
	renderer.resetScissor();
	renderer.resetBlending();
	renderer.resetDepth();
}
}
