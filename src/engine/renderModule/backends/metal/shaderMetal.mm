#ifdef METAL_BACKEND

#include "shaderMetal.h"
#include "gpuMemoryMetal.h"

#include <cstring>
#include <exception>
#include <map>
#include <vector>
#include <TargetConditionals.h>

#include "deviceMetal.h"
#include "spirv_msl.hpp"

#include "resourceModule/fileSystem/fileSystem.h"
#include "resourceModule/serviceManager.h"
#include "utilsModule/log/loggerDefine.h"
#include "../interface/resourceStruct.h"
#include "../interface/driverInterface.h"

namespace IKIGAI::RENDER {

	static bool IsSpirvBytes(const std::string& source) {
		if (source.size() < 4) {
			return false;
		}
		uint32_t magic = 0;
		std::memcpy(&magic, source.data(), sizeof(magic));
		return magic == 0x07230203u;
	}

	static spv::ExecutionModel ExecutionModelFor(ShaderType stage) {
		switch (stage) {
		case ShaderType::VERTEX: return spv::ExecutionModelVertex;
		case ShaderType::FRAGMENT: return spv::ExecutionModelFragment;
		case ShaderType::GEOMETRY: return spv::ExecutionModelGeometry;
		case ShaderType::TESSELLATION_CONTROL: return spv::ExecutionModelTessellationControl;
		case ShaderType::TESSELLATION_EVALUATION: return spv::ExecutionModelTessellationEvaluation;
		case ShaderType::COMPUTE: return spv::ExecutionModelGLCompute;
		default: return spv::ExecutionModelMax;
		}
	}

	static bool IsBufferUniform(ShaderReflection::UniformType type) {
		return type == ShaderReflection::UniformType::UNIFORM_BUFFER
			|| type == ShaderReflection::UniformType::STORAGE_BUFFER;
	}

	static bool IsTextureUniform(ShaderReflection::UniformType type) {
		switch (type) {
		case ShaderReflection::UniformType::SAMPLER_2D:
		case ShaderReflection::UniformType::SAMPLER_CUBE:
		case ShaderReflection::UniformType::SAMPLER_3D:
		case ShaderReflection::UniformType::SAMPLER_2D_ARRAY:
		case ShaderReflection::UniformType::IMAGE_2D:
		case ShaderReflection::UniformType::IMAGE_3D:
		case ShaderReflection::UniformType::IMAGE_2D_ARRAY:
		case ShaderReflection::UniformType::IMAGE_CUBE:
			return true;
		default:
			return false;
		}
	}

	static MTLTessellationPartitionMode PartitionMode(const spirv_cross::SPIREntryPoint& entry) {
		if (entry.flags.get(spv::ExecutionModeSpacingEqual)) {
			return MTLTessellationPartitionModeInteger;
		}
		if (entry.flags.get(spv::ExecutionModeSpacingFractionalEven)) {
			return MTLTessellationPartitionModeFractionalEven;
		}
		return MTLTessellationPartitionModeFractionalOdd;
	}

	id<MTLFunction> ShaderMetal::rasterVertexFunction() const {
		if (mTessEvalFunction) {
			return mTessEvalFunction;
		}
		if (mVertexFunction && mVertexFunction.functionType == MTLFunctionTypeVertex) {
			return mVertexFunction;
		}
		return nil;
	}

	ShaderMetal::ShaderMetal(const ShaderResource& res) {
		create(res);
	}

	ShaderMetal::ShaderMetal(const std::string& vertexPath, const std::string& fragmentPath) {
		ShaderResource res;
		res.paths[ShaderResource::toStr.at(ShaderType::VERTEX)] = vertexPath;
		res.paths[ShaderResource::toStr.at(ShaderType::FRAGMENT)] = fragmentPath;
		create(res);
	}

	ShaderMetal::~ShaderMetal() = default;

	void ShaderMetal::bind() {}
	void ShaderMetal::unbind() {}

	void ShaderMetal::setUniform(const std::string&, int) {}
	void ShaderMetal::setUniform(const std::string&, float) {}
	void ShaderMetal::setUniform(const std::string&, const MATH::Vector2f&) {}
	void ShaderMetal::setUniform(const std::string&, const MATH::Vector3f&) {}
	void ShaderMetal::setUniform(const std::string&, const MATH::Vector4f&) {}
	void ShaderMetal::setUniform(const std::string&, const MATH::Matrix3f&) {}
	void ShaderMetal::setUniform(const std::string&, const MATH::Matrix4f&) {}
	void ShaderMetal::setUniform(const std::string&, bool) {}
	void ShaderMetal::setUniform(const std::string&, const std::vector<MATH::Matrix4f>&) {}

	void ShaderMetal::recompile(const ShaderResource& res) {
		requireOwnerThread();
		if (!res.sources.empty()) {
			res.spirvSources.clear();
		}
		create(res);
	}

	void ShaderMetal::create(const ShaderResource& res) {
    configureResourceInterface(res);
    if (!res.sources.empty() && !res.useBinary) res.spirvSources.clear();
        if (bindingMode()==ShaderBindingMode::Bindless) {
            auto supported=[](ShaderType stage){return stage==ShaderType::VERTEX || stage==ShaderType::FRAGMENT || stage==ShaderType::COMPUTE;};
            for (const auto& [stage,source] : res.sources) if (!supported(stage)) throw std::runtime_error("Metal bindless profile supports vertex/fragment/compute stages");
            for (const auto& [stage,source] : res.spirvSources) if (!supported(stage)) throw std::runtime_error("Metal bindless profile supports vertex/fragment/compute stages");
        }
		static size_t nextId = 1;
		mId = nextId++;
		mGeneration++;
		mPath = res.path;
		mShaderPaths = res.getPaths();
		mReflection = ShaderReflection();
		mSamplerBindings.clear();mArgumentIds.clear();
		mVertexFunction = nil;
		mFragmentFunction = nil;
        mComputeFunction = nil;
		mGeometryFunction = nil;
		mTessControlFunction = nil;
		mTessEvalFunction = nil;
		mTessVertexFunction = nil;
		mTessVertexIndexedFunction = nil;
		mTessControlPoints = 3;
		mTessTriangles = true;
		mTessPartition = MTLTessellationPartitionModeFractionalOdd;

		if (res.spirvSources.empty() && res.sources.empty() && !res.paths.empty()) {
			auto& fs = RESOURCES::ServiceManager::Get<RESOURCES::FileSystem>();
			for (const auto& [key, path] : res.paths) {
				if (path.empty() || !ShaderResource::toEnum.contains(key)) {
					continue;
				}
				auto file = fs.getFile(path);
				if (!file || !file->isOpened()) {
					LOG_ERROR << "Metal shader: failed to load " << path;
					continue;
				}
				const auto bytes = file->read();
				res.sources[ShaderResource::toEnum.at(key)] = std::string(bytes.begin(), bytes.end());
			}
		}

		std::map<ShaderType, std::vector<uint32_t>> spirv = res.spirvSources;
		if (spirv.empty()) {
			for (const auto& [type, source] : res.sources) {
				if (source.empty()) {
					continue;
				}
				if (IsSpirvBytes(source)) {
					std::vector<uint32_t> words(source.size() / sizeof(uint32_t));
					std::memcpy(words.data(), source.data(), words.size() * sizeof(uint32_t));
					spirv[type] = std::move(words);
				} else {
					spirv[type] = CompileGlslToSpirv(type, source, {});
				}
			}
		}

		for (const auto& [type, words] : spirv) {
			if (!words.empty()) {
				GetReflection(mReflection, words, type);
			}
		}

		const bool tessellation = (spirv.contains(ShaderType::TESSELLATION_CONTROL) && !spirv[ShaderType::TESSELLATION_CONTROL].empty())
			|| (spirv.contains(ShaderType::TESSELLATION_EVALUATION) && !spirv[ShaderType::TESSELLATION_EVALUATION].empty());

		if (spirv.contains(ShaderType::VERTEX) && !spirv[ShaderType::VERTEX].empty()) {
			if (tessellation) {
				const auto linear = compileStage(spirv[ShaderType::VERTEX], ShaderType::VERTEX, true, false);
				const auto indexed = compileStage(spirv[ShaderType::VERTEX], ShaderType::VERTEX, true, true);
				mTessVertexFunction = linear.function;
				mTessVertexIndexedFunction = indexed.function;
			} else {
				mVertexFunction = compileStage(spirv[ShaderType::VERTEX], ShaderType::VERTEX, false, false).function;
			}
		}
		if (spirv.contains(ShaderType::TESSELLATION_CONTROL) && !spirv[ShaderType::TESSELLATION_CONTROL].empty()) {
			const auto stage = compileStage(spirv[ShaderType::TESSELLATION_CONTROL], ShaderType::TESSELLATION_CONTROL, false, false);
			mTessControlFunction = stage.function;
			if (stage.outputVertices > 0) {
				mTessControlPoints = stage.outputVertices;
			}
			mTessTriangles = stage.triangles;
			mTessPartition = stage.partition;
		}
		if (spirv.contains(ShaderType::TESSELLATION_EVALUATION) && !spirv[ShaderType::TESSELLATION_EVALUATION].empty()) {
			const auto stage = compileStage(spirv[ShaderType::TESSELLATION_EVALUATION], ShaderType::TESSELLATION_EVALUATION, false, false);
			mTessEvalFunction = stage.function;
			mTessTriangles = stage.triangles;
			mTessPartition = stage.partition;
			if (mTessControlPoints == 3 && stage.outputVertices > 0) {
				mTessControlPoints = stage.outputVertices;
			}
		}
		if (spirv.contains(ShaderType::GEOMETRY) && !spirv[ShaderType::GEOMETRY].empty()) {
			mGeometryFunction = compileStage(spirv[ShaderType::GEOMETRY], ShaderType::GEOMETRY, false, false).function;
		}
		if (spirv.contains(ShaderType::COMPUTE)) mComputeFunction=compileStage(spirv[ShaderType::COMPUTE],ShaderType::COMPUTE,false,false).function;
		if (spirv.contains(ShaderType::FRAGMENT) && !spirv[ShaderType::FRAGMENT].empty()) {
			mFragmentFunction = compileStage(spirv[ShaderType::FRAGMENT], ShaderType::FRAGMENT, false, false).function;
		}
	}

	ShaderMetal::StageResult ShaderMetal::compileStage(const std::vector<uint32_t>& spirv, ShaderType stage, bool vertexForTessellation, bool indexedVertex) {
		StageResult result;
		id<MTLDevice> device = DeviceMetal::Get();
		if (!device || spirv.empty()) {
			return result;
		}
		const auto model = ExecutionModelFor(stage);
		if (model == spv::ExecutionModelMax) {
			return result;
		}

		try {
			spirv_cross::CompilerMSL compiler(spirv);
			spirv_cross::CompilerMSL::Options options;
			options.set_msl_version(2, 4);
            if (bindingMode()==ShaderBindingMode::Bindless) {
                options.argument_buffers=true;
                options.argument_buffers_tier=spirv_cross::CompilerMSL::Options::ArgumentBuffersTier::Tier2;
                options.force_active_argument_buffer_resources=true;
            }
#if TARGET_OS_IPHONE
			options.platform = spirv_cross::CompilerMSL::Options::iOS;
#else
			options.platform = spirv_cross::CompilerMSL::Options::macOS;
#endif
			options.swizzle_buffer_index = 15;
			options.buffer_size_buffer_index = kMetalBufferSizeBufferIndex;
			options.shader_patch_input_buffer_index = kMetalShaderPatchInputBufferIndex;
			options.shader_index_buffer_index = kMetalShaderIndexBufferIndex;
			options.shader_input_buffer_index = kMetalShaderInputBufferIndex;
			options.shader_tess_factor_buffer_index = kMetalTessFactorBufferIndex;
			options.shader_patch_output_buffer_index = kMetalPatchOutputBufferIndex;
			options.shader_output_buffer_index = kMetalShaderOutputBufferIndex;
			options.indirect_params_buffer_index = kMetalIndirectParamsBufferIndex;
			options.vertex_for_tessellation = vertexForTessellation;
			options.multi_patch_workgroup = stage == ShaderType::TESSELLATION_CONTROL;
			options.raw_buffer_tese_input = stage == ShaderType::TESSELLATION_EVALUATION;
			if (vertexForTessellation) {
				options.vertex_index_type = indexedVertex
					? spirv_cross::CompilerMSL::Options::IndexType::UInt32
					: spirv_cross::CompilerMSL::Options::IndexType::None;
			}
			compiler.set_msl_options(options);

			const auto stages = compiler.get_entry_points_and_stages();
			if (!stages.empty()) {
				const auto& entry = compiler.get_entry_point(stages[0].name, stages[0].execution_model);
				result.outputVertices = entry.output_vertices;
				result.triangles = !entry.flags.get(spv::ExecutionModeQuads);
				result.partition = PartitionMode(entry);
			}

			std::vector<uint32_t> samplerBindings;
			for (const auto& image : compiler.get_shader_resources().sampled_images)
				samplerBindings.push_back(compiler.get_decoration(image.id, spv::DecorationBinding));
			mSamplerBindings[stage] = MetalSamplerBindings(std::move(samplerBindings));
			bool pushBound = false;
            uint32_t argumentCursor=0;
			for (const auto& uniform : mReflection.mUniforms) {
				if ((uniform.mShaderMask & static_cast<size_t>(stage)) == 0) {
					continue;
				}
				spirv_cross::MSLResourceBinding binding{};
				binding.stage = model;
				binding.count = descriptorCount(uniform);
				if (uniform.mType == ShaderReflection::UniformType::PUSH_CONSTANT) {
					if (pushBound) {
						continue;
					}
					binding.desc_set = spirv_cross::kPushConstDescSet;
					binding.binding = spirv_cross::kPushConstBinding;
					binding.msl_buffer = kMetalPushConstantBufferIndex;
					compiler.add_msl_resource_binding(binding);
					pushBound = true;
					continue;
				}
				if (!IsBufferUniform(uniform.mType) && !IsTextureUniform(uniform.mType)) {
					continue;
				}
                if (bindingMode()==ShaderBindingMode::Bindless) {
                    if (uniform.mSet!=0) throw std::runtime_error("Metal bindless ABI requires descriptor set zero");
                    if (uniform.mRuntimeArray) throw std::runtime_error("Metal bindless profile requires bounded arrays");
                    if (isBindlessResource(uniform)) {
                        const auto caps=DriverInterface::Get()->bindlessCapabilities();
                        const bool sampled=uniform.mType==ShaderReflection::UniformType::SAMPLER_2D ||
                            uniform.mType==ShaderReflection::UniformType::SAMPLER_CUBE ||
                            uniform.mType==ShaderReflection::UniformType::SAMPLER_3D ||
                            uniform.mType==ShaderReflection::UniformType::SAMPLER_2D_ARRAY;
                        if ((!sampled && uniform.mType!=ShaderReflection::UniformType::STORAGE_BUFFER) ||
                            descriptorCount(uniform)>(sampled?caps.maxSampledTextures:caps.maxStorageBuffers))
                            throw std::runtime_error("Unsupported Metal bindless table type/capacity");
                    }
                    ArgumentIds ids;ids.count=descriptorCount(uniform);ids.resource=argumentCursor;argumentCursor+=ids.count;
                    if (IsTextureUniform(uniform.mType)) {ids.sampler=argumentCursor;argumentCursor+=ids.count;}
                    mArgumentIds[stage][static_cast<uint32_t>(uniform.mBind)]=ids;
                    binding.desc_set=0;binding.binding=uniform.mBind;
                    binding.msl_buffer=ids.resource;binding.msl_texture=ids.resource;binding.msl_sampler=ids.sampler;
                    compiler.add_msl_resource_binding(binding);
                    continue;
                }
				binding.desc_set = static_cast<uint32_t>(uniform.mSet);
				binding.binding = static_cast<uint32_t>(uniform.mBind);
				if (IsBufferUniform(uniform.mType)) {
					binding.msl_buffer = MetalBufferIndexForBinding(static_cast<uint32_t>(uniform.mBind));
				} else {
					binding.msl_texture = static_cast<uint32_t>(uniform.mBind);
					binding.msl_sampler = samplerBinding(stage, static_cast<uint32_t>(uniform.mBind)).value_or(0);
				}
				compiler.add_msl_resource_binding(binding);
			}

			            if (bindingMode()==ShaderBindingMode::Bindless) {
                spirv_cross::MSLResourceBinding arguments{};arguments.stage=model;arguments.desc_set=0;
                arguments.binding=spirv_cross::kArgumentBufferBinding;arguments.msl_buffer=kMetalBindlessArgumentBufferIndex;
                compiler.add_msl_resource_binding(arguments);
            }
            std::string msl = compiler.compile();
            if (bindingMode()==ShaderBindingMode::Bindless && compiler.needs_buffer_size_buffer())
                throw std::runtime_error("Metal bindless buffer length queries are unsupported");
			if (stage == ShaderType::GEOMETRY) {
				const auto unknown = msl.find("unknown ");
				if (unknown != std::string::npos) {
					msl.replace(unknown, std::char_traits<char>::length("unknown "), "kernel ");
				}
			}

			NSError* error = nil;
			MTLCompileOptions* compileOptions = [[MTLCompileOptions alloc] init];
			compileOptions.languageVersion = MTLLanguageVersion2_4;
			NSString* source = [NSString stringWithUTF8String:msl.c_str()];
			id<MTLLibrary> library = [device newLibraryWithSource:source options:compileOptions error:&error];
			if (!library) {
				const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
				LOG_ERROR << "Metal shader compile failed (" << mPath << "): " << message;
				return result;
			}

			NSString* picked = nil;
			for (NSString* name in library.functionNames) {
				if ([name containsString:@"main"]) {
					picked = name;
					break;
				}
			}
			if (!picked && library.functionNames.count > 0) {
				picked = library.functionNames.firstObject;
			}
			if (!picked) {
				LOG_ERROR << "Metal shader '" << mPath << "' produced no functions";
				return result;
			}
			result.function = [library newFunctionWithName:picked];
#if IKIGAI_ENABLE_MEMORY_TRACKING
            TrackGpuObjectMetal(result.function, device, PROFILER::GpuResourceKind::Shader, mPath.c_str());
#endif
			return result;
		} catch (const std::exception& error) {
			LOG_ERROR << "SPIRV-Cross failed for '" << mPath << "': " << error.what();
			return result;
		}
	}

}

#endif
