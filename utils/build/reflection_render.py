"""Render reflection models into C++ without changing the parsed declarations."""

MARKER = "// AUTO GENERATED FILE. DO NOT MODIFY."
FEATURES = {
    "scriptComponent": "USE_LUA",
    "ikigaiScriptComponent": "USE_IKIGAI_SCRIPT",
    "physics2dComponent": "USE_PHYSICS_2D",
    "forceField2dComponent": "USE_PHYSICS_2D",
    "cloth2dComponent": "USE_PHYSICS_2D",
    "physics3dComponent": "USE_PHYSICS_3D",
    "physicsComponent": "USE_PHYSICS_3D",
}


def guarded(stem, text):
    feature = FEATURES.get(stem)
    return f"#if {feature}\n{text}#endif\n" if feature else text


def render_serde(cls):
    fields = []
    for prop in cls.properties:
        if prop.serde is None:
            continue
        key = prop.serde.get("name", f'"{prop.name}"')
        default = f", default_{{{prop.serde['default']}}}" if "default" in prop.serde else ""
        fields.append(f"            .field(&{cls.name}::{prop.name}, {key}{default})")
    chain = "".join("\n" + field for field in fields)
    function = f"""    template<class Context>
    constexpr auto ikigai_serde(Context& context, {cls.name}& value) {{
        using namespace serde::attribute;
        serde::serde_struct(context, value){chain};
    }}
"""
    if cls.namespace:
        function = f"namespace {cls.namespace} {{\n{function}}}\n"
    return function + "\n"


def render_serde_header(header, root):
    include = header.path.relative_to(root).as_posix().removeprefix("engine/")
    return f"""{MARKER}
// Generated from {header.path.name}
#pragma once
#include <set>
#include <vector>
#include <map>
#include <string>
#include "reflectionModule/reflection.h"
#include "{include}"

""" + "".join(render_serde(cls) for cls in header.classes)


def metadata_expression(metadata):
    entries = [f'{{"{key}", {value.expression}}}' for key, value in metadata.items()]
    return "IKIGAI::UTILS::Metadata{ " + ", ".join(entries) + " }"


def register_property(owner, prop):
    method = "registerStaticField" if prop.is_static else "registerField"
    access = f"&{owner}::{prop.name}"
    if prop.getter or prop.setter:
        getter = f"[]({owner}* obj) {{ return obj->{prop.getter}(); }}" if prop.getter else "nullptr"
        setter = f"[]({owner}* obj, {prop.cpp_type} val) {{ obj->{prop.setter}(val); }}" if prop.setter else "nullptr"
        access = f"{getter}, {setter}"
    metadata = metadata_expression(prop.metadata)
    return f'm.{method}<{owner}, {prop.cpp_type}>("{prop.name}", {access}, {metadata});'


def class_registration(cls):
    owner = cls.qualified_name
    yield "auto& m = IKIGAI::UTILS::ReflectionManager::Instance();"
    yield f"m.registerType<{owner}>();"
    for key, value in (("TypeName", cls.type_name), ("ClassName", cls.class_name)):
        yield f'm.registerStaticField<{owner}, std::string>("{key}", [](){{ return std::string({value}); }}, nullptr);'
    for group in cls.groups:
        yield f'm.addTypeToGroup<{owner}>("{group}");'
    if "Component" in cls.groups:
        for name, method in (("AddComponent", "addComponent"), ("RemoveComponent", "removeComponent")):
            yield f'm.registerStaticMethod<{owner}>("{name}", static_cast<void(*)(std::shared_ptr<IKIGAI::ECS::Object>)>('
            yield "    [](std::shared_ptr<IKIGAI::ECS::Object> obj) {"
            yield f"        obj->{method}<{owner}>();"
            yield "    }"
            yield "));"
    for key, (cpp_type, expr) in cls.metadata.items():
        yield f'm.registerStaticField<{owner}, {cpp_type}>("{key}", [](){{ return {expr}; }}, nullptr);'
    for prop in cls.properties:
        yield register_property(owner, prop)
    for method in cls.methods:
        yield f'm.registerMethod("{method.name}", &{owner}::{method.name}, {metadata_expression(method.metadata)});'


def render_registration_source(header):
    definitions = []
    calls = []
    for cls in header.classes:
        symbol = f"ReflectionReg<{cls.qualified_name}>"
        body = " \n".join("            " + line for line in class_registration(cls))
        definitions.append(f"""    template<>
    struct {symbol} {{
        static void Register() {{
{body}
        }}
    }};
""")
        calls.append(f"    {symbol}::Register();")
    for enum in header.enums:
        symbol = "ReflectionReg_Enum_" + enum.name.replace("::", "__")
        pairs = ", ".join(f'{{"{value}", static_cast<int>({enum.qualified_name}::{value})}}' for value in enum.values)
        definitions.append(f"""    struct {symbol} {{
        static void Register() {{
            auto& m = IKIGAI::UTILS::ReflectionManager::Instance();
            m.registerEnum<{enum.qualified_name}>("{enum.name}", {{ {pairs} }});
        }}
    }};
""")
        calls.append(f"    {symbol}::Register();")
    stem = header.path.stem
    source = f"""{MARKER}
// Generated from {header.path.name}
#include "{stem}.generated.h"
#include "coreModule/ecs/object.h"

namespace IKIGAI::UTILS {{
{''.join(definitions)}void RegisterReflection_{stem}() {{
""" + "\n".join(calls) + "\n}\n}\n"
    return guarded(stem, source + "\n") if stem in FEATURES else source


def render_registry(headers):
    stems = [header.path.stem for header in headers]
    declarations = "".join(guarded(stem, f"void RegisterReflection_{stem}();\n") for stem in stems)
    calls = "".join(guarded(stem, f"        RegisterReflection_{stem}();\n") for stem in stems)
    return f"""{MARKER}
#include <mutex>
namespace IKIGAI::UTILS {{
{declarations}void RegisterGeneratedReflection() {{
    static std::once_flag once;
    std::call_once(once, [] {{
{calls}    }});
}}
}}
"""
