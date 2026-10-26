#include "vectorRenderer.h"
#include "../vectorContext.h"
#include "coreModule/ecs/object.h"
#include "coreModule/gui/components/vectorComponents.h"
#include "renderModule/backends/meshFactory.h"
#include "renderModule/render.h"
#include "resourceModule/fontManager.h"
#include "utilsModule/jsonLoader.h"
#include <bit>
#include <cstring>
#include <cstdlib>
#include <iostream>

namespace IKIGAI::VECTOR {
namespace {
	Vec2 vector(MATH::Vector2f v) {
		return { v.x, v.y };
	}
	Color color(MATH::Vector4f v) {
		return { v.x, v.y, v.z, v.w };
	}
	template <class T> Paint paint(const T& c) {
		return { color(c.Color), color(c.GradientColor), vector(c.GradientStart), vector(c.GradientEnd), c.Gradient };
	}
	template <class T> StrokeStyle style(const T& c) {
		return { { color(c.StrokeColor) }, c.StrokeWidth, c.MiterLimit, static_cast<LineCap>(std::clamp(c.Cap, 0, 2)),
			static_cast<LineJoin>(std::clamp(c.Join, 0, 2)) };
	}
	template <class T> std::string key(const T& c) {
		return std::to_string(c.configurationHash());
	}
	bool sameBlend(const RENDER::Blending& a, const RENDER::Blending& b) {
		return a.mColorSrc == b.mColorSrc && a.mColorDst == b.mColorDst && a.mAlphaSrc == b.mAlphaSrc && a.mAlphaDst == b.mAlphaDst
			&& a.mColorFunc == b.mColorFunc && a.mAlphaFunc == b.mAlphaFunc && a.mColorMask == b.mColorMask;
	}
	void hash(uint64_t& h, const void* p, size_t n) {
		auto b = static_cast<const unsigned char*>(p);
		for (size_t i = 0; i < n; ++i) {
			h ^= b[i];
			h *= 1099511628211ull;
		}
	}
}

void VectorRenderer::beginFrame(bool world) {
	++mFrame;
	mWorld = world;
	mBatches.clear();
	mStatistics = {};
}

std::shared_ptr<RENDER::TextureInterface> VectorRenderer::pageTexture(
	const std::shared_ptr<FontResource>& font, unsigned page, RENDER::Renderer& renderer) {
	auto& cache = mFonts[font.get()];
	cache.font = font;
	cache.seen = mFrame;
	if (cache.generation != font->revision()) {
		cache.pages.clear();
		cache.generation = font->revision();
	}
	if (cache.pages.size() <= page) {
		cache.pages.resize(page + 1);
	}
	auto& entry = cache.pages[page];
	AtlasPage snapshot;
	if (font->snapshotPage(page, entry.revision, snapshot)) {
		RENDER::TextureResource resource;
		resource.width = resource.height = snapshot.size;
		resource.channels = 1;
		resource.pixelType = RENDER::PixelFormat::R_INT;
		resource.useMipmap = false;
		resource.colorData = std::move(snapshot.pixels);
		// Publish a fresh GPU texture rather than mutating a texture referenced
		// by commands from earlier frames. UVs remain fixed within the page.
		entry.texture = renderer.createTexture(resource);
		entry.revision = snapshot.revision;
		++mStatistics.atlasUploads;
	}
	return entry.texture;
}

std::vector<PreparedDraw> VectorRenderer::prepare(ECS::Object& object, float pixelScale, RENDER::Renderer& renderer) {
	std::vector<PreparedDraw> result;
	pixelScale = std::clamp(pixelScale, .001f, 10000.f);
	GeometryOptions options { .35f / pixelScale, 1.f / pixelScale };
	if (!mWhite) {
		RENDER::TextureResource r;
		r.width = r.height = 1;
		r.channels = 4;
		r.pixelType = RENDER::PixelFormat::RGBA_INT;
		r.useMipmap = false;
		r.colorData = { 255, 255, 255, 255 };
		mWhite = renderer.createTexture(r);
	}
	auto geometry = [&](auto component, const Path& path, const std::string& signature) {
		auto& cached = mComponents[component.get()];
		cached.seen = mFrame;
		if (cached.key != signature) {
			VectorContext context(options);
			auto p = paint(*component);
			auto s = style(*component);
			context.drawPath(path, component->Filled ? &p : nullptr, component->StrokeWidth > 0 ? &s : nullptr);
			Mesh mesh;
			for (auto& command : context.commands()) {
				mesh.append(command.mesh);
			}
			cached.mesh = std::move(mesh);
			cached.key = signature;
			++mStatistics.geometryBuilds;
			auto b = bounds(cached.mesh);
			object.getTransform()->getTransform().setLocalSize({ std::max(b.max.x, 0.f), std::max(b.max.y, 0.f) });
		}
		result.push_back({ &cached.mesh, mWhite, false, {} });
	};
	// Invalid editor input should report once and keep the last valid geometry.
	if (auto c = object.getComponent<ECS::VectorShapeComponent>()) {
		auto signature = key(*c) + ":" + std::to_string(pixelScale);
		auto& cache = mComponents[c.get()];
		try {
			if (cache.key != signature) {
				Path path;
				if (c->Shape == 0) {
					path = rectangle(vector(c->Size));
				}
				else if (c->Shape == 1) {
					auto r = c->CornerRadii;
					path = (r.x > 0 || r.y > 0 || r.z > 0 || r.w > 0) ? roundedRectangle(vector(c->Size), { r.x, r.y, r.z, r.w })
																	  : rectangle(vector(c->Size), vector(c->Radii));
				}
				else if (c->Shape == 2) {
					float d = std::min(c->Size.x, c->Size.y);
					path = ellipse({ d, d }, 0, 360, false, options);
				}
				else if (c->Shape == 3) {
					path = ellipse(vector(c->Size), 0, 360, false, options);
				}
				else if (c->Shape == 4) {
					path = ellipse(vector(c->Size), c->StartAngle, c->EndAngle, c->Filled, options);
				}
				else if (c->Shape == 5) {
					path = parsePath(c->Points);
				}
				else {
					throw std::invalid_argument("Unknown vector shape type");
				}
				geometry(c, path, signature);
			}
			else {
				cache.seen = mFrame;
				result.push_back({ &cache.mesh, mWhite });
			}
		} catch (const std::exception& e) {
			if (cache.key != signature) {
				std::cerr << "Vector shape: " << e.what() << '\n';
			}
			cache.key = signature;
			cache.seen = mFrame;
			result.push_back({ &cache.mesh, mWhite });
		}
	}
	if (auto c = object.getComponent<ECS::VectorPathComponent>()) {
		auto signature = key(*c) + ":" + std::to_string(pixelScale);
		auto& cache = mComponents[c.get()];
		if (!c->PathAsset.empty()) {
			auto& fs = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
			if (auto path = fs.getFilePath(c->PathAsset)) {
				std::error_code error;
				auto time = std::filesystem::last_write_time(*path, error);
				if (!error) {
					const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
					signature += std::to_string(ticks);
				}
			}
		}
		try {
			if (cache.key != signature) {
				std::string commands = c->Commands;
				if (!c->PathAsset.empty()) {
					auto file = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>().getFile(c->PathAsset);
					if (!file || !file->isValid()) {
						throw std::runtime_error("Vector path asset not found: " + c->PathAsset);
					}
					auto json = nlohmann::json::parse(file->read());
					commands = json.at("Commands").get<std::string>();
				}
				geometry(c, parsePath(commands), signature);
			}
			else {
				cache.seen = mFrame;
				result.push_back({ &cache.mesh, mWhite });
			}
		} catch (const std::exception& e) {
			if (cache.key != signature) {
				std::cerr << "Vector path: " << e.what() << '\n';
			}
			cache.key = signature;
			cache.seen = mFrame;
			result.push_back({ &cache.mesh, mWhite });
		}
	}
	if (auto c = object.getComponent<ECS::SdfTextComponent>()) {
		auto& cache = mComponents[c.get()];
		cache.seen = mFrame;
		auto signature = key(*c);
		try {
			auto font = RESOURCES::ServiceManager::Get<RESOURCES::FontLoader>().loadResource(c->Font);
			signature += ":" + std::to_string(font->revision());
			if (cache.key != signature) {
				TextOptions o;
				o.size = c->Size;
				o.wrapWidth = c->WrapWidth;
				o.alignment = static_cast<TextAlign>(std::clamp(c->Alignment, 0, 2));
				o.letterSpacing = c->LetterSpacing;
				o.lineSpacing = c->LineSpacing;
				o.color = color(c->Color);
				auto text = layoutText(*font, c->Text, o);
				cache.font = font;
				cache.text = std::move(text);
				cache.key = signature;
				++mStatistics.textLayouts;
				object.getTransform()->getTransform().setLocalSize({ cache.text.size.x, cache.text.size.y });
			}
			if (cache.font) {
				auto o = cache.font->options();
				float scale = std::max(c->Size, .001f) / o.pixelSize;
				float outline = std::clamp(c->OutlineWidth / scale, 0.f, float(o.spread) - 2);
				auto shadow = vector(c->ShadowOffset) * (1.f / scale);
				float maxOffset = std::max(std::abs(shadow.x), std::abs(shadow.y));
				if (maxOffset > float(o.spread) - 2) {
					shadow = shadow * ((o.spread - 2) / maxOffset);
				}
				SdfEffects effects { { float(o.spread), outline, shadow.x / o.pageSize, shadow.y / o.pageSize }, c->OutlineColor, c->ShadowColor };
				for (auto& run : cache.text.runs) {
					result.push_back({ &run.mesh, pageTexture(cache.font, run.page, renderer), true, effects });
				}
			}
		} catch (const std::exception& e) {
			if (cache.key != signature) {
				std::cerr << "SDF text: " << e.what() << '\n';
			}
			cache.key = signature;
		}
	}
	return result;
}

void VectorRenderer::enqueue(std::vector<::IKIGAI::Vertex> vertices, const std::vector<unsigned>& indices,
	std::shared_ptr<RENDER::TextureInterface> texture, std::shared_ptr<RENDER::ShaderInterface> shader, const MATH::Matrix4f& projection,
	RENDER::Blending blend, RENDER::Depth depth, const RENDER::Scissor& clip, bool clipped, bool sdf, SdfEffects effects) {
	if (vertices.empty() || indices.empty() || !texture || !shader) {
		return;
	}
	Batch* batch = nullptr;
	if (!mBatches.empty()) {
		auto& b = mBatches.back();
		if (b.texture == texture && b.shader == shader && b.projection == projection && sameBlend(b.blend, blend) && b.depth.mFunc == depth.mFunc
			&& b.depth.mWriteMask == depth.mWriteMask && b.clipped == clipped
			&& (!clipped || (b.clip.mPosition == clip.mPosition && b.clip.mSize == clip.mSize)) && b.sdf == sdf && b.effects == effects) {
			batch = &b;
		}
	}
	if (!batch) {
		mBatches.emplace_back();
		batch = &mBatches.back();
		batch->texture = std::move(texture);
		batch->shader = std::move(shader);
		batch->projection = projection;
		batch->blend = blend;
		batch->depth = depth;
		batch->clip = clip;
		batch->clipped = clipped;
		batch->sdf = sdf;
		batch->effects = effects;
	}
	unsigned base = static_cast<unsigned>(batch->vertices.size());
	batch->vertices.insert(batch->vertices.end(), vertices.begin(), vertices.end());
	for (unsigned i : indices) {
		batch->indices.push_back(base + i);
	}
}

void VectorRenderer::flush(RENDER::Renderer& renderer) {
	auto& mBatchCache = mBatchCaches[mWorld ? 1 : 0];
	mBatchCache.resize(mBatches.size());
	for (size_t i = 0; i < mBatches.size(); ++i) {
		auto& b = mBatches[i];
		auto& c = mBatchCache[i];
		uint64_t h = 1469598103934665603ull;
		for (auto& v : b.vertices) {
			hash(h, &v.position, sizeof(v.position));
			hash(h, &v.texCoord, sizeof(v.texCoord));
			hash(h, &v.m_Weights, sizeof(v.m_Weights));
			hash(h, &v.normal, sizeof(v.normal));
			hash(h, &v.tangent, sizeof(v.tangent));
		}
		hash(h, b.indices.data(), b.indices.size() * sizeof(unsigned));
		hash(h, &b.projection, sizeof(b.projection));
		hash(h, &b.effects, sizeof(b.effects));
		if (!c.mesh || c.hash != h) {
			c.mesh = RENDER::CreateMesh(b.vertices, b.indices);
			c.projection = renderer.createUniformBuffer<MATH::Matrix4f>(b.projection);
			if (b.sdf) {
				c.effects = renderer.createUniformBuffer<SdfEffects>(b.effects);
			}
			c.hash = h;
			++mStatistics.meshUploads;
		}
		if (!c.mesh) {
			throw std::runtime_error("Vector GUI mesh upload unavailable for active backend");
		}
		renderer.setShader(b.shader);
		renderer.setDepth(b.depth);
		renderer.setCull(RENDER::CullFace::NONE);
		renderer.setBlending(b.blend);
		if (b.clipped) {
			renderer.setScissor(b.clip);
		}
		else {
			renderer.resetScissor();
		}
		renderer.setUniformBuffer("engine_Gui", c.projection);
		renderer.setTexture("Image", b.texture);
		if (b.sdf) {
			renderer.setUniformBuffer("engine_Sdf", c.effects);
		}
		renderer.draw(c.mesh, RENDER::PrimitiveMode::TRIANGLES, 1);
	}
	renderer.resetScissor();
	renderer.resetBlending();
	renderer.resetDepth();
	std::erase_if(mComponents, [&](auto& entry) { return entry.second.seen + 2 < mFrame; });
	std::erase_if(mFonts, [&](auto& entry) { return entry.second.seen + 2 < mFrame; });
	mStatistics.batches = mBatches.size();
	if (std::getenv("IKIGAI_VECTOR_STATS")) {
		std::cout << "VECTOR_STATS pass=" << (mWorld ? "world" : "screen") << " frame=" << mFrame
			<< " geometry=" << mStatistics.geometryBuilds << " layout=" << mStatistics.textLayouts
			<< " atlas=" << mStatistics.atlasUploads << " mesh=" << mStatistics.meshUploads
			<< " batches=" << mStatistics.batches << '\n';
	}
}
}
