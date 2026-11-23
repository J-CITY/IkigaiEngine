#ifdef METAL_BACKEND
#include "driverMetal.h"
#include "gpuMemoryMetal.h"
#if USE_RAY_TRACING
#include "rayAsMetal.h"
#endif
#include "spirv_msl.hpp"
#include "spirv_reflect.h"
#include <TargetConditionals.h>
#include <algorithm>
#include <limits>
#include <set>

namespace IKIGAI::RENDER {
namespace {

constexpr auto ComputeStage = spv::ExecutionModelGLCompute;
// Compute encoders have their own slot namespace. Keep the existing Metal
// push/length ABI and reserve one slot for indexed sampled images.
constexpr uint32_t ArrayTableSlot = kMetalIndirectParamsBufferIndex;
constexpr uint32_t BufferSlots = kMetalBufferSizeBufferIndex;

struct ProgramBinding {
	std::string name, address;
	SpvReflectDescriptorType type;
	uint32_t count, index = 0, sampler = 0;
	bool inTable = false;
	SpvReflectImageTraits image;
	SpvReflectDecorationFlags qualifiers;
	uint32_t minimumBytes;
};

class ProgramMetal final : public RayTracingProgram {
public:
	bool isCompute() const override { return true; }
	bool isRayQuery() const override { return query; }

	bool query = false, needsBufferLengths = false;
	id<MTLComputePipelineState> pipeline = nil;
	id<MTLArgumentEncoder> arrayEncoder = nil;
	uint32_t pushBytes = 0;
	MTLSize localSize {};
	std::vector<ProgramBinding> bindings;
};

[[noreturn]] void unsupported(bool query, const std::string& message) {
	if (query) throw UnsupportedRayTracing(message);
	throw UnsupportedGpuFeature(message);
}

struct ReflectedModule {
	explicit ReflectedModule(const std::vector<uint32_t>& words) {
		if (spvReflectCreateShaderModule(words.size() * sizeof(uint32_t), words.data(), &data) != SPV_REFLECT_RESULT_SUCCESS)
			throw std::runtime_error("Invalid SPIR-V module in Metal program");
	}
	~ReflectedModule() { spvReflectDestroyShaderModule(&data); }
	SpvReflectShaderModule data {};
};

std::shared_ptr<ProgramMetal> createProgram(id<MTLDevice> device, const RayTracingProgramResource& resource, bool query) {
	if (!device) throw std::runtime_error("Metal device required to create compute program");
	if (resource.Shaders.size() != 1 || resource.Shaders.front().Stage != ShaderType::COMPUTE || !resource.Groups.empty())
		unsupported(query, "Metal supports a single compute module without shader binding table groups");
	const auto& shader = resource.Shaders.front();
	if (shader.Source.empty()) throw std::invalid_argument("Metal program shader source is empty: " + shader.Name);
	const bool tier2 = device.argumentBuffersSupport == MTLArgumentBuffersTier2;
	if (resource.RequiresDescriptorIndexing && !tier2)
		unsupported(query, "Metal indexed descriptors require Tier 2 argument buffers");
	if (resource.PushConstantSize % 4 || resource.PushConstantSize > 4096)
		throw std::invalid_argument("Metal program push constants require a multiple of four bytes up to 4096");

	auto words = TryDecodeSpirv(shader.Source);
	if (!words) {
		if (shader.Language != "GLSL" || shader.EntryPoint != "main")
			unsupported(query, "Metal source programs require a GLSL main entry point");
		words = CompileGlslToSpirv(ShaderType::COMPUTE, shader.Source, {}, true);
	}
	ReflectedModule reflected(*words);
	const auto& module = reflected.data;
	if (module.entry_point_count != 1 || module.shader_stage != SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT || shader.EntryPoint != module.entry_point_name)
		throw std::invalid_argument("Metal program requires a single matching compute entry point");
	for (uint32_t i = 0; i < module.push_constant_block_count; ++i) {
		const auto& block = module.push_constant_blocks[i];
		if (block.size > resource.PushConstantSize)
			throw std::invalid_argument("Metal program PushConstantSize is smaller than its reflected block");
	}

	auto program = std::make_shared<ProgramMetal>();
	program->query = query;
	program->pushBytes = resource.PushConstantSize;
	const auto local = module.entry_points[0].local_size;
	const auto limits = device.maxThreadsPerThreadgroup;
	if (!local.x || !local.y || !local.z || local.x > limits.width || local.y > limits.height || local.z > limits.depth)
		unsupported(query, "Metal program requires a fixed local size within the device limits");
	program->localSize = MTLSizeMake(local.x, local.y, local.z);

	spirv_cross::CompilerMSL compiler(std::move(*words));
	const auto& capabilities = compiler.get_declared_capabilities();
	if (!query && std::find(capabilities.begin(), capabilities.end(), spv::CapabilityRayQueryKHR) != capabilities.end())
		throw UnsupportedRayTracing("Metal ray-query modules require createRayTracingProgram");
	spirv_cross::CompilerMSL::Options options;
	options.set_msl_version(2, 4);
#if TARGET_OS_IPHONE
	options.platform = spirv_cross::CompilerMSL::Options::iOS;
#endif
	options.buffer_size_buffer_index = kMetalBufferSizeBufferIndex;
	uint32_t buffers = 0, textures = 0, samplers = 0, argumentIds = 0, tableSamplers = 0;
	std::set<std::pair<uint32_t, uint32_t>> addresses;
	for (uint32_t i = 0; i < module.descriptor_binding_count; ++i) {
		const auto& desc = module.descriptor_bindings[i];
		if (!addresses.emplace(desc.set, desc.binding).second)
			throw std::invalid_argument("Duplicate set/binding in Metal program");
		if (desc.array.dims_count > 1 || !desc.count)
			unsupported(query, "Metal program descriptors require fixed, one-dimensional arrays");
		ProgramBinding binding { desc.name ? desc.name : "", std::to_string(desc.set) + ":" + std::to_string(desc.binding),
			desc.descriptor_type, desc.count, 0, 0, false, desc.image, desc.decoration_flags, desc.block.size };

		// External set/binding pairs stay in the immutable layout. Internally,
		// set 0 contains direct arguments and set 1 contains sampled arrays.
		// This lets SSBO .length() use the same direct size-buffer ABI on all GPUs.
		binding.inTable = tier2 && desc.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && desc.array.dims_count;
		spirv_cross::MSLResourceBinding native {};
		native.stage = ComputeStage;
		native.desc_set = binding.inTable ? 1 : 0;
		native.binding = i;
		native.count = desc.count;
		compiler.set_decoration(desc.spirv_id, spv::DecorationDescriptorSet, native.desc_set);
		compiler.set_decoration(desc.spirv_id, spv::DecorationBinding, native.binding);
		switch (desc.descriptor_type) {
		case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
			if (!query) throw UnsupportedRayTracing("Metal TLAS descriptors require a ray-query program");
			if (desc.array.dims_count) unsupported(query, "Metal query programs require individually bound TLAS descriptors");
			[[fallthrough]];
		case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			if (desc.count > BufferSlots - buffers) unsupported(query, "Metal program exceeds direct buffer slots");
			native.msl_buffer = binding.index = buffers;
			buffers += desc.count;
			break;
		case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			if (binding.inTable) {
				if (desc.count > device.maxArgumentBufferSamplerCount - tableSamplers || desc.count > (UINT32_MAX - argumentIds) / 2)
					unsupported(query, "Metal sampled array exceeds the argument buffer sampler limit");
				native.msl_texture = binding.index = argumentIds;
				argumentIds += desc.count;
				native.msl_sampler = binding.sampler = argumentIds;
				argumentIds += desc.count;
				tableSamplers += desc.count;
				break;
			}
			if (desc.count > 16 - samplers) unsupported(query, "Metal program exceeds direct sampler slots");
			native.msl_sampler = binding.sampler = samplers;
			samplers += desc.count;
			[[fallthrough]];
		case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			if (desc.count > 31 - textures) unsupported(query, "Metal program exceeds direct texture slots");
			native.msl_texture = binding.index = textures;
			textures += desc.count;
			break;
		default:
			unsupported(query, "Unsupported Metal program descriptor at " + binding.address);
		}
		compiler.add_msl_resource_binding(native);
		program->bindings.push_back(std::move(binding));
	}

	if (argumentIds) {
		options.argument_buffers = true;
		options.argument_buffers_tier = spirv_cross::CompilerMSL::Options::ArgumentBuffersTier::Tier2;
		options.force_active_argument_buffer_resources = true;
		compiler.add_discrete_descriptor_set(0);
		spirv_cross::MSLResourceBinding table {};
		table.stage = ComputeStage;
		table.desc_set = 1;
		table.binding = spirv_cross::kArgumentBufferBinding;
		table.msl_buffer = ArrayTableSlot;
		compiler.add_msl_resource_binding(table);
	}
	spirv_cross::MSLResourceBinding push {};
	push.stage = ComputeStage;
	push.desc_set = spirv_cross::kPushConstDescSet;
	push.binding = spirv_cross::kPushConstBinding;
	push.msl_buffer = kMetalPushConstantBufferIndex;
	compiler.add_msl_resource_binding(push);
	compiler.set_msl_options(options);
	std::string msl;
	try {
		msl = compiler.compile();
	} catch (const spirv_cross::CompilerError& error) {
		unsupported(query, "Metal lowering failed for " + shader.Name + ": " + error.what());
	}
	program->needsBufferLengths = compiler.needs_buffer_size_buffer();
	MTLCompileOptions* compileOptions = [[MTLCompileOptions alloc] init];
	if (@available(macOS 12.0, iOS 15.0, *)) {
		compileOptions.languageVersion = MTLLanguageVersion2_4;
	} else {
		unsupported(query, "Reflected Metal programs require MSL 2.4");
	}
	NSError* error = nil;
	id<MTLLibrary> library = [device newLibraryWithSource:[NSString stringWithUTF8String:msl.c_str()] options:compileOptions error:&error];
	if (!library) throw std::runtime_error("Metal compilation failed for " + shader.Name + ": " + (error.localizedDescription.UTF8String ?: "unknown error"));
	const auto entry = compiler.get_cleansed_entry_point_name(shader.EntryPoint, ComputeStage);
	id<MTLFunction> function = [library newFunctionWithName:[NSString stringWithUTF8String:entry.c_str()]];
	if (!function) throw std::runtime_error("Missing Metal compute function: " + entry);
	program->pipeline = [device newComputePipelineStateWithFunction:function error:&error];
	if (!program->pipeline) throw std::runtime_error("Metal compute pipeline failed for " + shader.Name + ": " + (error.localizedDescription.UTF8String ?: "unknown error"));
	if (uint64_t(local.x) * local.y * local.z > program->pipeline.maxTotalThreadsPerThreadgroup)
		unsupported(query, "Metal local size exceeds the compute pipeline limit");
	if (argumentIds) {
		program->arrayEncoder = [function newArgumentEncoderWithBufferIndex:ArrayTableSlot];
		if (!program->arrayEncoder) throw std::runtime_error("Metal sampled array argument encoder unavailable");
	}
#if IKIGAI_ENABLE_MEMORY_TRACKING
	TrackGpuObjectMetal(program->pipeline, device, PROFILER::GpuResourceKind::Pipeline, shader.Name.c_str());
#endif
	return program;
}

template <class Base, class Backend>
Backend* metalResource(const RayTracingResource& resource) {
	const auto* value = std::get_if<std::shared_ptr<Base>>(&resource);
	return value ? dynamic_cast<Backend*>(value->get()) : nullptr;
}

bool matchesTextureType(const SpvReflectImageTraits& image, id<MTLTexture> texture) {
	switch (image.dim) {
	case SpvDim1D: return texture.textureType == (image.arrayed ? MTLTextureType1DArray : MTLTextureType1D);
	case SpvDim2D:
		if (image.ms) return texture.textureType == (image.arrayed ? MTLTextureType2DMultisampleArray : MTLTextureType2DMultisample);
		return texture.textureType == (image.arrayed ? MTLTextureType2DArray : MTLTextureType2D);
	case SpvDim3D: return texture.textureType == MTLTextureType3D;
	case SpvDimCube: return texture.textureType == (image.arrayed ? MTLTextureTypeCubeArray : MTLTextureTypeCube);
	default: return false;
	}
}

struct BoundArgument {
	const ProgramBinding& layout;
	uint32_t element;
	id<MTLBuffer> buffer = nil;
	uint32_t bufferBytes = 0;
	id<MTLTexture> texture = nil;
	id<MTLSamplerState> sampler = nil;
#if USE_RAY_TRACING
	AccelerationStructureMetal* scene = nullptr;
#endif
};

BoundArgument resolve(const ProgramBinding& layout, uint32_t element, const RayTracingResource& resource, id<MTLDevice> device) {
	BoundArgument arg {layout, element};
	switch (layout.type) {
	case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		if (auto* buffer = metalResource<UniformBufferInterface, UniformBufferMetal>(resource)) {
			arg.buffer = buffer->getBuffer();
			if (arg.buffer.device == device && arg.buffer.length >= layout.minimumBytes) return arg;
		}
		break;
	case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
		size_t length = 0;
		if (auto* buffer = metalResource<StorageBufferInterface, StorageBufferMetal>(resource)) {
			arg.buffer = buffer->getBuffer();
			length = buffer->getSizeByte();
		} else if (auto* vertex = metalResource<VertexBufferInterface, VertexBufferMetal>(resource)) {
			arg.buffer = vertex->getBuffer();
			length = vertex->getSizeByte();
		}
		if (arg.buffer.device == device && length <= arg.buffer.length && length <= UINT32_MAX) {
			arg.bufferBytes = static_cast<uint32_t>(length);
			return arg;
		}
		break;
	}
#if USE_RAY_TRACING
	case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
		arg.scene = metalResource<AccelerationStructure, AccelerationStructureMetal>(resource);
		if (arg.scene && arg.scene->isTopLevel() && arg.scene->getDevice() == device && arg.scene->getHandle()) return arg;
		break;
#endif
	case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
	case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		if (auto* texture = metalResource<TextureInterface, TextureMetal>(resource)) {
			arg.texture = texture->getTexture();
			if (arg.texture.device != device || !matchesTextureType(layout.image, arg.texture)) break;
			if (layout.type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
				arg.sampler = texture->getSampler();
				if (!arg.sampler || !(arg.texture.usage & MTLTextureUsageShaderRead)) break;
			} else {
				if (!(layout.qualifiers & SPV_REFLECT_DECORATION_NON_WRITABLE) && !(arg.texture.usage & MTLTextureUsageShaderWrite)) break;
				if (!(layout.qualifiers & SPV_REFLECT_DECORATION_NON_READABLE) && !(arg.texture.usage & MTLTextureUsageShaderRead)) break;
				const auto format = arg.texture.pixelFormat;
				if ((layout.image.image_format == SpvImageFormatRgba32f && format != MTLPixelFormatRGBA32Float)
					|| (layout.image.image_format == SpvImageFormatRg32f && format != MTLPixelFormatRG32Float)
					|| (layout.image.image_format == SpvImageFormatR32f && format != MTLPixelFormatR32Float)) break;
			}
			return arg;
		}
		break;
	default: break;
	}
	throw std::invalid_argument("Invalid Metal program resource at " + layout.address + "[" + std::to_string(element) + "] (" + layout.name + ")");
}

} // namespace

std::shared_ptr<RayTracingProgram> DriverMetal::createComputeProgram(const RayTracingProgramResource& resource) {
	return createProgram(mDevice, resource, false);
}

void DriverMetal::dispatchComputeProgram(const RayTracingDispatch& dispatch) {
	executeMetalProgram(dispatch, false);
}

#if USE_RAY_TRACING
std::shared_ptr<RayTracingProgram> DriverMetal::createRayTracingProgram(const RayTracingProgramResource& resource) {
	const auto caps = rayTracingCapabilities();
	if (!caps.RayQuery) throw UnsupportedRayTracing("Metal ray queries are unavailable on this device");
	if (resource.RequiresDescriptorIndexing && !caps.DescriptorIndexing)
		throw UnsupportedRayTracing("Metal indexed ray-query descriptors are unavailable on this device");
	return createProgram(mDevice, resource, true);
}

void DriverMetal::dispatchRayQueries(const RayTracingDispatch& dispatch) {
	executeMetalProgram(dispatch, true);
}
#endif

void DriverMetal::executeMetalProgram(const RayTracingDispatch& dispatch, bool query) {
	requireOwnerThread();
	auto program = std::dynamic_pointer_cast<ProgramMetal>(dispatch.Program);
	if (!program || program->pipeline.device != mDevice || program->query != query)
		throw std::invalid_argument("Metal program kind or device disagrees with dispatch");
	if (dispatch.PushConstants.size() != program->pushBytes || dispatch.RaygenRecord)
		throw std::invalid_argument("Invalid Metal compute push constants or raygen record");
	const auto& extent = dispatch.Extent;
	if (!extent[0] || !extent[1] || !extent[2]) throw std::invalid_argument("Metal program dispatch requires a nonzero extent");
	const auto local = program->localSize;
	auto groups = [](uint32_t n, NSUInteger size) { return (uint64_t(n) + size - 1) / size; };
	const auto grid = MTLSizeMake(groups(extent[0], local.width), groups(extent[1], local.height), groups(extent[2], local.depth));
	checkComputeGrid(grid);

	std::vector<BoundArgument> arguments;
	for (const auto& binding : program->bindings) {
		auto found = dispatch.Bindings.find(binding.address);
		if (found == dispatch.Bindings.end() && !binding.name.empty()) found = dispatch.Bindings.find(binding.name);
		if (found == dispatch.Bindings.end() || found->second.size() != binding.count)
			throw std::invalid_argument("Missing Metal binding or incorrect array length at " + binding.address + " (" + binding.name + ")");
		for (uint32_t i = 0; i < binding.count; ++i) arguments.push_back(resolve(binding, i, found->second[i], mDevice));
	}

	if (!mCurrentCommandBuffer) begin();
	if (!mCurrentCommandBuffer) throw std::runtime_error("Metal program dispatch requires an active command buffer");
	id<MTLBuffer> arrayTable = nil;
	auto arrayEncoder = program->arrayEncoder;
	NSMutableArray<id<MTLSamplerState>>* indirectSamplers = nil;
	if (arrayEncoder) {
		arrayTable = AllocateBufferMetal(mDevice, arrayEncoder.encodedLength, MTLResourceStorageModeShared);
		if (!arrayTable) throw std::runtime_error("Metal sampled array allocation failed");
		[arrayEncoder setArgumentBuffer:arrayTable offset:0];
		indirectSamplers = [NSMutableArray array];
	}

	endPass();
	id<MTLComputeCommandEncoder> encoder = [mCurrentCommandBuffer computeCommandEncoder];
	if (!encoder) throw std::runtime_error("Metal compute encoder unavailable");
	struct EndEncoding {
		id<MTLComputeCommandEncoder> encoder;
		~EndEncoding() { [encoder endEncoding]; }
	} end {encoder};
	[encoder setComputePipelineState:program->pipeline];
	std::array<uint32_t, BufferSlots> lengths {};
	for (const auto& arg : arguments) {
		const uint32_t index = arg.layout.index + arg.element;
		if (arg.buffer) {
			NSUInteger offset = 0;
			const auto buffer = arg.layout.type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER ? snapshotUniform(arg.buffer, offset) : arg.buffer;
			[encoder setBuffer:buffer offset:offset atIndex:index];
			lengths[index] = arg.bufferBytes;
		} else if (arg.layout.inTable) {
			[arrayEncoder setTexture:arg.texture atIndex:index];
			[arrayEncoder setSamplerState:arg.sampler atIndex:arg.layout.sampler + arg.element];
			[encoder useResource:arg.texture usage:MTLResourceUsageRead];
			[indirectSamplers addObject:arg.sampler];
		} else if (arg.texture) {
			[encoder setTexture:arg.texture atIndex:index];
			if (arg.sampler) [encoder setSamplerState:arg.sampler atIndex:arg.layout.sampler + arg.element];
		}
#if USE_RAY_TRACING
		else if (arg.scene) {
			[encoder setAccelerationStructure:arg.scene->getHandle() atBufferIndex:index];
			for (id<MTLAccelerationStructure> bottom in arg.scene->getReferencedBottomLevels())
				[encoder useResource:bottom usage:MTLResourceUsageRead];
		}
#endif
	}
	if (arrayTable) [encoder setBuffer:arrayTable offset:0 atIndex:ArrayTableSlot];
	if (program->needsBufferLengths) [encoder setBytes:lengths.data() length:sizeof(lengths) atIndex:kMetalBufferSizeBufferIndex];
	if (program->pushBytes) [encoder setBytes:dispatch.PushConstants.data() length:program->pushBytes atIndex:kMetalPushConstantBufferIndex];
	// Direct bindings and useResource retain native resources on this command
	// buffer. Argument buffers do not retain their sampler objects.
	if (indirectSamplers) [mCurrentCommandBuffer addCompletedHandler:^(id<MTLCommandBuffer>) { (void)indirectSamplers.count; }];
	[encoder dispatchThreadgroups:grid threadsPerThreadgroup:local];
}

} // namespace IKIGAI::RENDER
#endif
