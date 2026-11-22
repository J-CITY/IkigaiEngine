#pragma once
#include <array>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include "reflectionStructs.h"
#include "mathModule/math.h"

namespace IKIGAI::RENDER {
class TextureInterface;
class UniformBufferInterface;
class StorageBufferInterface;
class VertexBufferInterface;
class IndexBufferInterface;

struct RayTracingCapabilities {
    bool AccelerationStructure = false;
    bool RayQuery = false;
    bool Pipeline = false;
    bool DescriptorIndexing = false;
    uint32_t MaxRecursionDepth = 0;
    uint64_t MaxDispatchInvocations = 0;
};

struct RayTracingShader {
    std::string Name, Path, Source;
    ShaderType Stage = ShaderType::NONE;
    std::string EntryPoint = "main";
    std::map<std::string,std::string> BackendPaths;
    std::string Language="GLSL";
};
struct RayTracingGroup {
    std::string Name, General, ClosestHit, AnyHit, Intersection;
    std::vector<uint8_t> RecordData;
};
struct RayTracingProgramResource {
    std::vector<RayTracingShader> Shaders;
    std::vector<RayTracingGroup> Groups;
    uint32_t MaxRecursionDepth = 1;
    uint32_t PushConstantSize = 0;
    uint32_t MaxPayloadSize=64,MaxAttributeSize=8;
    bool RequiresDescriptorIndexing=false;
};
inline void from_json(const nlohmann::json& j, RayTracingShader& v) {
    j.at("Name").get_to(v.Name); j.at("Stage").get_to(v.Stage);
    v.Path=j.value("Path",std::string{}); v.Source=j.value("Source",std::string{});
    v.EntryPoint=j.value("EntryPoint",std::string{"main"});
    v.BackendPaths=j.value("BackendPaths",std::map<std::string,std::string>{});
    v.Language=j.value("Language",std::string{"GLSL"});
}
inline void to_json(nlohmann::json& j, const RayTracingShader& v) {
    j={{"Name",v.Name},{"Stage",v.Stage},{"Path",v.Path},{"Source",v.Source},
       {"EntryPoint",v.EntryPoint},{"BackendPaths",v.BackendPaths},{"Language",v.Language}};
}
inline void from_json(const nlohmann::json& j, RayTracingGroup& v) {
    j.at("Name").get_to(v.Name); v.General=j.value("General",std::string{});
    v.ClosestHit=j.value("ClosestHit",std::string{}); v.AnyHit=j.value("AnyHit",std::string{});
    v.Intersection=j.value("Intersection",std::string{});
    v.RecordData=j.value("RecordData",std::vector<uint8_t>{});
}
inline void to_json(nlohmann::json& j, const RayTracingGroup& v) {
    j={{"Name",v.Name},{"General",v.General},{"ClosestHit",v.ClosestHit},
       {"AnyHit",v.AnyHit},{"Intersection",v.Intersection},{"RecordData",v.RecordData}};
}
inline void from_json(const nlohmann::json& j, RayTracingProgramResource& v) {
    j.at("Shaders").get_to(v.Shaders); v.Groups=j.value("Groups",std::vector<RayTracingGroup>{});
    v.MaxRecursionDepth=j.value("MaxRecursionDepth",1u);
    v.PushConstantSize=j.value("PushConstantSize",0u);
    v.MaxPayloadSize=j.value("MaxPayloadSize",64u);v.MaxAttributeSize=j.value("MaxAttributeSize",8u);
    v.RequiresDescriptorIndexing=j.value("RequiresDescriptorIndexing",false);
}
class UnsupportedRayTracing:public std::runtime_error {public:using std::runtime_error::runtime_error;};

class RayTracingProgram {
public:
    virtual ~RayTracingProgram() = default;
    virtual bool isRayQuery() const {return false;}
    virtual bool isCompute() const {return false;}
};
class AccelerationStructure {
public:
    virtual ~AccelerationStructure() = default;
};

// A BLAS can contain triangle and procedural geometries. All resources are owned
// by the submitted snapshot, not borrowed from mutable renderer state.
struct RayTracingGeometry {
    std::shared_ptr<VertexBufferInterface> Vertices;
    std::shared_ptr<IndexBufferInterface> Indices;
    uint32_t VertexCount=0, VertexStride=0, IndexCount=0, IndexStride=4;
    uint64_t VertexOffset=0, IndexOffset=0;
    bool Opaque=true;
    std::vector<std::array<float,6>> Aabbs;
};
struct RayTracingInstance {
    std::shared_ptr<AccelerationStructure> BottomLevel;
    MATH::Matrix4f Transform=MATH::Matrix4f::Identity;
    uint32_t CustomIndex=0, HitGroupOffset=0;
    uint8_t Mask=255;
    bool DoubleSided=true;
};

using RayTracingResource = std::variant<std::shared_ptr<TextureInterface>,
    std::shared_ptr<UniformBufferInterface>,std::shared_ptr<StorageBufferInterface>,
    std::shared_ptr<AccelerationStructure>,std::shared_ptr<VertexBufferInterface>>;
struct RayTracingDispatch {
    std::shared_ptr<RayTracingProgram> Program;
    std::map<std::string,std::vector<RayTracingResource>> Bindings;
    std::array<uint32_t,3> Extent={1,1,1};
    std::vector<uint8_t> PushConstants;
    uint32_t RaygenRecord=0;
	bool GraphManagedBarriers = false;
};
}
