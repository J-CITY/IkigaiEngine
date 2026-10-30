#pragma once
#include "renderModule/materials/deformationDescriptor.h"
#include "renderModule/materials/pbrMaterialDescriptor.h"
#include <optional>
#include <serdepp/adaptor/nlohmann_json.hpp>
#include <serdepp/attribute/make_optional.hpp>
#include <serdepp/serializer.hpp>
#include <string>
#include <vector>

#include "bindlessInterface.h"
#include "mathModule/math.h"
#include "rayTracingInterface.h"
#include "reflectionStructs.h"
#include "renderEnums.h"
#include "resourceModule/parser/parseFlags.h"
#include "textureInterface.h"
#include "utilsModule/reflection/reflection.h"
// #include "coreModule/resourceManager/parser/assimpParser.h"

namespace IKIGAI::RENDER {
enum class ResourceType {
	SHADER,
	TEXTURE,
	MODEL,
	AUDIO,
	MATERIAL,
	SKELETON_ANIMATION,
	SKELETON,
	SKELETON_BLENDSPACE_1D,
	SKELETON_BLENDSPACE_2D,
	SKELETON_STATE_GRAPH
};

struct ResourceBase {
	std::string parentPath;
};

struct TextureResource : public ResourceBase {
	ResourceType type = ResourceType::TEXTURE;
	std::string path;
	// bool needFileWatch = false;

	TextureType texType = TextureType::TEXTURE_2D;

	std::vector<std::string> pathTexture;
	std::vector<uint8_t> colorData;

	int width = 0;
	int height = 0;
	int depth = 0;
	int channels = 0;
	PixelFormat pixelType = PixelFormat::RGBA_INT;

	MinMagFilter minFilter = MinMagFilter::LINEAR;
	MinMagFilter magFilter = MinMagFilter::LINEAR;

	WrapFilter wrapS = WrapFilter::CLAMP_TO_EDGE;
	WrapFilter wrapT = WrapFilter::CLAMP_TO_EDGE;
	WrapFilter wrapR = WrapFilter::CLAMP_TO_EDGE;

	bool storage = false;
	bool isFloat = false;
	bool useMipmap = true;
	int mipMapCount = 0;

	template <class Context> constexpr static auto serde(Context& context, TextureResource& value) {
		using Self = TextureResource;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			//.field(&Self::needFileWatch, "NeedFileWatch", default_{true})
			.field(&Self::texType, "TexType", default_ { TextureType::TEXTURE_2D })
			.field(&Self::minFilter, "MinFilter", default_ { MinMagFilter::LINEAR })
			.field(&Self::magFilter, "MagFilter", default_ { MinMagFilter::LINEAR })
			.field(&Self::wrapS, "WrapS", default_ { WrapFilter::CLAMP_TO_EDGE })
			.field(&Self::wrapT, "WrapT", default_ { WrapFilter::CLAMP_TO_EDGE })
			.field(&Self::wrapR, "WrapR", default_ { WrapFilter::CLAMP_TO_EDGE })
			.field(&Self::pathTexture, "PathTexture", default_ { std::vector<std::string>() })
			.field(&Self::colorData, "ColorData", default_ { std::vector<uint8_t>() })
			.field(&Self::width, "Width", default_ { 0 })
			.field(&Self::height, "Height", default_ { 0 })
			.field(&Self::depth, "Depth", default_ { 0 })
			.field(&Self::channels, "Channels", default_ { 0 })
			.field(&Self::pixelType, "PixelType", default_ { PixelFormat::RGBA_INT })
			.field(&Self::storage, "Storage", default_ { false })
			.field(&Self::isFloat, "IsFloat", default_ { false })
			.field(&Self::parentPath, "##parent", default_ { std::string() })
			.field(&Self::useMipmap, "UseMipmap", default_ { true })
			.field(&Self::mipMapCount, "MipMapCount", default_ { 0 });
	}
	static auto GetMembers() {
		return std::tuple {
			// IKIGAI::UTILS::MakeMemberInfo("NeedFileWatch",
			// &TextureResource::needFileWatch),
			IKIGAI::UTILS::MakeMemberInfo("TexType", &TextureResource::texType),
			IKIGAI::UTILS::MakeMemberInfo("PathTexture", &TextureResource::pathTexture),
			IKIGAI::UTILS::MakeMemberInfo("ColorData", &TextureResource::colorData),
			IKIGAI::UTILS::MakeMemberInfo("Width", &TextureResource::width),
			IKIGAI::UTILS::MakeMemberInfo("Height", &TextureResource::height),
			IKIGAI::UTILS::MakeMemberInfo("Depth", &TextureResource::depth),
			IKIGAI::UTILS::MakeMemberInfo("Channels", &TextureResource::channels),
			IKIGAI::UTILS::MakeMemberInfo("PixelType", &TextureResource::pixelType),
			IKIGAI::UTILS::MakeMemberInfo("IsFloat", &TextureResource::isFloat),
			IKIGAI::UTILS::MakeMemberInfo("UseMipmap", &TextureResource::useMipmap),
			IKIGAI::UTILS::MakeMemberInfo("WrapS", &TextureResource::wrapS),
			IKIGAI::UTILS::MakeMemberInfo("WrapT", &TextureResource::wrapT),
			IKIGAI::UTILS::MakeMemberInfo("WrapR", &TextureResource::wrapR),
			IKIGAI::UTILS::MakeMemberInfo("MinFilter", &TextureResource::minFilter),
			IKIGAI::UTILS::MakeMemberInfo("MagFilter", &TextureResource::magFilter),
		};
	}
};

inline void from_json(const nlohmann::json& j, ShaderBindingMode& v) {
	const auto s = j.get<std::string>();
	if (s == "Bound") {
		v = ShaderBindingMode::Bound;
	}
	else if (s == "Bindless") {
		v = ShaderBindingMode::Bindless;
	}
	else {
		throw std::invalid_argument("Unknown shader BindingMode: " + s);
	}
}
inline void to_json(nlohmann::json& j, const ShaderBindingMode& v) {
	j = v == ShaderBindingMode::Bindless ? "Bindless" : "Bound";
}
inline void from_json(const nlohmann::json& j, VertexFetchMode& v) {
	const auto s = j.get<std::string>();
	if (s == "Attributes") {
		v = VertexFetchMode::Attributes;
	}
	else if (s == "Pulling") {
		v = VertexFetchMode::Pulling;
	}
	else {
		throw std::invalid_argument("Unknown VertexFetchMode: " + s);
	}
}
inline void to_json(nlohmann::json& j, const VertexFetchMode& v) {
	j = v == VertexFetchMode::Pulling ? "Pulling" : "Attributes";
}
// Raster, compute and ray tracing share one .shader descriptor. The program
// base holds named modules/groups; Paths remains the compact legacy form.
struct ShaderResource : public ResourceBase, public RayTracingProgramResource {
	bool supportsPbrLayers = false;
	std::string pbrLayerVariant;
	unsigned compiledPbrLayerCount = 0;
	ShaderBindingMode bindingMode = ShaderBindingMode::Bound;
	VertexFetchMode vertexFetchMode = VertexFetchMode::Attributes;
	uint32_t bindlessCapacity = 64;
	ResourceType type = ResourceType::SHADER;
	std::string path;

	bool useBinary = false;

	std::map<std::string, std::string> paths;

	inline static std::map<ShaderType, std::string> toStr {
		{ ShaderType::VERTEX, "VERTEX" },
		{ ShaderType::FRAGMENT, "FRAGMENT" },
		{ ShaderType::GEOMETRY, "GEOMETRY" },
		{ ShaderType::TESSELLATION_CONTROL, "TESSELLATION_CONTROL" },
		{ ShaderType::TESSELLATION_EVALUATION, "TESSELLATION_EVALUATION" },
		{ ShaderType::COMPUTE, "COMPUTE" },
		{ ShaderType::RAYGEN, "RAYGEN" },
		{ ShaderType::MISS, "MISS" },
		{ ShaderType::CLOSEST_HIT, "CLOSEST_HIT" },
		{ ShaderType::ANY_HIT, "ANY_HIT" },
		{ ShaderType::INTERSECTION, "INTERSECTION" },
		{ ShaderType::CALLABLE, "CALLABLE" },
	};

	inline static std::map<std::string, ShaderType> toEnum {
		{ "VERTEX", ShaderType::VERTEX },
		{ "FRAGMENT", ShaderType::FRAGMENT },
		{ "GEOMETRY", ShaderType::GEOMETRY },
		{ "TESSELLATION_CONTROL", ShaderType::TESSELLATION_CONTROL },
		{ "TESSELLATION_EVALUATION", ShaderType::TESSELLATION_EVALUATION },
		{ "COMPUTE", ShaderType::COMPUTE },
		{ "RAYGEN", ShaderType::RAYGEN },
		{ "MISS", ShaderType::MISS },
		{ "CLOSEST_HIT", ShaderType::CLOSEST_HIT },
		{ "ANY_HIT", ShaderType::ANY_HIT },
		{ "INTERSECTION", ShaderType::INTERSECTION },
		{ "CALLABLE", ShaderType::CALLABLE },
	};
	bool hasShader(ShaderType type) const {
		return paths.contains(toStr[type]);
	}

	void setPaths(std::map<ShaderType, std::string> ps) {
		paths.clear();
		for (auto& [e, v] : ps) {
			paths[toStr[e]] = v;
		}
	}

	std::map<ShaderType, std::string> getPaths() const {
		std::map<ShaderType, std::string> res;
		for (auto& [e, v] : paths) {
			res[toEnum.at(e)] = v;
		}
		return res;
	}

	const std::string& getShaderPath(ShaderType type) const {
		return paths.at(toStr[type]);
	}

	mutable std::map<ShaderType, std::string> sources;
	mutable std::map<ShaderType, std::vector<uint32_t>> spirvSources;

	RayTracingProgramResource getProgramResource() const {
		auto program = static_cast<const RayTracingProgramResource&>(*this);
		if (!Shaders.empty()) {
			if (!paths.empty() || !sources.empty()) {
				throw std::invalid_argument("Shader must use either Shaders or Paths/sources");
			}
			return program;
		}
		auto stages = getPaths();
		for (const auto& [stage, source] : sources) {
			stages.try_emplace(stage, "");
		}
		for (const auto& [stage, shaderPath] : stages) {
			RayTracingShader shader;
			shader.Name = toStr.at(stage);
			shader.Stage = stage;
			shader.Path = shaderPath;
			if (const auto it = sources.find(stage); it != sources.end()) {
				shader.Source = it->second;
			}
			program.Shaders.push_back(std::move(shader));
		}
		if (program.Shaders.empty()) {
			throw std::invalid_argument("Shader program has no stages");
		}
		if (useBinary) {
			throw std::invalid_argument("Program shader loading requires source stages");
		}
		return program;
	}

	template <class Context> constexpr static auto serde(Context& context, ShaderResource& value) {
		using Self = ShaderResource;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::supportsPbrLayers, "SupportsPbrLayers", default_ { false })
			.field(&Self::pbrLayerVariant, "PbrLayerVariant", default_ { std::string {} })
			.field(&Self::bindingMode, "BindingMode", default_ { ShaderBindingMode::Bound })
			.field(&Self::vertexFetchMode, "VertexFetchMode", default_ { VertexFetchMode::Attributes })
			.field(&Self::bindlessCapacity, "BindlessCapacity", default_ { uint32_t(64) })
			.field(&Self::useBinary, "UseBinary", default_ { false })
			.field(&Self::parentPath, "##parent", default_ { std::string() })
			.field(&Self::paths, "Paths", default_ { std::map<std::string, std::string> {} })
			.field(&Self::Shaders, "Shaders", default_ { std::vector<RayTracingShader> {} })
			.field(&Self::Groups, "Groups", default_ { std::vector<RayTracingGroup> {} })
			.field(&Self::MaxRecursionDepth, "MaxRecursionDepth", default_ { 1u })
			.field(&Self::PushConstantSize, "PushConstantSize", default_ { 0u })
			.field(&Self::MaxPayloadSize, "MaxPayloadSize", default_ { 64u })
			.field(&Self::MaxAttributeSize, "MaxAttributeSize", default_ { 8u })
			.field(&Self::RequiresDescriptorIndexing, "RequiresDescriptorIndexing", default_ { false });
	}

	static auto GetMembers() {
		return std::tuple {
			IKIGAI::UTILS::MakeMemberInfo("UseBinary", &ShaderResource::useBinary),
			IKIGAI::UTILS::MakeMemberInfo("BindingMode", &ShaderResource::bindingMode),
			IKIGAI::UTILS::MakeMemberInfo("VertexFetchMode", &ShaderResource::vertexFetchMode),
			IKIGAI::UTILS::MakeMemberInfo("BindlessCapacity", &ShaderResource::bindlessCapacity),
			IKIGAI::UTILS::MakeMemberInfo("Paths", &ShaderResource::paths),
			IKIGAI::UTILS::MakeMemberInfo("Shaders", &ShaderResource::Shaders),
			IKIGAI::UTILS::MakeMemberInfo("Groups", &ShaderResource::Groups),
			IKIGAI::UTILS::MakeMemberInfo("MaxRecursionDepth", &ShaderResource::MaxRecursionDepth),
			IKIGAI::UTILS::MakeMemberInfo("PushConstantSize", &ShaderResource::PushConstantSize),
			IKIGAI::UTILS::MakeMemberInfo("MaxPayloadSize", &ShaderResource::MaxPayloadSize),
			IKIGAI::UTILS::MakeMemberInfo("MaxAttributeSize", &ShaderResource::MaxAttributeSize),
			IKIGAI::UTILS::MakeMemberInfo("RequiresDescriptorIndexing", &ShaderResource::RequiresDescriptorIndexing),
		};
	}
};

struct ModelResource : public ResourceBase {
	ResourceType type = ResourceType::MODEL;
	std::string path;
	bool needFileWatch = false;

	std::string pathModel;
	std::vector<RESOURCES::ModelParserFlags> flags;

	template <class Context> constexpr static auto serde(Context& context, ModelResource& value) {
		using Self = ModelResource;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::needFileWatch, "NeedFileWatch", default_ { true })
			.field(&Self::pathModel, "PathModel")
			.field(&Self::parentPath, "##parent", default_ { std::string() })
			.field(&Self::flags, "Flags", default_ { std::vector<RESOURCES::ModelParserFlags>() });
	}
	static auto GetMembers() {
		return std::tuple {
			IKIGAI::UTILS::MakeMemberInfo("NeedFileWatch", &ModelResource::needFileWatch),
			IKIGAI::UTILS::MakeMemberInfo("PathModel", &ModelResource::pathModel),
			IKIGAI::UTILS::MakeMemberInfo("Flags", &ModelResource::flags),
		};
	}
};

struct SkeletonResource : public ResourceBase {
	ResourceType type = ResourceType::SKELETON;
	std::string path;

	std::string pathSkeleton;

	template <class Context> constexpr static auto serde(Context& context, SkeletonResource& value) {
		using Self = SkeletonResource;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::pathSkeleton, "PathSkeleton")
			.field(&Self::parentPath, "##parent", default_ { std::string() });
	}
	static auto GetMembers() {
		return std::tuple {
			IKIGAI::UTILS::MakeMemberInfo("PathSkeleton", &SkeletonResource::pathSkeleton),
		};
	}
};

struct AnimationResource : public ResourceBase {
	ResourceType type = ResourceType::SKELETON_ANIMATION;
	std::string path;

	std::string pathAnimation;
	bool additive = false;
	std::string additiveReference;
	std::string skeletonPath;

	template <class Context> constexpr static auto serde(Context& context, AnimationResource& value) {
		using Self = AnimationResource;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::pathAnimation, "PathAnimation")
			.field(&Self::parentPath, "##parent", default_ { std::string() })
			.field(&Self::additive, "Additive", default_ { false })
			.field(&Self::additiveReference, "AdditiveReference", default_ { std::string() })
			.field(&Self::skeletonPath, "SkeletonPath", default_ { std::string() });
	}
	static auto GetMembers() {
		return std::tuple {
			IKIGAI::UTILS::MakeMemberInfo("PathAnimation", &AnimationResource::pathAnimation),
			IKIGAI::UTILS::MakeMemberInfo("Additive", &AnimationResource::additive),
			IKIGAI::UTILS::MakeMemberInfo("AdditiveReference", &AnimationResource::additiveReference),
		};
	}
};

// struct SkeletonBlendspace1D : public ResourceBase {
//   ResourceType type = ResourceType::SKELETON_BLENDSPACE_1D;
//   std::string path;
//
//   std::string skeletonPath;
//
//   using AnimationPath = std::string;
//   using Value = float;
//   std::vector<std::pair<Value, AnimationPath>> nodes;
//   template <class Context>
//   constexpr static auto serde(Context &context, SkeletonBlendspace1D &value) {
//     using Self = SkeletonBlendspace1D;
//     using namespace serde::attribute;
//     serde::serde_struct(context, value)
//         .field(&Self::skeletonPath, "SkeletonPath")
//         .field(&Self::parentPath, "##parent", default_{std::string()})
//         .field(&Self::nodes, "Nodes");
//   }
//   static auto GetMembers() {
//     return std::tuple{
//         IKIGAI::UTILS::MakeMemberInfo("SkeletonPath", &SkeletonBlendspace1D::skeletonPath),
//         IKIGAI::UTILS::MakeMemberInfo("Nodes", &SkeletonBlendspace1D::nodes),
//     };
//   }
// };

struct SkeletonBlendspace : public ResourceBase {
	ResourceType type = ResourceType::SKELETON_BLENDSPACE_2D;
	std::string path;

	std::string skeletonPath;

	using AnimationPath = std::string;
	using Value = float;
	struct Row {
		Value value;
		std::vector<std::pair<Value, AnimationPath>> nodes;

		template <class Context> constexpr static auto serde(Context& context, Row& v) {
			using namespace serde::attribute;
			serde::serde_struct(context, v).field(&Row::value, "Value").field(&Row::nodes, "Nodes");
		}
	};
	std::vector<Row> nodes;

	template <class Context> constexpr static auto serde(Context& context, SkeletonBlendspace& value) {
		using Self = SkeletonBlendspace;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::skeletonPath, "SkeletonPath")
			.field(&Self::parentPath, "##parent", default_ { std::string() })
			.field(&Self::nodes, "Nodes");
	}
	static auto GetMembers() {
		return std::tuple {
			IKIGAI::UTILS::MakeMemberInfo("SkeletonPath", &SkeletonBlendspace::skeletonPath),
			IKIGAI::UTILS::MakeMemberInfo("Nodes", &SkeletonBlendspace::nodes),
		};
	}
};

struct MaterialResource : public ResourceBase {
	DeformationDescriptor Deformation = {};
	PbrLayersDescriptor PbrLayers;
	ResourceType Type = ResourceType::MATERIAL;
	std::string path;
	bool NeedFileWatch = false;

	std::string ShaderPath;
	bool Blendable = false;
	bool BackfaceCulling = true;
	bool FrontfaceCulling = false;
	bool DepthTest = true;
	bool DepthWriting = true;
	bool ColorWriting = true;
	unsigned GpuInstances = 1;
	bool IsDeferred = false;
	DepthFunction DepthFunc = DepthFunction::LESS;

	using UniformType = std::variant<float, int, bool, std::string, MATH::Vector4f, MATH::Vector3f, MATH::Vector2f, MATH::Matrix4f, MATH::Matrix3f>;

	std::map<std::string, UniformType> Uniforms;

	template <class Context> constexpr static auto serde(Context& context, MaterialResource& value) {
		using Self = MaterialResource;
		using namespace serde::attribute;
		serde::serde_struct(context, value)
			.field(&Self::NeedFileWatch, "NeedFileWatch", default_ { true })
			.field(&Self::ShaderPath, "ShaderPath", default_ { "" })
			.field(&Self::Blendable, "Blendable", default_ { false })
			.field(&Self::BackfaceCulling, "BackfaceCulling", default_ { true })
			.field(&Self::FrontfaceCulling, "FrontfaceCulling", default_ { false })
			.field(&Self::DepthTest, "DepthTest", default_ { true })
			.field(&Self::DepthWriting, "DepthWriting", default_ { true })
			.field(&Self::ColorWriting, "ColorWriting", default_ { true })
			.field(&Self::GpuInstances, "GpuInstances", default_ { 1 })
			.field(&Self::IsDeferred, "IsDeferred", default_ { false })
			.field(&Self::DepthFunc, "DepthFunc", default_ { DepthFunction::LESS })
			.field(&Self::parentPath, "##parent", default_ { std::string() })
			.field(&Self::PbrLayers, "PbrLayers", default_ { PbrLayersDescriptor {} })
			.field(&Self::Deformation, "Deformation", default_ { DeformationDescriptor {} })
			.field(&Self::Uniforms, "Uniforms");
	}
	static auto GetMembers() {
		return std::tuple {
			IKIGAI::UTILS::MakeMemberInfo("NeedFileWatch", &MaterialResource::NeedFileWatch),
			IKIGAI::UTILS::MakeMemberInfo("ShaderPath", &MaterialResource::ShaderPath),
			IKIGAI::UTILS::MakeMemberInfo("Blendable", &MaterialResource::Blendable),
			IKIGAI::UTILS::MakeMemberInfo("BackfaceCulling", &MaterialResource::BackfaceCulling),
			IKIGAI::UTILS::MakeMemberInfo("FrontfaceCulling", &MaterialResource::FrontfaceCulling),
			IKIGAI::UTILS::MakeMemberInfo("DepthTest", &MaterialResource::DepthTest),
			IKIGAI::UTILS::MakeMemberInfo("DepthWriting", &MaterialResource::DepthWriting),
			IKIGAI::UTILS::MakeMemberInfo("ColorWriting", &MaterialResource::ColorWriting),
			IKIGAI::UTILS::MakeMemberInfo("GpuInstances", &MaterialResource::GpuInstances),
			IKIGAI::UTILS::MakeMemberInfo("IsDeferred", &MaterialResource::IsDeferred),
			IKIGAI::UTILS::MakeMemberInfo("DepthFunc", &MaterialResource::DepthFunc),
			IKIGAI::UTILS::MakeMemberInfo("Uniforms", &MaterialResource::Uniforms),
			IKIGAI::UTILS::MakeMemberInfo("PbrLayers", &MaterialResource::PbrLayers),
		};
	}
};

} // namespace IKIGAI::RENDER

namespace serde {
// Uniform vectors and matrices share JSON object syntax. Select their shape
// before reading; probing larger vectors first throws and inserts null fields.
template <> struct serde_adaptor<nlohmann::json, IKIGAI::RENDER::MaterialResource::UniformType> {
	using Variant = IKIGAI::RENDER::MaterialResource::UniformType;

	static void from(nlohmann::json& json, std::string_view key, Variant& value) {
		auto& uniform = key.empty() ? json : json.at(std::string(key));
		if (uniform.is_number_float()) {
			value = uniform.get<float>();
		}
		else if (uniform.is_number_integer()) {
			value = uniform.get<int>();
		}
		else if (uniform.is_boolean()) {
			value = uniform.get<bool>();
		}
		else if (uniform.is_string()) {
			value = uniform.get<std::string>();
		}
		else if (uniform.is_object()) {
			using namespace IKIGAI::MATH;
			if (const auto data = uniform.find("Data"); data != uniform.end()) {
				if (!data->is_array() || (data->size() != 9 && data->size() != 16)) {
					throw std::runtime_error("Uniform matrix Data must contain 9 or 16 elements");
				}
				if (data->size() == 16) {
					value = deserialize<Matrix4f>(uniform);
				}
				else {
					value = deserialize<Matrix3f>(uniform);
				}
			}
			else if (uniform.contains("W")) {
				value = deserialize<Vector4f>(uniform);
			}
			else if (uniform.contains("Z")) {
				value = deserialize<Vector3f>(uniform);
			}
			else if (uniform.contains("X") && uniform.contains("Y")) {
				value = deserialize<Vector2f>(uniform);
			}
			else {
				throw std::runtime_error("Uniform object must be a vector or matrix");
			}
		}
		else {
			throw std::runtime_error("Unsupported uniform JSON type");
		}
	}

	static void into(nlohmann::json& json, std::string_view key, const Variant& value) {
		std::visit([&](const auto& uniform) { serialize_to<nlohmann::json>(uniform, json, key); }, value);
	}
};
}
