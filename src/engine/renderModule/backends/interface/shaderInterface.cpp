#include "shaderInterface.h"
#include "driverInterface.h"
#include "renderModule/vertex.h"
#include "resourceStruct.h"
#include "spirvCache.h"
#include "utilsModule/time/loadTimer.h"
#include <algorithm>
#include <cstdint>
#include <tuple>

void IKIGAI::RENDER::ShaderInterface::swapCompiledInterface(ShaderInterface& replacement) {
	// Materials own storage laid out by the original reflection. A reload may
	// move descriptor slots, while changing their data layout needs a rebuild.
	const auto compatible = [&] {
		if (mBindingMode != replacement.mBindingMode || mVertexFetchMode != replacement.mVertexFetchMode
			|| mBindlessCapacity != replacement.mBindlessCapacity || mReflection.mUniforms.size() != replacement.mReflection.mUniforms.size()) {
			return false;
		}
		const auto& next = replacement.mReflection;
		for (const auto& uniform : mReflection.mUniforms) {
			if (!next.mNameToUniforms.contains(uniform.mName)) {
				return false;
			}
			const auto& other = next.mUniforms.at(next.mNameToUniforms.at(uniform.mName));
			if (uniform.mType != other.mType || uniform.mSize != other.mSize || uniform.mDescriptorCount != other.mDescriptorCount
				|| uniform.mRuntimeArray != other.mRuntimeArray || uniform.mReadOnly != other.mReadOnly
				|| !std::ranges::equal(uniform.mMembers, other.mMembers, [](const auto& a, const auto& b) {
					   return std::tie(a.mName, a.mType, a.mOffset, a.mSize, a.mArraySize)
						   == std::tie(b.mName, b.mType, b.mOffset, b.mSize, b.mArraySize);
				   })) {
				return false;
			}
		}
		return true;
	};
	if (!compatible()) {
		throw std::invalid_argument("Shader interface changed during hot reload: " + mPath);
	}
	using std::swap;
	swap(mReflection, replacement.mReflection);
	swap(mBindingMode, replacement.mBindingMode);
	swap(mVertexFetchMode, replacement.mVertexFetchMode);
	swap(mBindlessCapacity, replacement.mBindlessCapacity);
	swap(mSupportsPbrLayers, replacement.mSupportsPbrLayers);
	swap(mCompiledPbrLayerCount, replacement.mCompiledPbrLayerCount);
	swap(mPbrLayerVariant, replacement.mPbrLayerVariant);
	swap(mPath, replacement.mPath);
	swap(mShaderPaths, replacement.mShaderPaths);
	++mGeneration;
}

void IKIGAI::RENDER::ShaderInterface::configureResourceInterface(const ShaderResource& res) {
	mSupportsPbrLayers = res.supportsPbrLayers;
	mCompiledPbrLayerCount = res.compiledPbrLayerCount;
	mPbrLayerVariant = res.pbrLayerVariant;
	mBindingMode = res.bindingMode;
	mVertexFetchMode = res.vertexFetchMode;
	mBindlessCapacity = res.bindlessCapacity;
	++mGeneration;
	if (mBindlessCapacity < 2) {
		throw std::invalid_argument("Bindless capacity must be positive");
	}
	if (mVertexFetchMode == VertexFetchMode::Pulling && (!DriverInterface::Get() || !DriverInterface::Get()->supportsVertexPulling())) {
		throw std::runtime_error("Vertex pulling requires storage buffer reads in the vertex stage");
	}
	if (mBindingMode == ShaderBindingMode::Bindless) {
		auto* driver = DriverInterface::Get();
		if (!driver || (!driver->bindlessCapabilities().sampledTextures && !driver->bindlessCapabilities().storageBuffers)) {
			throw std::runtime_error("Bindless shaders are unavailable on this backend");
		}
	}
}

#include "SPIRV/GlslangToSpv.h"
#include "glslang/Public/ShaderLang.h"
#include "spirv_glsl.hpp"
#include "spirv_hlsl.hpp"
#include "spirv_reflect.h"
#include "utilsModule/assertion.h"

void IKIGAI::RENDER::ShaderInterface::GetReflection(ShaderReflection& reflection, const std::vector<uint32_t>& shaderCode, ShaderType type) {
	SpvReflectShaderModule module;
	SpvReflectResult result = spvReflectCreateShaderModule(shaderCode.size() * sizeof(uint32_t), shaderCode.data(), &module);
	ASSERT_IF(result == SPV_REFLECT_RESULT_SUCCESS, "spvReflectCreateShaderModule error");

	if (type == ShaderType::COMPUTE && module.entry_point_count) {
		const auto local = module.entry_points[0].local_size;
		reflection.mLocalSize[0] = local.x;
		reflection.mLocalSize[1] = local.y;
		reflection.mLocalSize[2] = local.z;
	}
	for (auto i = 0; i < module.descriptor_set_count; i++) {
		const auto& descr = module.descriptor_sets[i];
		for (auto j = 0; j < descr.binding_count; j++) {
			const auto spvUniformInfo = descr.bindings[j];

			if (reflection.mNameToUniforms.contains(spvUniformInfo->name)) {
				reflection.mUniforms[reflection.mNameToUniforms.at(spvUniformInfo->name)].mShaderMask |= (size_t)type;
				continue;
			}

			ShaderReflection::Uniform uniform;
			uniform.mBind = spvUniformInfo->binding;
			uniform.mSet = spvUniformInfo->set;
			uniform.mName = spvUniformInfo->name;
			uniform.mShaderMask = (size_t)type;
			uniform.mDescriptorCount = spvUniformInfo->count;
			uniform.mRuntimeArray = spvUniformInfo->count == 0;
			uniform.mReadOnly = (spvUniformInfo->decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE) != 0;
			if (spvUniformInfo->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
				uniform.mType = ShaderReflection::UniformType::UNIFORM_BUFFER;
			}
			else if (spvUniformInfo->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
				uniform.mType = ShaderReflection::UniformType::STORAGE_BUFFER;
			}
			else if (spvUniformInfo->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
				uniform.mType = ShaderReflection::UniformType::SAMPLER_2D;
				if (spvUniformInfo->image.dim == SpvDimCube) {
					uniform.mType = ShaderReflection::UniformType::SAMPLER_CUBE;
				}
				else if (spvUniformInfo->image.dim == SpvDim3D) {
					uniform.mType = ShaderReflection::UniformType::SAMPLER_3D;
				}
				else if (spvUniformInfo->image.arrayed) {
					uniform.mType = ShaderReflection::UniformType::SAMPLER_2D_ARRAY;
				}
			}
			else if (spvUniformInfo->descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
				uniform.mType = spvUniformInfo->image.dim == SpvDim3D ? ShaderReflection::UniformType::IMAGE_3D
					: spvUniformInfo->image.dim == SpvDimCube		  ? ShaderReflection::UniformType::IMAGE_CUBE
					: spvUniformInfo->image.arrayed					  ? ShaderReflection::UniformType::IMAGE_2D_ARRAY
																	  : ShaderReflection::UniformType::IMAGE_2D;
			}
			else {
				throw std::runtime_error("Unsupported shader descriptor: " + uniform.mName);
			}
			// TODO: support array

			reflection.mNameToUniforms[uniform.mName] = reflection.mUniforms.size();

			size_t sz = 0;
			const auto* typeDesc = spvUniformInfo->type_description;
			const uint32_t memberCount = spvUniformInfo->block.member_count;
			for (uint32_t k = 0; k < memberCount; k++) {
				// name и тип берём из type_description->members, а не из block.members
				// это обходит возможное несоответствие sizeof(SpvReflectBlockVariable)
				const auto& memberType = typeDesc->members[k];
				const auto& memberBlock = spvUniformInfo->block.members[k];

				ShaderReflection::UniformMember m;
				m.mName = memberType.struct_member_name ? memberType.struct_member_name : "";
				m.mOffset = memberBlock.offset;
				m.mSize = memberBlock.size;
				sz += m.mSize;
				m.mArraySize = (memberBlock.array.dims_count == 0 ? 0 : memberBlock.array.dims[0]);

				auto memberOp = memberType.op;
				// Spirv convert 'bool' in UBO to 'int' so if we have 'bool' in UBO, need set 'Bool' prefix to member name
				if (memberOp == SpvOpTypeInt && m.mName.ends_with("Bool")) {
					memberOp = SpvOpTypeBool;
				}

				switch (memberOp) {
				case SpvOpTypeFloat:
					m.mType = ShaderReflection::UniformType::FLOAT;
					break;
				case SpvOpTypeInt:
					m.mType = ShaderReflection::UniformType::INT;
					break;
				case SpvOpTypeBool:
					m.mType = ShaderReflection::UniformType::BOOL;
					break;
				case SpvOpTypeVector:
					if (m.mSize == 16) {
						m.mType = ShaderReflection::UniformType::VEC4;
					}
					else if (m.mSize == 12) {
						m.mType = ShaderReflection::UniformType::VEC3;
					}
					else if (m.mSize == 8) {
						m.mType = ShaderReflection::UniformType::VEC2;
					}
					break;
				case SpvOpTypeMatrix:
					if (m.mSize == 16 * 4) {
						m.mType = ShaderReflection::UniformType::MAT4;
					}
					else if (m.mSize == 9 * 4) {
						m.mType = ShaderReflection::UniformType::MAT3;
					}
					break;
				default:
					m.mType = ShaderReflection::UniformType::NONE;
					break;
				}
				uniform.mMembers.push_back(m);
			}
			const size_t padded = spvUniformInfo->block.padded_size;
			uniform.mSize = padded > sz ? padded : sz;
			reflection.mUniforms.push_back(uniform);
		}
	}

	for (auto i = 0; i < module.push_constant_block_count; i++) {
		const auto& descr = module.push_constant_blocks[i];
		if (reflection.mNameToUniforms.contains(descr.name)) {
			reflection.mUniforms[reflection.mNameToUniforms.at(descr.name)].mShaderMask |= (size_t)type;
			continue;
		}
		ShaderReflection::Uniform uniform;
		uniform.mName = descr.name;
		uniform.mShaderMask = (size_t)type;

		uniform.mType = ShaderReflection::UniformType::PUSH_CONSTANT;
		reflection.mNameToUniforms[uniform.mName] = reflection.mUniforms.size();

		size_t sz = descr.size;
		const auto* typeDesc = descr.type_description;
		const uint32_t memberCount = descr.member_count;
		for (uint32_t k = 0; k < memberCount; k++) {
			const auto& member = descr.members[k];

			ShaderReflection::UniformMember m;
			m.mName = member.name;
			m.mOffset = member.offset;
			m.mSize = member.size;

			// TODO: array support
			// m.mArraySize = (memberBlock.array.dims_count == 0 ? 0 : memberBlock.array.dims[0]);
			switch (member.type_description->op) {
			case SpvOpTypeFloat:
				m.mType = ShaderReflection::UniformType::FLOAT;
				break;
			case SpvOpTypeInt:
				m.mType = ShaderReflection::UniformType::INT;
				break;
			case SpvOpTypeBool:
				m.mType = ShaderReflection::UniformType::BOOL;
				break;
			case SpvOpTypeVector:
				if (m.mSize == 16) {
					m.mType = ShaderReflection::UniformType::VEC4;
				}
				else if (m.mSize == 12) {
					m.mType = ShaderReflection::UniformType::VEC3;
				}
				else if (m.mSize == 8) {
					m.mType = ShaderReflection::UniformType::VEC2;
				}
				break;
			case SpvOpTypeMatrix:
				if (m.mSize == 16 * 4) {
					m.mType = ShaderReflection::UniformType::MAT4;
				}
				else if (m.mSize == 9 * 4) {
					m.mType = ShaderReflection::UniformType::MAT3;
				}
				break;
			default:
				m.mType = ShaderReflection::UniformType::NONE;
				break;
			}
			uniform.mMembers.push_back(m);
		}
		uniform.mSize = sz;
		reflection.mUniforms.push_back(uniform);
	}

	if (type == ShaderType::VERTEX) {
		for (auto i = 0; i < module.input_variable_count; i++) {
			auto& descr = module.input_variables[i];
			if (descr->decoration_flags & SPV_REFLECT_DECORATION_BUILT_IN) {
				continue;
			}
			ShaderReflection::InputParam member;
			member.mName = descr->name;
			member.mIndex = 0;
			member.mLocation = descr->location;
			const auto& numeric = descr->type_description->traits.numeric;
			member.mBitWidth = numeric.scalar.width;
			const bool integer = (descr->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_INT) != 0;
			member.mScalarType = integer ? (numeric.scalar.signedness ? VertexScalarType::Int : VertexScalarType::UInt) : VertexScalarType::Float;
			member.mComponents = numeric.vector.component_count ? numeric.vector.component_count : 1;
			if (descr->type_description->op == SpvOpTypeMatrix) {
				member.mComponents = numeric.matrix.row_count;
				member.mSize = member.mComponents * 4;
				for (uint32_t column = 0; column < numeric.matrix.column_count; ++column) {
					auto input = member;
					input.mLocation += column;
					reflection.mInputParams.push_back(input);
				}
				continue;
			}

			// TODO: add int vector support
			int size = 0;
			if (descr->type_description->op == SpvOp::SpvOpTypeVector) {
				bool isInt = (descr->type_description->type_flags & SPV_REFLECT_TYPE_FLAG_INT) != 0;
				if (descr->type_description->traits.numeric.vector.component_count == 2) {
					member.mFormat = isInt ? PixelFormat::RG32_INT : PixelFormat::RG_FLOAT;
					member.mSize = (isInt ? sizeof(int) : sizeof(float)) * 2;
					member.mOffset = size;
					size += member.mSize;
				}
				else if (descr->type_description->traits.numeric.vector.component_count == 3) {
					member.mFormat = isInt ? PixelFormat::RGB32_INT : PixelFormat::RGB_FLOAT;
					member.mSize = (isInt ? sizeof(int) : sizeof(float)) * 3;
					member.mOffset = size;
					size += member.mSize;
				}
				else if (descr->type_description->traits.numeric.vector.component_count == 4) {
					member.mFormat = isInt ? PixelFormat::RGBA32_INT : PixelFormat::RGBA_FLOAT;
					member.mSize = (isInt ? sizeof(int) : sizeof(float)) * 4;
					member.mOffset = size;
					size += member.mSize;
				}
			}
			else if (descr->type_description->op == SpvOp::SpvOpTypeMatrix) {
				// if (descr->type_description->traits.numeric.matrix.column_count == 3)
				// { 	member.type = VertexDescriptor::Type::MAT3; 	member.size =
				// sizeof(float) * 3 * 3; 	descriptor.size += sizeof(float) * 3 *
				// 3; }
				// else if (descr->type_description->traits.numeric.matrix.column_count
				// == 4) { 	member.type = VertexDescriptor::Type::MAT4;
				// member.size =
				// sizeof(float) * 4 * 4; 	descriptor.size += sizeof(float) * 4 *
				// 4;
				// }
			}
			else if (descr->type_description->op == SpvOp::SpvOpTypeFloat) {
				member.mFormat = PixelFormat::R_FLOAT;
				member.mSize = sizeof(float);
				member.mOffset = size;
				size += member.mSize;
			}
			else if (descr->type_description->op == SpvOp::SpvOpTypeInt) {
				member.mFormat = PixelFormat::R32_INT;
				member.mSize = sizeof(int);
				member.mOffset = size;
				size += member.mSize;
			}
			else if (descr->type_description->op == SpvOp::SpvOpTypeBool) {
				member.mFormat = PixelFormat::R32_INT;
				member.mSize = sizeof(bool);
				member.mOffset = size;
				size += member.mSize;
			}

			reflection.mInputParams.push_back(member);
		}

		std::sort(
			reflection.mInputParams.begin(), reflection.mInputParams.end(), [](const auto& a, const auto& b) { return a.mLocation < b.mLocation; });

		// Standard mesh locations keep the CPU Vertex layout even when a shader
		// omits normals/bones. In particular GUI color at location 6 is not the
		// third packed field after position and UV.
		const size_t vertexOffsets[] = { offsetof(IKIGAI::Vertex, position), offsetof(IKIGAI::Vertex, texCoord), offsetof(IKIGAI::Vertex, normal),
			offsetof(IKIGAI::Vertex, tangent), offsetof(IKIGAI::Vertex, bitangent), offsetof(IKIGAI::Vertex, m_BoneIDs),
			offsetof(IKIGAI::Vertex, m_Weights) };
		const size_t vertexSizes[] = { 12, 8, 12, 12, 12, 16, 16 };
		int sz = 0;
		for (auto& e : reflection.mInputParams) {
			e.mOffset = e.mLocation < 7 && e.mSize == vertexSizes[e.mLocation] ? vertexOffsets[e.mLocation] : sz;
			sz += e.mSize;
		}
	}
	// TODO:
	if (type == ShaderType::FRAGMENT) {
		auto attachmentsCount = module.output_variable_count;
		for (auto i = 0; i < attachmentsCount; i++) {
			auto a = module.output_variables[i];
			if (std::string(a->name) == "gl_FragDepth") {
				attachmentsCount--;
			}
		}
	}

	spvReflectDestroyShaderModule(&module);
}

const TBuiltInResource DefaultTBuiltInResource = {
	/* .maxLights = */ 128,
	/* .maxClipPlanes = */ 6,
	/* .maxTextureUnits = */ 32,
	/* .maxTextureCoords = */ 32,
	/* .maxVertexAttribs = */ 64,
	/* .maxVertexUniformComponents = */ 4096,
	/* .maxVaryingFloats = */ 64,
	/* .maxVertexTextureImageUnits = */ 32,
	/* .maxCombinedTextureImageUnits = */ 80,
	/* .maxTextureImageUnits = */ 32,
	/* .maxFragmentUniformComponents = */ 4096,
	/* .maxDrawBuffers = */ 32,
	/* .maxVertexUniformVectors = */ 128,
	/* .maxVaryingVectors = */ 8,
	/* .maxFragmentUniformVectors = */ 16,
	/* .maxVertexOutputVectors = */ 16,
	/* .maxFragmentInputVectors = */ 15,
	/* .minProgramTexelOffset = */ -8,
	/* .maxProgramTexelOffset = */ 7,
	/* .maxClipDistances = */ 8,
	/* .maxComputeWorkGroupCountX = */ 65535,
	/* .maxComputeWorkGroupCountY = */ 65535,
	/* .maxComputeWorkGroupCountZ = */ 65535,
	/* .maxComputeWorkGroupSizeX = */ 1024,
	/* .maxComputeWorkGroupSizeY = */ 1024,
	/* .maxComputeWorkGroupSizeZ = */ 64,
	/* .maxComputeUniformComponents = */ 1024,
	/* .maxComputeTextureImageUnits = */ 16,
	/* .maxComputeImageUniforms = */ 8,
	/* .maxComputeAtomicCounters = */ 8,
	/* .maxComputeAtomicCounterBuffers = */ 1,
	/* .maxVaryingComponents = */ 60,
	/* .maxVertexOutputComponents = */ 64,
	/* .maxGeometryInputComponents = */ 64,
	/* .maxGeometryOutputComponents = */ 128,
	/* .maxFragmentInputComponents = */ 128,
	/* .maxImageUnits = */ 8,
	/* .maxCombinedImageUnitsAndFragmentOutputs = */ 8,
	/* .maxCombinedShaderOutputResources = */ 8,
	/* .maxImageSamples = */ 0,
	/* .maxVertexImageUniforms = */ 0,
	/* .maxTessControlImageUniforms = */ 0,
	/* .maxTessEvaluationImageUniforms = */ 0,
	/* .maxGeometryImageUniforms = */ 0,
	/* .maxFragmentImageUniforms = */ 8,
	/* .maxCombinedImageUniforms = */ 8,
	/* .maxGeometryTextureImageUnits = */ 16,
	/* .maxGeometryOutputVertices = */ 256,
	/* .maxGeometryTotalOutputComponents = */ 1024,
	/* .maxGeometryUniformComponents = */ 1024,
	/* .maxGeometryVaryingComponents = */ 64,
	/* .maxTessControlInputComponents = */ 128,
	/* .maxTessControlOutputComponents = */ 128,
	/* .maxTessControlTextureImageUnits = */ 16,
	/* .maxTessControlUniformComponents = */ 1024,
	/* .maxTessControlTotalOutputComponents = */ 4096,
	/* .maxTessEvaluationInputComponents = */ 128,
	/* .maxTessEvaluationOutputComponents = */ 128,
	/* .maxTessEvaluationTextureImageUnits = */ 16,
	/* .maxTessEvaluationUniformComponents = */ 1024,
	/* .maxTessPatchComponents = */ 120,
	/* .maxPatchVertices = */ 32,
	/* .maxTessGenLevel = */ 64,
	/* .maxViewports = */ 16,
	/* .maxVertexAtomicCounters = */ 0,
	/* .maxTessControlAtomicCounters = */ 0,
	/* .maxTessEvaluationAtomicCounters = */ 0,
	/* .maxGeometryAtomicCounters = */ 0,
	/* .maxFragmentAtomicCounters = */ 8,
	/* .maxCombinedAtomicCounters = */ 8,
	/* .maxAtomicCounterBindings = */ 1,
	/* .maxVertexAtomicCounterBuffers = */ 0,
	/* .maxTessControlAtomicCounterBuffers = */ 0,
	/* .maxTessEvaluationAtomicCounterBuffers = */ 0,
	/* .maxGeometryAtomicCounterBuffers = */ 0,
	/* .maxFragmentAtomicCounterBuffers = */ 1,
	/* .maxCombinedAtomicCounterBuffers = */ 1,
	/* .maxAtomicCounterBufferSize = */ 16384,
	/* .maxTransformFeedbackBuffers = */ 4,
	/* .maxTransformFeedbackInterleavedComponents = */ 64,
	/* .maxCullDistances = */ 8,
	/* .maxCombinedClipAndCullDistances = */ 8,
	/* .maxSamples = */ 4,
	/* .maxMeshOutputVerticesNV = */ 2048,
	/* .maxMeshOutputPrimitivesNV = */ 4096,
	/* .maxMeshWorkGroupSizeX_NV = */ 32,
	/* .maxMeshWorkGroupSizeY_NV = */ 1,
	/* .maxMeshWorkGroupSizeZ_NV = */ 1,
	/* .maxTaskWorkGroupSizeX_NV = */ 32,
	/* .maxTaskWorkGroupSizeY_NV = */ 1,
	/* .maxTaskWorkGroupSizeZ_NV = */ 1,
	/* .maxMeshViewCountNV = */ 4,
	/* .maxMeshOutputVerticesEXT = */ 2048,
	/* .maxMeshOutputPrimitivesEXT = */ 4096,
	/* .maxMeshWorkGroupSizeX_EXT = */ 32,
	/* .maxMeshWorkGroupSizeY_EXT = */ 1,
	/* .maxMeshWorkGroupSizeZ_EXT = */ 1,
	/* .maxTaskWorkGroupSizeX_EXT = */ 32,
	/* .maxTaskWorkGroupSizeY_EXT = */ 1,
	/* .maxTaskWorkGroupSizeZ_EXT = */ 1,
	/* .maxMeshViewCountEXT = */ 4,
	/* .maxDualSourceDrawBuffersEXT = */ 1,
	/* .limits = */
	{
	/* .nonInductiveForLoops = */ true,
	/* .whileLoops = */ true,
	/* .doWhileLoops = */ true,
	/* .generalUniformIndexing = */ true,
	/* .generalAttributeMatrixVectorIndexing = */ true,
	/* .generalVaryingIndexing = */ true,
	/* .generalSamplerIndexing = */ true,
	/* .generalVariableIndexing = */ true,
	/* .generalConstantMatrixVectorIndexing = */ true,
	}
};

std::vector<uint32_t> IKIGAI::RENDER::CompileGlslToSpirv(
	IKIGAI::RENDER::ShaderType stage, const std::string& code, const std::vector<std::string>& defines) {
	static const std::unordered_map<IKIGAI::RENDER::ShaderType, EShLanguage> StageMap
		= { { IKIGAI::RENDER::ShaderType::VERTEX, EShLangVertex }, { IKIGAI::RENDER::ShaderType::FRAGMENT, EShLangFragment },
			  { IKIGAI::RENDER::ShaderType::GEOMETRY, EShLangGeometry }, { IKIGAI::RENDER::ShaderType::TESSELLATION_CONTROL, EShLangTessControl },
			  { IKIGAI::RENDER::ShaderType::TESSELLATION_EVALUATION, EShLangTessEvaluation }, { IKIGAI::RENDER::ShaderType::COMPUTE, EShLangCompute },
			  { IKIGAI::RENDER::ShaderType::RAYGEN, EShLangRayGen }, { IKIGAI::RENDER::ShaderType::MISS, EShLangMiss },
			  { IKIGAI::RENDER::ShaderType::CLOSEST_HIT, EShLangClosestHit }, { IKIGAI::RENDER::ShaderType::ANY_HIT, EShLangAnyHit },
			  { IKIGAI::RENDER::ShaderType::INTERSECTION, EShLangIntersect }, { IKIGAI::RENDER::ShaderType::CALLABLE, EShLangCallable } };

	IKIGAI::UTILS::LoadTimer timing("glsl", std::to_string(static_cast<int>(stage)));

	// The last member of an SSBO keeps a runtime .length() even when the array
	// is sized, so MoltenVK still asks for spvBufferSizeConstants. The GLSL file
	// stays unsized; only the macOS compile uses a fixed bound (MAX_LIGHTS).
	std::string appleCode;
	const std::string* source = &code;
#if defined(__APPLE__)
	appleCode = code;
	for (const auto* lightType : { "LightOGL", "Light" }) {
		const std::string runtimeLights = std::string(lightType) + " lights[]";
		const std::string fixedLights = std::string(lightType) + " lights[64]";
		for (size_t pos = 0; (pos = appleCode.find(runtimeLights, pos)) != std::string::npos; pos += fixedLights.size()) {
			appleCode.replace(pos, runtimeLights.size(), fixedLights);
		}
	}
	const std::string runtimeLength = "engine_Lights.lights.length()";
	const std::string fixedLength = "64";
	for (size_t pos = 0; (pos = appleCode.find(runtimeLength, pos)) != std::string::npos; pos += fixedLength.size()) {
		appleCode.replace(pos, runtimeLength.size(), fixedLength);
	}
	source = &appleCode;
#endif
	std::string preamble;

	for (auto define : defines) {
		preamble += "#define " + define + "\n";
	}

	const auto target = DriverInterface::settings.backend == RenderSettings::Backend::OPENGL ? glslang::EShTargetSpv_1_5 : glslang::EShTargetSpv_1_6;
	std::filesystem::path cachePath;
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__)
	if (!IKIGAI::Config::STORAGE_ROOT.empty())
#endif
	{
		const auto root = IKIGAI::Config::STORAGE_ROOT.empty()
			? IKIGAI::RESOURCES::STORAGE::AppDataPath(IKIGAI::Config::ORGANIZATION, IKIGAI::Config::APPLICATION)
			: std::filesystem::u8path(IKIGAI::Config::STORAGE_ROOT);
		const auto version = glslang::GetVersion();
		const auto compiler = std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
		const auto key = IKIGAI::RENDER::SHADER_CACHE::Key(compiler, static_cast<uint32_t>(stage), static_cast<uint32_t>(target), *source, preamble);
		cachePath = root / "cache" / "spirv" / (key + ".bin");
		auto cached = IKIGAI::RENDER::SHADER_CACHE::Read(cachePath);
		if (!cached.empty()) {
			if (IKIGAI::Config::PROFILE_LOADING) {
				std::cout << "LOAD_SHADER_CACHE hit=1 key=" << key << std::endl;
			}
			return cached;
		}
		if (IKIGAI::Config::PROFILE_LOADING) {
			std::cout << "LOAD_SHADER_CACHE hit=0 key=" << key << std::endl;
		}
	}
	// Keep glslang's built-in symbol tables between stages. Finalizing after
	// every stage forced them to be rebuilt for every shader on every launch.
	static const struct CompilerProcess {
		CompilerProcess() {
			if (!glslang::InitializeProcess()) {
				throw std::runtime_error("Cannot initialize GLSL compiler");
			}
		}
		~CompilerProcess() {
			glslang::FinalizeProcess();
		}
	} compilerProcess;
	const auto _stage = StageMap.at(stage);
	glslang::TShader shader(_stage);
	const auto str = source->c_str();
	shader.setStrings(&str, 1);
	shader.setPreamble(preamble.c_str());
	// The bundled SPIRV-Cross only emits SPIR-V 1.6 demote for Vulkan GLSL.
	// GLSL discard becomes OpKill in 1.5, which is portable to desktop GL/GLES.
	shader.setEnvTarget(glslang::EShTargetSpv, target);

	auto messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules | EShMsgEnhanced);

	if (!shader.parse(&DefaultTBuiltInResource, 100, false, messages)) {
		auto info_log = shader.getInfoLog();
		throw std::runtime_error(info_log);
	}

	glslang::TProgram program;
	program.addShader(&shader);

	if (!program.link(messages)) {
		auto info_log = shader.getInfoLog();
		throw std::runtime_error(info_log);
	}

	auto intermediate = program.getIntermediate(_stage);

	std::vector<uint32_t> result;
	glslang::GlslangToSpv(*intermediate, result);
	if (!cachePath.empty()) {
		IKIGAI::RENDER::SHADER_CACHE::Write(cachePath, result);
	}

	return result;
}

std::string IKIGAI::RENDER::CompileSpirvToHlsl(const std::vector<uint32_t>& spirv, uint32_t version) {
	auto compiler = spirv_cross::CompilerHLSL(spirv);

	spirv_cross::CompilerHLSL::Options options;
	options.shader_model = version;
	options.flatten_matrix_vertex_input_semantics = true;
	compiler.set_hlsl_options(options);

	return compiler.compile();
}

namespace {
// GLSL ES 3.00 (WebGL2) has no SSBOs or std430. Cap unsized SSBO arrays so
// they can be emitted as std140 UBO members. Keep in sync with RENDER::MAX_LIGHTS.
constexpr uint32_t kEsSsboRuntimeArraySize = 64;

class EsCompilerGLSL : public spirv_cross::CompilerGLSL {
public:
	explicit EsCompilerGLSL(std::vector<uint32_t> spirv)
		: CompilerGLSL(std::move(spirv)) { }

	void RemapStorageBuffersForEs300() {
		auto resources = get_shader_resources();

		auto setBlockName = [this](const spirv_cross::Resource& resource) {
			auto name = get_name(resource.id);
			if (name.empty()) {
				name = resource.name;
			}
			if (!name.empty()) {
				set_name(resource.base_type_id, name);
			}
		};

		for (const auto& resource : resources.storage_buffers) {
			auto& var = get<spirv_cross::SPIRVariable>(resource.id);
			var.storage = spv::StorageClassUniform;

			auto& type = get<spirv_cross::SPIRType>(resource.base_type_id);
			unset_decoration(type.self, spv::DecorationBufferBlock);
			set_decoration(type.self, spv::DecorationBlock);
			setBlockName(resource);

			for (auto memberTypeId : type.member_types) {
				auto& memberType = get<spirv_cross::SPIRType>(memberTypeId);
				for (size_t i = 0; i < memberType.array.size(); ++i) {
					if (memberType.array[i] == 0) {
						memberType.array[i] = kEsSsboRuntimeArraySize;
						if (i < memberType.array_size_literal.size()) {
							memberType.array_size_literal[i] = true;
						}
					}
				}
			}
		}

		for (const auto& resource : resources.uniform_buffers) {
			setBlockName(resource);
		}
	}
};
} // namespace

std::string IKIGAI::RENDER::CompileSpirvToGlsl(
	const std::vector<uint32_t>& spirv, bool es, uint32_t version, bool enable_420pack_extension, bool force_flattened_io_blocks) {
	auto compiler = EsCompilerGLSL(spirv);

	spirv_cross::CompilerGLSL::Options options;
	options.es = es;
	options.version = version;
	options.enable_420pack_extension = enable_420pack_extension;
	options.force_flattened_io_blocks = force_flattened_io_blocks;
	// options.vertex.flip_vert_y = true;
	compiler.set_common_options(options);

	bool fix_varyings = (es && version <= 300) || force_flattened_io_blocks;

	if (fix_varyings) {
		// https://github.com/KhronosGroup/SPIRV-Cross/issues/1104

		auto stage = compiler.get_entry_points_and_stages()[0].execution_model;
		auto resources = compiler.get_shader_resources();

		if (stage == spv::ExecutionModelFragment) {
			for (const auto& input : resources.stage_inputs) {
				compiler.set_name(input.id, "varying_" + std::to_string(compiler.get_decoration(input.id, spv::DecorationLocation)));
			}
		}
		else if (stage == spv::ExecutionModelVertex) {
			for (const auto& output : resources.stage_outputs) {
				compiler.set_name(output.id, "varying_" + std::to_string(compiler.get_decoration(output.id, spv::DecorationLocation)));
			}
		}
	}

	// SSBO + std430 need GLES 3.10 or desktop GL 4.30. macOS is 4.10 and WebGL2 is ES 3.00.
	if ((es && version < 310) || (!es && version < 430)) {
		compiler.RemapStorageBuffersForEs300();
	}

	return compiler.compile();
}
