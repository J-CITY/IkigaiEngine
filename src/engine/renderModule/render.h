#pragma once
#include <memory>
#include <type_traits>

#include "backends/interface/driverInterface.h"
#include "backends/interface/frameBufferInterface.h"
#include "backends/interface/meshInterface.h"

namespace IKIGAI::RENDER {
	class MaterialInterface;
	class StorageBufferInterface;
	class TextureInterface;
	class DriverInterface;

	class TextureBase {
	public:
		virtual ~TextureBase() = default;
	};

	class Texture : public TextureBase {
	protected:
		class Internal;
		std::unique_ptr<Internal> mInternal;

	public:
		enum class Type {
			TEXTURE_2D = 0,
			TEXTURE_3D,
			TEXTURE_CUBE,
			TEXTURE_2D_ARRAY,
			DEPTH
		};

		virtual ~Texture() override = default;

		Type getType() const;
		PixelFormat getFormat() const;
		const std::string& getPath() const;
		size_t getWidth() const;
		size_t getHeight() const;
		size_t getDepth() const;
		size_t getChannels() const;

		void* getImguiId();
	};

	class DriverInterface;
	class Renderer;

	enum class OpCode : uint8_t {
		LAMBDA,
		DRAW,
		DRAW_INDEXED,
		DRAW_MESH,
		SET_VIEWPORT,
		SET_SCISSOR,
		SET_SHADER,
		SET_VERTEX_BUFFER,
		SET_INDEX_BUFFER,
		SET_TEXTURE,
		SET_UNIFORM_BUFFER,
		SET_STORAGE_BUFFER,
		SET_TEXTURE_NAMED,
		SET_UNIFORM_BUFFER_NAMED,
		SET_STORAGE_BUFFER_NAMED,
		SET_MSAA,
		CLEAR,
		SET_PRIMITIVE_MODE,
		SET_RASTERIZATION,
		SET_BLEND,
		SET_DEPTH,
		SET_STENCIL,
		SET_CULL,
		SET_CLEAR_COLOR_VEC,
		SET_CLEAR_COLOR_FLOAT,
		SET_TRIANGLE_ORIENTATION,
		RESET_VIEWPORT,
		RESET_SCISSOR,
		RESET_BLEND,
		RESET_DEPTH,
		RESET_STENCIL,
		BEGIN,
		END,
		SUBMIT,
		CLEANUP,
		SET_FRAMEBUFFER,
		RESET_FRAMEBUFFER,
		SET_PUSH_CONSTANT,
		EXIT
	};

	struct CommandHeader {
		OpCode op;
		size_t size;
	};

	struct CmdDraw : CommandHeader {
		uint32_t count;
		uint32_t offset;
		uint32_t instance;
	};

	struct CmdDrawIndexed : CommandHeader {
		uint32_t count;
		uint32_t offset;
		uint32_t instance;
	};

	struct CmdLambda : CommandHeader {
		std::function<void()> func;
		// Manual constructor/destructor handling in CommandBuffer
	};

	// POD wrappers for simple arguments
	template <typename T> struct CmdWrapper : CommandHeader {
		T data;
	};

	template <typename T> struct CmdWrapper2 : CommandHeader {
		T data1;
		size_t binding;
	};

	struct CmdSetTextureNamed : CommandHeader {
		char name[64]; // Fixed size for simplicity in POD, or handle dynamic string
		std::shared_ptr<TextureInterface> texture;
	};
	// For simplicity in this iteration, we might use specific structs for complex
	// types
	struct CmdSetTexture : CommandHeader {
		size_t bind;
		std::shared_ptr<TextureInterface> texture;
	};
	// ... similarly for others ...

	// To avoid defining 20 structs here, we can use generic templates or define
	// them as needed. For the sake of the task, I will define the necessary ones or
	// a generic approach for PODs.

	struct CmdSetViewport : CommandHeader {
		Viewport viewport;
	};
	struct CmdSetScissor : CommandHeader {
		Scissor scissor;
	};
	struct CmdSetShader : CommandHeader {
		std::shared_ptr<ShaderInterface> shader;
	};
	struct CmdSetVertexBuffer : CommandHeader {
		std::shared_ptr<VertexBufferInterface> buffer;
	};
	struct CmdSetIndexBuffer : CommandHeader {
		std::shared_ptr<IndexBufferInterface> buffer;
	};
	struct CmdSetBlending : CommandHeader {
		Blending blending;
	};
	struct CmdSetDepth : CommandHeader {
		Depth depth;
	};
	struct CmdSetStencil : CommandHeader {
		Stencil stencil;
	};
	struct CmdSetCull : CommandHeader {
		CullFace cull;
	};
	struct CmdSetTriangleOrientation : CommandHeader {
		TriangleOrientation orientation;
	};
	struct CmdSetClearColorVec : CommandHeader {
		MATH::Vector4f color;
	};
	struct CmdSetClearColorFloat : CommandHeader {
		float r, g, b, a;
	};
	struct CmdClear : CommandHeader {
		bool color;
		bool depth;
		bool stencil;
	};
	struct CmdSetPrimitiveMode : CommandHeader {
		PrimitiveMode mode;
	};
	struct CmdSetRasterization : CommandHeader {
		RasterizationMode mode;
	};
	struct CmdSetMSAA : CommandHeader {
		bool value;
	};

	struct CmdSetUniformBuffer : CommandHeader {
		size_t bind;
		std::shared_ptr<UniformBufferInterface> buffer;
	};
	struct CmdSetStorageBuffer : CommandHeader {
		size_t bind;
		std::shared_ptr<StorageBufferInterface> buffer;
	};
	struct CmdSetFrameBuffer : CommandHeader {
		std::shared_ptr<FrameBufferInterface> frameBuffer;
	};
	struct CmdDrawMesh : CommandHeader {
		std::shared_ptr<MeshInterface> mesh;
		PrimitiveMode primitive;
		uint32_t instances;
	};

	// String versions need care. We can store std::string but need destructor call
	// (CmdLambda mechanism or similar). Let's use specific structs that own the
	// std::string, and CommandBuffer destruction loop will handle them if we mark
	// them. Actually, easier to use CmdLambda for complex things or just make these
	// have destructors and rely on CommandBuffer's clear. We will implement a smart
	// iterate-and-destroy in CommandBuffer::clear().
	struct CmdStringResource : CommandHeader {
		std::string name;
		std::shared_ptr<TextureInterface> texture; // or uniform/storage
	};
	struct CmdStringUniformResource : CommandHeader {
		std::string name;
		std::shared_ptr<UniformBufferInterface> buffer;
	};
	struct CmdStringStorageResource : CommandHeader {
		std::string name;
		std::shared_ptr<StorageBufferInterface> buffer;
	};

	class RenderExecutorInterface {
	public:
		virtual ~RenderExecutorInterface() = default;
		virtual void* allocate(size_t size) = 0;
		virtual void submit(CommandHeader* header, DriverInterface* driver) = 0;
		virtual void flush(DriverInterface* driver) = 0;

		template <typename T, typename... Args>
		void submit(DriverInterface* driver, OpCode op, Args &&...args) {
			size_t size = sizeof(T);
			size_t alignedSize = (size + 7) & ~7;
			T* ptr = reinterpret_cast<T*>(allocate(alignedSize));
			if constexpr (std::is_same_v<T, CommandHeader>) {
				new (ptr) T{op, alignedSize};
			} else {
				new (ptr) T{{op, alignedSize}, std::forward<Args>(args)...};
			}
			submit(ptr, driver);
		}
	};

	class ImmediateExecutor : public RenderExecutorInterface {
		std::vector<uint8_t> mScratch;

	public:
		ImmediateExecutor() { mScratch.reserve(1024); }
		void* allocate(size_t size) override {
			if (mScratch.size() < size)
				mScratch.resize(size);
			return mScratch.data();
		}
		void submit(CommandHeader* header, DriverInterface* driver) override;
		void flush(DriverInterface* driver) override {}
	};

	class BufferedExecutor : public RenderExecutorInterface {
		std::vector<uint8_t> mBuffer;

	public:
		BufferedExecutor() { mBuffer.reserve(1024 * 1024); }
		void* allocate(size_t size) override {
			size_t offset = mBuffer.size();
			mBuffer.resize(offset + size);
			return mBuffer.data() + offset;
		}
		void submit(CommandHeader* header, DriverInterface* driver) override {
			// No-op, data is already in buffer
		}
		void flush(DriverInterface* driver) override;
	};

	struct CmdSetPushConstant : CommandHeader {
		ShaderType stage;
		uint32_t offset;
		uint32_t dataSize;
		std::byte data[128];
	};

	class Renderer {
		std::unique_ptr<RenderExecutorInterface> mExecutor;
		DriverInterface* mDriver = nullptr;

	public:
		Renderer(DriverInterface* driver,
			std::unique_ptr<RenderExecutorInterface> executor)
			: mDriver(driver), mExecutor(std::move(executor)) {
		};

		DriverInterface* getDriver() { return mDriver; }

		template <typename T, typename... Args> void push(OpCode op, Args &&...args) {
			mExecutor->submit<T>(mDriver, op, std::forward<Args>(args)...);
		}

		// Hybrid approach:
		void execute(std::function<void()> func) {
			push<CmdLambda>(OpCode::LAMBDA, func);
		}

		void begin() { push<CommandHeader>(OpCode::BEGIN); }
		void end() { push<CommandHeader>(OpCode::END); }
		void submit() { push<CommandHeader>(OpCode::SUBMIT); }
		void cleanup() { push<CommandHeader>(OpCode::CLEANUP); }

		void setViewport(const Viewport& viewport) {
			push<CmdSetViewport>(OpCode::SET_VIEWPORT, viewport);
		}
		void resetViewport() { push<CommandHeader>(OpCode::RESET_VIEWPORT); }
		void setScissor(const Scissor& scissor) {
			push<CmdSetScissor>(OpCode::SET_SCISSOR, scissor);
		}
		void resetScissor() { push<CommandHeader>(OpCode::RESET_SCISSOR); }
		void setPrimitiveMode(PrimitiveMode mode) {
			push<CmdSetPrimitiveMode>(OpCode::SET_PRIMITIVE_MODE, mode);
		}
		void setRasterization(RasterizationMode mode) {
			push<CmdSetRasterization>(OpCode::SET_RASTERIZATION, mode);
		}
		void setShader(std::shared_ptr<ShaderInterface> shader) {
			push<CmdSetShader>(OpCode::SET_SHADER, shader);
		}
		void setVertexBuffer(std::shared_ptr<VertexBufferInterface> buffer) {
			push<CmdSetVertexBuffer>(OpCode::SET_VERTEX_BUFFER, buffer);
		}
		void setIndexBuffer(std::shared_ptr<IndexBufferInterface> buffer) {
			push<CmdSetIndexBuffer>(OpCode::SET_INDEX_BUFFER, buffer);
		}
		void setBlending(const Blending& value) {
			push<CmdSetBlending>(OpCode::SET_BLEND, value);
		}
		void resetBlending() { push<CommandHeader>(OpCode::RESET_BLEND); }
		void setDepth(const Depth& depth) {
			push<CmdSetDepth>(OpCode::SET_DEPTH, depth);
		}
		void resetDepth() { push<CommandHeader>(OpCode::RESET_DEPTH); }
		void setStencil(const Stencil& stencil) {
			push<CmdSetStencil>(OpCode::SET_STENCIL, stencil);
		}
		void resetStencil() { push<CommandHeader>(OpCode::RESET_STENCIL); }
		void setCull(CullFace cull) { push<CmdSetCull>(OpCode::SET_CULL, cull); }
		void setTriangleOrientation(TriangleOrientation val) {
			push<CmdSetTriangleOrientation>(OpCode::SET_TRIANGLE_ORIENTATION, val);
		}
		void clear(bool c, bool d, bool s) { push<CmdClear>(OpCode::CLEAR, c, d, s); }
		void setClearColor(const MATH::Vector4f& color) {
			push<CmdSetClearColorVec>(OpCode::SET_CLEAR_COLOR_VEC, color);
		}
		void setClearColor(float r, float g, float b, float a) {
			push<CmdSetClearColorFloat>(OpCode::SET_CLEAR_COLOR_FLOAT, r, g, b, a);
		}
		void setMSAA(bool val) { push<CmdSetMSAA>(OpCode::SET_MSAA, val); }

		void draw(uint32_t c, uint32_t o, uint32_t i) {
			push<CmdDraw>(OpCode::DRAW, c, o, i);
		}
		void drawIndexed(uint32_t c, uint32_t o, uint32_t i) {
			push<CmdDrawIndexed>(OpCode::DRAW_INDEXED, c, o, i);
		}
		void draw(std::shared_ptr<MeshInterface> mesh, PrimitiveMode primitive,
			uint32_t instances) {
			push<CmdDrawMesh>(OpCode::DRAW_MESH, mesh, primitive, instances);
		}

		void setTexture(size_t bind, std::shared_ptr<TextureInterface> tex) {
			push<CmdSetTexture>(OpCode::SET_TEXTURE, bind, tex);
		}
		void setUniformBuffer(size_t bind,
			std::shared_ptr<UniformBufferInterface> buf) {
			push<CmdSetUniformBuffer>(OpCode::SET_UNIFORM_BUFFER, bind, buf);
		}
		void setStorageBuffer(size_t bind,
			std::shared_ptr<StorageBufferInterface> buf) {
			push<CmdSetStorageBuffer>(OpCode::SET_STORAGE_BUFFER, bind, buf);
		}

		void setTexture(const std::string& name,
			std::shared_ptr<TextureInterface> tex) {
			push<CmdStringResource>(OpCode::SET_TEXTURE_NAMED, name, tex);
		}
		void setUniformBuffer(const std::string& name,
			std::shared_ptr<UniformBufferInterface> buf) {
			push<CmdStringUniformResource>(OpCode::SET_UNIFORM_BUFFER_NAMED, name, buf);
		}
		void setStorageBuffer(const std::string& name,
			std::shared_ptr<StorageBufferInterface> buf) {
			push<CmdStringStorageResource>(OpCode::SET_STORAGE_BUFFER_NAMED, name, buf);
		}

		void setFrameBuffer(std::shared_ptr<FrameBufferInterface> frameBuffer) {
			push<CmdSetFrameBuffer>(OpCode::SET_FRAMEBUFFER, frameBuffer);
		}
		void resetFrameBuffer() { push<CommandHeader>(OpCode::RESET_FRAMEBUFFER); }

		void setPushConstant(ShaderType stage, uint32_t offset, uint32_t dataSize, const void* data) {
			size_t allocSize = sizeof(CmdSetPushConstant);
			size_t alignedSize = (allocSize + 7) & ~7;
			auto* cmd = reinterpret_cast<CmdSetPushConstant*>(mExecutor->allocate(alignedSize));
			cmd->op = OpCode::SET_PUSH_CONSTANT;
			cmd->size = alignedSize;
			cmd->stage = stage;
			cmd->offset = offset;
			cmd->dataSize = dataSize;
			memcpy(cmd->data, data, dataSize);
			mExecutor->submit(cmd, mDriver);
		}

		std::shared_ptr<UniformBufferInterface> createUniformBuffer(const void* data,
			size_t size) {
			return mDriver->createUniformBuffer(data, size);
		}

		template <typename T>
		std::shared_ptr<UniformBufferInterface> createUniformBuffer(const T& data) {
			return mDriver->createUniformBuffer(&data, sizeof(T));
		}

		std::shared_ptr<StorageBufferInterface>
			createStorageBuffer(const void* data, size_t size, size_t stride) {
			return mDriver->createStorageBuffer(data, size, stride);
		}

		template <typename T>
		std::shared_ptr<StorageBufferInterface>
			createStorageBuffer(const std::vector<T>& data) {
			return mDriver->createStorageBuffer(data.data(), data.size() * sizeof(T),
				sizeof(T));
		}

		template <typename T>
		std::shared_ptr<StorageBufferInterface>
			createStorageBuffer(const T& data, size_t stride = sizeof(T)) {
			return mDriver->createStorageBuffer(&data, sizeof(T), stride);
		}

		std::shared_ptr<TextureInterface> createTexture(const std::string& path, bool generateMipmap = true, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) {
			return mDriver->createTexture(path, generateMipmap, allocator, deleter);
		}

		std::shared_ptr<TextureInterface> createTextureAtlas(const std::string& path, bool generateMipmap = true, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) {
			return mDriver->createTextureAtlas(path, generateMipmap, allocator, deleter);
		}

		std::shared_ptr<TextureInterface> createTexture(const TextureResource& res, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) {
			return mDriver->createTexture(res, allocator, deleter);
		}

		std::shared_ptr<TextureInterface> createTexture(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) {
			return mDriver->createTexture(res, fileData, allocator, deleter);
		}

		std::shared_ptr<TextureInterface> createTextureAtlas(const TextureResource& res, const std::vector<std::vector<uint8_t>>& fileData, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) {
			return mDriver->createTextureAtlas(res, fileData, allocator, deleter);
		}

		std::shared_ptr<TextureInterface> createTexture(const std::string& path, const std::vector<uint8_t>& data, bool generateMipmap = true, UTILS::IAllocator* allocator = nullptr, TextureDeleter deleter = nullptr) {
			return mDriver->createTexture(path, data, generateMipmap, allocator, deleter);
		}

		std::shared_ptr<ShaderInterface>
			createShader(const std::string& vertexPath, const std::string& fragmentPath) {
			return mDriver->createShader(vertexPath, fragmentPath);
		}

		std::shared_ptr<ShaderInterface> createShader(const ShaderResource& res,
				UTILS::IAllocator* allocator = nullptr, ShaderDeleter deleter = nullptr) {
			return mDriver->createShader(res, allocator, deleter);
		}

		std::shared_ptr<ModelInterface> createModel(const std::string& path,
				UTILS::IAllocator* allocator = nullptr, ModelDeleter deleter = nullptr) {
			return mDriver->createModel(path, allocator, deleter);
		}

		std::shared_ptr<FrameBufferInterface> createFrameBuffer(
			const std::vector<std::shared_ptr<TextureInterface>>& textures,
			std::shared_ptr<TextureInterface> depth) {
			return mDriver->createFrameBuffer(textures, depth);
		}

		std::shared_ptr<MaterialInterface> createMaterial(
				const MaterialResource& res,
				UTILS::IAllocator* allocator = nullptr,
				MaterialDeleter deleter = nullptr) {
			return mDriver->createMaterial(res, allocator, deleter);
		}

		// Flush commands to executor (Buffer mode)
		void flush() {
			if (mExecutor && mDriver) {
				mExecutor->flush(mDriver);
			}
		}
	};
} // namespace IKIGAI::RENDER

/*#include <string>
#include "../glManager/glManager.h"


namespace IKIGAI {
		namespace RESOURCES {
				class Mesh;
				//class Animator;
		}
		namespace RENDER {
				class Material;
				class Frustum;
				class Model;
				class Camera;

				enum class PrimitiveMode {
						POINTS = 0x0000,
						LINES = 0x0001,
						LINE_LOOP = 0x0002,
						LINE_STRIP = 0x0003,
						TRIANGLES = 0x0004,
						TRIANGLE_STRIP = 0x0005,
						TRIANGLE_FAN = 0x0006,
						LINES_ADJACENCY = 0x000A,
						LINE_STRIP_ADJACENCY = 0x000B,
						TRIANGLES_ADJACENCY = 0x000C,
						TRIANGLE_STRIP_ADJACENCY = 0x000D,
						PATCHES = 0xE,
				};
				enum class RenderingCapability {
						BLEND = 0x0BE2, // If enabled, blend the computed
fragment color values with the values in the color buffers. See glBlendFunc.
						CULL_FACE = 0x0B44, // If enabled, cull polygons based
on their winding in window coordinates. DEPTH_TEST = 0x0B71, // If enabled, do
depth comparisons and update the depth buffer. Note that even if the depth
buffer exists and the depth mask is non-zero, the depth buffer is not updated if
the depth test is disabled. DITHER = 0x0BD0, // If enabled, dither color
components or indices before they are written to the color buffer.
						POLYGON_OFFSET_FILL = 0x8037, // If enabled, an offset
is added to depth values of a polygon's fragments produced by rasterization.
						SAMPLE_ALPHA_TO_COVERAGE = 0x809E, // If enabled,
compute a temporary coverage value where each bit is determined by the alpha
value at the corresponding sample location. The temporary coverage value is then
ANDed with the fragment coverage value. SAMPLE_COVERAGE = 0x80A0, // If enabled,
the fragment's coverage is ANDed with the temporary coverage value. If
GL_SAMPLE_COVERAGE_INVERT is set to GL_TRUE, invert the coverage value.
						SCISSOR_TEST = 0x0C11, // If enabled, discard fragments
that are outside the scissor rectangle. STENCIL_TEST = 0x0B90, // If enabled, do
stencil testing and update the stencil buffer. MULTISAMPLE = 0x809D  // If
enabled, use multiple fragment samples in computing the final color of a pixel.
				};
				enum class RasterizationMode {
						POINT = 0x1B00, // Polygon vertices that are marked as
the start of a boundary edge are drawn as points. Point attributes such as
GL_POINT_SIZE and GL_POINT_SMOOTH control the rasterization of the points.
Polygon rasterization attributes other than GL_POLYGON_MODE have no effect. LINE
= 0x1B01, // Boundary edges of the polygon are drawn as line segments. Line
attributes such as GL_LINE_WIDTH and GL_LINE_SMOOTH control the rasterization of
the lines. Polygon rasterization attributes other than GL_POLYGON_MODE have no
effect. FILL = 0x1B02  // The interior of the polygon is filled. Polygon
attributes such as GL_POLYGON_SMOOTH control the rasterization of the polygon.
				};
				enum class ComparaisonAlgorithm {
						NEVER = 0x0200,
						LESS = 0x0201,
						EQUAL = 0x0202,
						LESS_EQUAL = 0x0203,
						GREATER = 0x0204,
						NOTEQUAL = 0x0205,
						GREATER_EQUAL = 0x0206,
						ALWAYS = 0x0207
				};

				enum class Operation {
						KEEP = 0x1E00,
						ZERO = 0,
						REPLACE = 0x1E01,
						INCREMENT = 0x1E02,
						INCREMENT_WRAP = 0x8507,
						DECREMENT = 0x1E03,
						DECREMENT_WRAP = 0x8508,
						INVERT = 0x150A
				};
				enum class CullFace {
						FRONT = 0x0404,
						BACK = 0x0405,
						FRONT_AND_BACK = 0x0408
				};
				enum class PixelDataType {
						BYTE = 0x1400,
						UNSIGNED_BYTE = 0x1401,
						BITMAP = 0x1A00,
						SHORT = 0x1402,
						UNSIGNED_SHORT = 0x1403,
						INT = 0x1404,
						UNSIGNED_INT = 0x1405,
						FLOAT = 0x1406,
						UNSIGNED_BYTE_3_3_2 = 0x8032,
						UNSIGNED_BYTE_2_3_3_REV = 0x8362,
						UNSIGNED_SHORT_5_6_5 = 0x8363,
						UNSIGNED_SHORT_5_6_5_REV = 0x8364,
						UNSIGNED_SHORT_4_4_4_4 = 0x8033,
						UNSIGNED_SHORT_4_4_4_4_REV = 0x8365,
						UNSIGNED_SHORT_5_5_5_1 = 0x8034,
						UNSIGNED_SHORT_1_5_5_5_REV = 0x8366,
						UNSIGNED_INT_8_8_8_8 = 0x8035,
						UNSIGNED_INT_8_8_8_8_REV = 0x8367,
						UNSIGNED_INT_10_10_10_2 = 0x8036,
						UNSIGNED_INT_2_10_10_10_REV = 0x8368
				};
				enum class PixelDataFormat {
						COLOR_INDEX = 0x1900,
						STENCIL_INDEX = 0x1901,
						DEPTH_COMPONENT = 0x1902,
						RED = 0x1903,
						GREEN = 0x1904,
						BLUE = 0x1905,
						ALPHA = 0x1906,
						RGB = 0x1907,
						BGR = 0x80E0,
						RGBA = 0x1908,
						BGRA = 0x80E1,
						LUMINANCE = 0x1909,
						LUMINANCE_ALPHA = 0x190A,
				};

				enum class CullingOptions {
						NONE = 0x0,
						FRUSTUM_PER_MODEL = 0x1,
						FRUSTUM_PER_MESH = 0x2
				};

				enum class BlendFactor {
						NONE = 0,
						ZERO,
						ONE,
						ONE_MINUS_SRC_COLOR,
						SRC_ALPHA,
						ONE_MINUS_SRC_ALPHA,
						DST_ALPHA,
						ONE_MINUS_DST_ALPHA,
						DST_COLOR,
						ONE_MINUS_DST_COLOR,
						CONSTANT_COLOR,
						ONE_MINUS_CONSTANT_COLOR,
						CONSTANT_ALPHA,
						ONE_MINUS_CONSTANT_ALPHA,
				};


				enum class DepthFunction {
						EQUAL = 0,
						NOT_EQUAL,
						LESS,
						GREATER,
						LESS_EQUAL,
						GREATER_EQUAL,
						ALWAYS,
						NEVER,
				};



				inline CullingOptions operator~ (CullingOptions a) { return
(CullingOptions)~(int)a; } inline CullingOptions operator| (CullingOptions a,
CullingOptions b) { return (CullingOptions)((int)a | (int)b); } inline
CullingOptions operator& (CullingOptions a, CullingOptions b) { return
(CullingOptions)((int)a & (int)b); } inline CullingOptions operator^
(CullingOptions a, CullingOptions b) { return (CullingOptions)((int)a ^ (int)b);
} inline CullingOptions& operator|= (CullingOptions& a, CullingOptions b) {
return (CullingOptions&)((int&)a |= (int)b); } inline CullingOptions& operator&=
(CullingOptions& a, CullingOptions b) { return (CullingOptions&)((int&)a &=
(int)b); } inline CullingOptions& operator^= (CullingOptions& a, CullingOptions
b) { return (CullingOptions&)((int&)a ^= (int)b); } inline bool
isFlagSet(CullingOptions flag, CullingOptions mask) { return (int)flag &
(int)mask; }

				class BaseRender {
				public:
						struct FrameInfo {
								uint64_t batchCount = 0;
								uint64_t instanceCount = 0;
								uint64_t polyCount = 0;
						};

						BaseRender(GL_SYSTEM::GlManager& driver);
						~BaseRender() = default;
						void setClearColor(float red, float green, float blue,
float alpha = 1.0f); void clear(bool colorBuffer = true, bool depthBuffer =
true, bool stencilBuffer = true); void setRasterizationLinesWidth(float width);
						void setRasterizationMode(RasterizationMode
rasterizationMode); void setCapability(RenderingCapability capability, bool
value);
						[[nodiscard]] bool getCapability(RenderingCapability
capability) const; void setStencilAlgorithm(ComparaisonAlgorithm algorithm,
int32_t reference, uint32_t mask); void setDepthAlgorithm(ComparaisonAlgorithm
algorithm); void setStencilMask(uint32_t mask); void
setStencilOperations(Operation stencilFail = Operation::KEEP, Operation
depthFail = Operation::KEEP, Operation bothPass = Operation::KEEP); void
setCullFace(CullFace cullFace); void setDepthWriting(bool enable); void
setColorWriting(bool enableRed, bool enableGreen, bool enableBlue, bool
enableAlpha); void setColorWriting(bool enable); void setViewPort(uint32_t x,
uint32_t y, uint32_t width, uint32_t height); void readPixels(uint32_t x,
uint32_t y, uint32_t width, uint32_t height, PixelDataFormat format,
PixelDataType type, void* data); bool getBool(GLenum parameter); bool
getBool(GLenum parameter, uint32_t index); int getInt(GLenum parameter); int
getInt(GLenum parameter, uint32_t index); float getFloat(GLenum parameter);
						float getFloat(GLenum parameter, uint32_t index);
						double getDouble(GLenum parameter);
						double getDouble(GLenum parameter, uint32_t index);
						int64_t getInt64(GLenum parameter);
						int64_t getInt64(GLenum parameter, uint32_t index);
						std::string getString(GLenum parameter);
						std::string getString(GLenum parameter, uint32_t index);
						void clearFrameInfo();
						void draw(const RESOURCES::Mesh & mesh, PrimitiveMode
primitiveMode = PrimitiveMode::TRIANGLES, uint32_t instances = 1); uint8_t
fetchGLState(); void applyStateMask(uint8_t mask); void setState(uint8_t state);
						[[nodiscard]] const FrameInfo& getFrameInfo() const;
						void useDepthBufferMask(bool value);
						void useBlendFactors(BlendFactor src, BlendFactor dist);
						void useReversedDepth(bool value);
						void useDepthFunction(DepthFunction function);
						void useCulling(bool value, bool counterClockWise, bool
cullBack); void drawIndices(PrimitiveMode primitive, size_t indexCount, size_t
indexOffset); void drawIndicesBaseVertex(PrimitiveMode primitive, size_t
indexCount, size_t indexOffset, size_t baseVertex); void
drawIndicesBaseVertexInstanced(PrimitiveMode primitive, size_t indexCount,
size_t indexOffset, size_t baseVertex, size_t instanceCount, size_t
baseInstance); void setPatchSize(int sz) const; private: GL_SYSTEM::GlManager&
driver; FrameInfo frameInfo; uint8_t state;
				};
		}
}
*/