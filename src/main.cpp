// #include "core/engine.hpp"
// #include "core/sdl-wrapper.hpp"
// #include "coreModule/core/app.h"
//
// #include <iostream>
// #include <map>
//
// #include <unordered_map>
// #include <variant>
//
// #include "coreModule/platform.hpp"
// #include "mathModule/math.h"
// #include "renderModule/backends/gl/materialGl.h"
// #include "renderModule/backends/gl/modelGl.h"
// #include "resourceModule/parser/assimpParser.h"
// #include "sceneModule/sceneManager.h"
// #include "utilsModule/reflection/reflection.h"
// using namespace std;
// #include <serdepp/adaptor/nlohmann_json.hpp>
// #include <serdepp/attribute/default.hpp>
// #include <serdepp/serializer.hpp>
//
// struct Component {
//  int c = 0;
//  template <class Context>
//  constexpr static auto serde(Context &context, Component &value) {
//    using Self = Component;
//    using namespace serde::attribute;
//    serde::serde_struct(context, value).field(&Self::c, "c", default_{0});
//  }
//};
// struct ComponentA : public Component {
//  int a = 0;
//};
// struct ComponentB : public Component {
//  int a = 0;
//  template <class Context>
//  constexpr static auto serde(Context &context, ComponentB &value) {
//    using Self = ComponentB;
//    using namespace serde::attribute;
//    serde::serde_struct(context, value).field(&Self::a, "a", default_{0});
//  }
//};
// struct ComponentC : public Component {
//  int a = 0;
//};
//
// struct Comtainer {
//  unordered_map<int, std::vector<std::shared_ptr<Component>>> components;
//};
//
// struct Text {
//  std::string text;
//
//  template <class Context>
//  constexpr static auto serde(Context &context, Text &value) {
//    using Self = Text;
//    using namespace serde::attribute;
//    serde::serde_struct(context, value)
//        .field(&Self::text, "text", default_{""});
//  }
//};
// struct Obj {
//  int id = 0;
//  void setId(int i) { id = i; }
//  int getId() const { return id; }
//
//  std::variant<int, std::string, float, ComponentB, std::shared_ptr<Text>>
//      variantData;
//  std::string strData;
//  std::vector<int> vecData;
//  std::unordered_map<int, std::string> mapData;
//
//  static auto GetMembers() {
//    return std::tuple{
//        IKIGAI::UTILS::MakeMemberInfo("id", &Obj::getId, &Obj::setId),
//        IKIGAI::UTILS::MakeMemberInfo("mapData", &Obj::mapData)};
//  }
//
//  template <class Context>
//  constexpr static auto serde(Context &context, Obj &value) {
//    using Self = Obj;
//    using namespace serde::attribute;
//    serde::serde_struct(context, value)
//        .field(&Self::id, "id", default_{0})
//        .field(&Self::vecData, "vecData")
//        .field(&Self::variantData, "variantData");
//  }
//};
//
// Comtainer comtainer;
//
// template <class T, class U>
// std::string value_type(std::unordered_map<T, U> const &m) {
//  return typeid(U).name();
//}
// #include <serdepp/adaptor/reflection.hpp>

// #ifdef DX12_BACKEND
// #include <Windows.h>
// #include <iostream>
// #include <dxgi1_4.h>
// #include <d3d12.h>
// #include <D3Dcompiler.h>
// #include <DirectXMath.h>
// #include <wrl/client.h>
// #include <DirectXPackedVector.h>
// using Microsoft::WRL::ComPtr;
// using namespace DirectX;
// using namespace DirectX::PackedVector;
//
// #pragma comment(lib, "d3dcompiler.lib")
// #pragma comment(lib, "D3D12.lib")
//
// int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine,
// int showCmd) {
//
// #if defined(DEBUG) | defined(_DEBUG)
//	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
// #endif
//	IKIGAI::CORE::App::hInstance = hInstance;
//	IKIGAI::CORE::App app;
//	app.getCore()->sceneManager->loadFromFile("scenes/sceneDx12.json");
//
//	app.run();
//	return 0;
// }
// #endif

#ifndef OCULUS
#include "coreModule/core/app.h"
#include "renderModule/backends/driverFactory.h"

#include <iostream>
#include <string>
#ifdef DX12_BACKEND
#include <Windows.h>
#endif

#include "ecsModule/world.h"
#include "sceneModule/sceneManager.h"


struct Vec {
  float x = 0;
  float y = 0;
  float norm() const { return std::sqrt(x * x + y * y); }

  void setX(float v) { x = v; }
  float getX() const { return x; }
};

namespace AA {
struct A {
  int this_is_the_name;
};
} // namespace AA

#include "EcsDemo.h"
#include "ReflectionDemo.h"

void ShaderTest()
{
    std::string v_source = R"(
#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec3 aNormal;

layout(binding = 0) uniform EngineUBO
{
    mat4 View;
    mat4 Projection;
    vec3 ViewPos;
    float Time;
    vec2 ViewportSize;
    float FPS;
    int FrameCount;
} engine_UBO;

//struct PushModel {
//    mat4 model;
//};
//layout(binding = 1) uniform PushModel engine_Model;

layout(push_constant) uniform Constants {
    mat4 model;
} engine_Model;

layout(location = 0) out struct { 
    vec3 Position; 
    vec3 Normal; 
    vec2 TexCoord; 
} Out;

out gl_PerVertex { 
    vec4 gl_Position; 
};

void main()
{
	Out.Position = vec3(engine_Model.model * vec4(aPosition, 1.0));
	Out.Normal = mat3(transpose(inverse(engine_Model.model))) * aNormal;
	Out.TexCoord = aTexCoord;
	gl_Position = engine_UBO.Projection * engine_UBO.View * engine_Model.model * vec4(aPosition, 1.0);
}
    )";

    std::string f_source = R"(
#version 450 core

// OUTPUT
layout(location = 0) out vec4 FRAGMENT_COLOR;

// INPUT
layout(location = 0) in struct { 
    vec3 Position; 
    vec3 Normal; 
    vec2 TexCoord; 
} fs_in;

// UNIFORMS
layout(binding = 0) uniform EngineUBO
{
    mat4 View;
    mat4 Projection;
    vec3 ViewPos;
    float Time;
    vec2 ViewportSize;
    float FPS;
    int FrameCount;
} engine_UBO;

struct LightOGL {
    vec3 pos;
    float cutoff;
    vec3 forward;
    float outerCutoff;
    vec3 color;
    float constant;
    int type;
    float linear;
    float quadratic;
    float intensity;
    float radius;
};

layout(std430, binding = 1) buffer engineLights {
    LightOGL lights[];
};

layout(std140, binding = 2) uniform DataUBO {
    vec2        u_TextureTiling;
    vec2        u_TextureOffset;
    vec4        u_Diffuse;
    vec3        u_Specular;
    float       u_Shininess;
    float       u_HeightScale;
    bool        u_EnableNormalMapping;
} data_UBO;

uniform layout(binding = 3) sampler2D   u_DiffuseMap;
uniform layout(binding = 4) sampler2D   u_SpecularMap;
uniform layout(binding = 5) sampler2D   u_NormalMap;
uniform layout(binding = 6) sampler2D   u_HeightMap;

// GLOBAL VARS
vec3 g_Normal;
vec2 g_TexCoords;
vec3 g_ViewDir;
vec4 g_DiffuseTexel;
vec4 g_SpecularTexel;
vec4 g_HeightTexel;
vec4 g_NormalTexel;
/

void main() {
    g_TexCoords = data_UBO.u_TextureOffset + vec2(mod(fs_in.TexCoord.x * data_UBO.u_TextureTiling.x, 1), mod(fs_in.TexCoord.y * data_UBO.u_TextureTiling.y, 1)); 

    /* Apply parallax mapping */
    //if (data_UBO.u_HeightScale > 0) {
    //    g_TexCoords = ParallaxMapping(normalize(fs_in.TangentViewPos - fs_in.TangentFragPos));
    //}

    /* Apply color mask */
    g_ViewDir           = normalize(engine_UBO.ViewPos - fs_in.Position);
    g_DiffuseTexel      = texture(u_DiffuseMap,  g_TexCoords) * data_UBO.u_Diffuse;
    g_SpecularTexel     = texture(u_SpecularMap, g_TexCoords) * vec4(data_UBO.u_Specular, 1.0);
    if (data_UBO.u_EnableNormalMapping) {
        g_Normal = texture(u_NormalMap, g_TexCoords).rgb;
        g_Normal = normalize(g_Normal * 2.0 - 1.0);
        //g_Normal = normalize(fs_in.TBN * g_Normal);
    }
    else {
        g_Normal = normalize(fs_in.Normal);
    }

    vec3 lightSum = vec3(0.0);
    for (int i = 0; i < lights.length(); ++i) {
        switch(lights[i].type) {
            case 0: {
                lightSum += 2;
                break;
            }
            case 1: {
                lightSum += 3;
                break;
            }
            case 2: {
                lightSum += 4;
                break;
            }
            case 3: lightSum += 5;    break;
            case 4: lightSum += 6; break;
        }
    }

	FRAGMENT_COLOR = vec4(lightSum, g_DiffuseTexel.a);
}
    )";

    std::string g_source = R"()";

    std::string tc_source = R"()";

    std::string te_source = R"()";

    std::string c_source = R"()";

    std::array stages = {IKIGAI::RENDER::ShaderType::VERTEX,
        IKIGAI::RENDER::ShaderType::FRAGMENT,
        IKIGAI::RENDER::ShaderType::GEOMETRY,
        IKIGAI::RENDER::ShaderType::TESSELLATION_CONTROL,
        IKIGAI::RENDER::ShaderType::TESSELLATION_EVALUATION,
        IKIGAI::RENDER::ShaderType::COMPUTE};
    std::array stagesName = {
        "IKIGAI::RENDER::ShaderType::VERTEX",
        "IKIGAI::RENDER::ShaderType::FRAGMENT",
        "IKIGAI::RENDER::ShaderType::GEOMETRY",
        "IKIGAI::RENDER::ShaderType::TESSELLATION_CONTROL",
        "IKIGAI::RENDER::ShaderType::TESSELLATION_EVALUATION",
        "IKIGAI::RENDER::ShaderType::COMPUTE"};
    IKIGAI::RENDER::ShaderReflection reflection;
    int stagesId = 0;

    for (auto& source :
        {v_source, f_source, g_source, tc_source, te_source, c_source}) {
        if (source.empty()) {
            stagesId++;
            continue;
        }

        auto spirvesource =
            IKIGAI::RENDER::CompileGlslToSpirv(stages[stagesId], source, {});

        IKIGAI::RENDER::ShaderInterface::GetReflection(reflection, spirvesource,
            stages[stagesId]);

        bool es = false;
        bool enable_420pack_extension = true;
        bool force_flattened_io_blocks = true;
        unsigned int version = 450;
        std::cout << stagesName[stagesId] << "\n";
        {
            es = true;
            version = 300;
            enable_420pack_extension = false;
            force_flattened_io_blocks = false;
            auto res = IKIGAI::RENDER::CompileSpirvToGlsl(spirvesource, es, version,
                enable_420pack_extension,
                force_flattened_io_blocks);
            std::cout << "Ver: " << version
                << "; Ext420: " << enable_420pack_extension
                << "; force_flattened: " << force_flattened_io_blocks << "\n";
            std::cout << res << "\n";
            std::cout << "==============================================\n";
        }
        {
            es = false;
            version = 450;
            enable_420pack_extension = true;
            force_flattened_io_blocks = true;
            auto res = IKIGAI::RENDER::CompileSpirvToGlsl(spirvesource, es, version,
                enable_420pack_extension,
                force_flattened_io_blocks);
            std::cout << "Ver: " << version
                << "; Ext420: " << enable_420pack_extension
                << "; force_flattened: " << force_flattened_io_blocks << "\n";
            std::cout << res << "\n";
            std::cout << "==============================================\n";
        }

        {
            es = false;
            version = 410;
            enable_420pack_extension = false;
            force_flattened_io_blocks = true;
            auto res = IKIGAI::RENDER::CompileSpirvToGlsl(spirvesource, es, version,
                enable_420pack_extension,
                force_flattened_io_blocks);
            std::cout << "Ver: " << version
                << "; Ext420: " << enable_420pack_extension
                << "; force_flattened: " << force_flattened_io_blocks << "\n";
            std::cout << res << "\n";
            std::cout << "==============================================\n";
        }
        {
            es = true;
            version = 300;
            enable_420pack_extension = false;
            force_flattened_io_blocks = false;
            auto res = IKIGAI::RENDER::CompileSpirvToGlsl(spirvesource, es, version,
                enable_420pack_extension,
                force_flattened_io_blocks);
            std::cout << "Ver: " << version
                << "; Ext420: " << enable_420pack_extension
                << "; force_flattened: " << force_flattened_io_blocks << "\n";
            std::cout << res << "\n";
            std::cout << "==============================================\n";
        }
        {
            auto res = IKIGAI::RENDER::CompileSpirvToHlsl(spirvesource, 50);
            std::cout << "HLSL 50\n";
            std::cout << res << "\n";
            std::cout << "==============================================\n";
        }

        stagesId++;
    }
}

#ifdef USE_SDL
#include <SDL3/SDL_main.h>
#endif

int main(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const std::string prefix = "--render-backend=";
    if (arg.rfind(prefix, 0) == 0) {
      IKIGAI::RENDER::SetCliRenderBackendOverride(arg.substr(prefix.size()));
    }
  }
#ifdef DX12_BACKEND
  IKIGAI::CORE::App::hInstance = GetModuleHandleW(nullptr);
#endif
  IKIGAI::CORE::App app;
  app.getCore()->sceneManager->loadFromFile("scenes/scene.json");
  std::cout << "START" << std::endl;

    //TEST PATCH RESOURCE
  //auto res = IKIGAI::UTILS::FromJson<IKIGAI::RENDER::MaterialResource>("/materials/a.material");
  //auto resStr = IKIGAI::UTILS::ToJsonStr(res.unwrap());

  app.run();

  // Example::EcsDemo::run();
  // std::cout << "______________" << std::endl;
  // ExampleReflection::ReflectionDemo::run();

  // Obtains name of member.
  // using namespace AA;
  // std::cout << NAMEOF_TYPE(A) << "\n";

  // Vec v;
  //
  // Ubpa::UDRefl::Mngr.RegisterType<Vec>();
  // Ubpa::UDRefl::Mngr.AddField<&Vec::x, &Vec::getX, &Vec::setX>("x");
  // Ubpa::UDRefl::Mngr.AddField<&Vec::y>("y");
  // Ubpa::UDRefl::Mngr.AddMethod<&Vec::norm>("norm");
  //
  // for (auto &&[name, info] : Ubpa::UDRefl::FieldRange_of<Vec>) {
  //   std::cout << name.GetView() << std::endl;
  //
  //   if (info.setter) {
  //     info.setter->Invoke(&v, nullptr, Ubpa::UDRefl::TempArgsView{2.0f});
  //   }
  //   // info.fieldptr.Var(&v) = 2;
  // }
  //
  // std::cout << v.x << " " << v.y << "\n";
  //
  // for (auto &&[name, info] : Ubpa::UDRefl::MethodRange_of<Vec>) {
  //   std::cout << name.GetView() << std::endl;
  //   // info.methodptr.Invoke(&v);
  // }
  //
  // IKIGAI::ECS2::World world;
  //
  // auto e0 = world.createEntity();
  // std::cout << (int)e0.getUniqueId() << std::endl;

  return 0;
}
#endif
