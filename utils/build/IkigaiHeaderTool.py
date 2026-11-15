
# Support direct CLI invocation from any working directory.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import contextlib
import os
import sys

from utils.build.header_parser import parse, arguments

def infer_cpp_type_and_val(val, key=None):
    if key == "EditWidget":
        return "IKIGAI::UTILS::WidgetType", f"IKIGAI::UTILS::WidgetType::{val}"
    val = val.strip()
    if val in ("true", "false"): return "bool", val
    if val.startswith('"') and val.endswith('"'): return "std::string", f"std::string({val})"
    
    try:
        if '.' in val or 'f' in val.lower():
            float(val.replace('f', ''))
            return "float", val
        int(val)
        return "int", val
    except ValueError:
        pass
        
    # Vector via []
    if val.startswith('[') and val.endswith(']'):
        content = val[1:-1].strip()
        if not content:
            print("Error: Empty [] container cannot be deduced.")
            sys.exit(1)
            
        items = []
        lvl = 0
        curr = ""
        is_set = False
        for c in content:
            if c in '({[': lvl += 1
            elif c in ')}]': lvl -= 1
            if lvl == 0 and c == '|':
                is_set = True
                items.append(curr.strip())
                curr = ""
            elif lvl == 0 and c == ',' and not is_set:
                items.append(curr.strip())
                curr = ""
            else:
                curr += c
        if curr: items.append(curr.strip())
        
        c_type, c_val = infer_cpp_type_and_val(items[0], key)
        val_list = [infer_cpp_type_and_val(i, key)[1] for i in items]
        ctype = f"std::set<{c_type}>" if is_set else f"std::vector<{c_type}>"
        return ctype, f"{ctype}{{{', '.join(val_list)}}}"
        
    # Map via {}
    if val.startswith('{') and val.endswith('}'):
        content_map = val[1:-1].strip()
        if not content_map:
            print("Error: Empty {} container cannot be deduced.")
            sys.exit(1)
            
        items = []
        lvl = 0
        curr = ""
        is_map = False
        for c in content_map:
            if c in '({[': lvl += 1
            elif c in ')}]': lvl -= 1
            
            if lvl == 0 and c == ',':
                items.append(curr.strip())
                curr = ""
            elif lvl == 0 and c == ':':
                is_map = True
                curr += ':'
            else:
                curr += c
        if curr: items.append(curr.strip())
        
        if is_map:
            pairs_expr = []
            k_type, v_type = "", ""
            for item in items:
                idx = item.find(':')
                if idx == -1: continue
                k = item[:idx].strip()
                v = item[idx+1:].strip()
                k_t, k_val = infer_cpp_type_and_val(k)
                v_t, v_val = infer_cpp_type_and_val(v)
                if not k_type:
                    k_type = k_t
                    v_type = v_t
                pairs_expr.append(f"{{{k_val}, {v_val}}}")
            ctype = f"std::map<{k_type}, {v_type}>"
            return ctype, f"{ctype}{{{', '.join(pairs_expr)}}}"
            
    if val.isidentifier():
        return "std::string", f'std::string("{val}")'
        
    return f"decltype({val})", val

def process_file(filepath):
    with open(filepath, encoding="utf-8") as source:
        return parse(source.read(), filepath)

def generate_headers(all_files_data, output_dir_base, root_dir):
    os.makedirs(output_dir_base, exist_ok=True)
    
    registrations = []
    for file_data in all_files_data:
        orig_filepath = file_data['header']
        classes = file_data['classes']
        enums = file_data['enums']
        
        print(root_dir)
        print(orig_filepath)

        rel_path = os.path.relpath(orig_filepath, root_dir).replace('\\', '/')
        if rel_path.startswith('engine/'):
            rel_path = rel_path[7:]
        
        base_name = os.path.basename(orig_filepath)
        name_no_ext = os.path.splitext(base_name)[0]
        out_filename = f"{name_no_ext}.generated.h"
        out_filepath = os.path.join(output_dir_base, out_filename)
        
        cpp_h_content = f"// AUTO GENERATED FILE. DO NOT MODIFY.\n"
        cpp_h_content += f"// Generated from {base_name}\n"
        cpp_h_content += f"#pragma once\n"
        cpp_h_content += f"#include <set>\n"
        cpp_h_content += f"#include <vector>\n"
        cpp_h_content += f"#include <map>\n"
        cpp_h_content += f"#include <string>\n"
        cpp_h_content += f"#include \"utilsModule/reflection/reflection.h\"\n"
        cpp_h_content += f"#include \"{rel_path}\"\n\n"

        cpp_c_content = f"// AUTO GENERATED FILE. DO NOT MODIFY.\n"
        cpp_c_content += f"// Generated from {base_name}\n"
        cpp_c_content += f"#include \"{out_filename}\"\n"
        cpp_c_content += f"#include \"coreModule/ecs/object.h\"\n\n"

        # Generate serde functions first (for ADL) in header
        for c in classes:
            cname = c["args"].get("Name", c["name"])
            namespace = c["namespace"]
            full_name = f"{namespace}::{cname}" if namespace else cname
            
            serde_fields = []
            for p in c['props']:
                s_data = p['args'].pop('SEREALIZE', None)
                if s_data is not None:
                    serde_fields.append((p['name'], s_data))
                    
            if serde_fields:
                if namespace:
                    cpp_h_content += f"namespace {namespace} {{\n"
                
                cpp_h_content += f"    template<class Context>\n"
                cpp_h_content += f"    constexpr auto ikigai_serde(Context& context, {cname}& value) {{\n"
                cpp_h_content += f"        using namespace serde::attribute;\n"
                cpp_h_content += f"        serde::serde_struct(context, value)\n"
                
                for i, (s_name, s_args) in enumerate(serde_fields):
                    s_name_attr = s_args.get('name', f'"{s_name}"')
                    if 'default' in s_args:
                        def_val = s_args['default']
                        cpp_h_content += f"            .field(&{cname}::{s_name}, {s_name_attr}, default_{{{def_val}}})"
                    else:
                        cpp_h_content += f"            .field(&{cname}::{s_name}, {s_name_attr})"
                    
                    if i < len(serde_fields) - 1:
                        cpp_h_content += "\n"
                    else:
                        cpp_h_content += ";\n"
                        
                cpp_h_content += f"    }}\n"
                
                if namespace:
                    cpp_h_content += f"}}\n"
                cpp_h_content += "\n"
            else:
                if namespace:
                    cpp_h_content += f"namespace {namespace} {{\n"

                cpp_h_content += f"    template<class Context>\n"
                cpp_h_content += f"    constexpr auto ikigai_serde(Context& context, {cname}& value) {{\n"
                cpp_h_content += f"        using namespace serde::attribute;\n"
                cpp_h_content += f"        serde::serde_struct(context, value);\n"
                cpp_h_content += f"    }}\n"
                if namespace:
                    cpp_h_content += f"}}\n"
                cpp_h_content += "\n"

        cpp_c_content += f"namespace IKIGAI::UTILS {{\n"
        file_registrations = []

        for c in classes:
            cname = c["args"].get("Name", c["name"])
            namespace = c["namespace"]
            full_name = f"{namespace}::{cname}" if namespace else cname
            c_args = c["args"].copy()
            c_args.pop("Name", None)
            
            registry_code = []
            registry_code.append(f'            auto& m = IKIGAI::UTILS::ReflectionManager::Instance();')
            registry_code.append(f'            m.registerType<{full_name}>();')
            
            type_name = c_args.pop("TypeName", f'"class {full_name}"')
            comp_name = c_args.pop("ClassName", f'"{cname}"')
            
            registry_code.append(f'            m.registerStaticField<{full_name}, std::string>("TypeName", [](){{ return std::string({type_name}); }}, nullptr);')
            registry_code.append(f'            m.registerStaticField<{full_name}, std::string>("ClassName", [](){{ return std::string({comp_name}); }}, nullptr);')
            
            groups_val = c_args.pop("Groups", None)
            is_component = False
            if groups_val:
                groups_val = groups_val.strip()
                if groups_val.startswith('[') and groups_val.endswith(']'):
                    groups_content = groups_val[1:-1]
                    for group_name in groups_content.split(','):
                        group_name = group_name.strip()
                        if group_name:
                            registry_code.append(f'            m.addTypeToGroup<{full_name}>("{group_name}");')
                            if group_name == "Component":
                                is_component = True
                else:
                    registry_code.append(f'            m.addTypeToGroup<{full_name}>("{groups_val}");')
                    if groups_val == "Component":
                        is_component = True

            if is_component:
                registry_code.append(f'            m.registerStaticMethod<{full_name}>("AddComponent", static_cast<void(*)(std::shared_ptr<IKIGAI::ECS::Object>)>(')
                registry_code.append(f'                [](std::shared_ptr<IKIGAI::ECS::Object> obj) {{')
                registry_code.append(f'                    obj->addComponent<{full_name}>();')
                registry_code.append(f'                }}')
                registry_code.append(f'            ));')
                registry_code.append(f'            m.registerStaticMethod<{full_name}>("RemoveComponent", static_cast<void(*)(std::shared_ptr<IKIGAI::ECS::Object>)>(')
                registry_code.append(f'                [](std::shared_ptr<IKIGAI::ECS::Object> obj) {{')
                registry_code.append(f'                    obj->removeComponent<{full_name}>();')
                registry_code.append(f'                }}')
                registry_code.append(f'            ));')

            for key, val in c_args.items():
                ctype, val_expr = infer_cpp_type_and_val(val, key)
                registry_code.append(f'            m.registerStaticField<{full_name}, {ctype}>("{key}", [](){{ return {val_expr}; }}, nullptr);')
            
            for p in c['props']:
                getter_call = p['args'].pop('Getter', None)
                setter_call = p['args'].pop('Setter', None)
                
                meta_entries = []
                for k, v in p['args'].items():
                    m_type, m_val_expr = infer_cpp_type_and_val(v, k)
                    meta_entries.append(f'{{"{k}", {m_val_expr}}}')
                        
                meta_str = "IKIGAI::UTILS::Metadata{ " + ", ".join(meta_entries) + " }"
                
                if getter_call or setter_call:
                    l_get = f"[]({full_name}* obj) {{ return obj->{getter_call}(); }}" if getter_call else "nullptr"
                    l_set = f"[]({full_name}* obj, {p['type']} val) {{ obj->{setter_call}(val); }}" if setter_call else "nullptr"
                    
                    if p['is_static']:
                        registry_code.append(f'            m.registerStaticField<{full_name}, {p["type"]}>("{p["name"]}", {l_get}, {l_set}, {meta_str});')
                    else:
                        registry_code.append(f'            m.registerField<{full_name}, {p["type"]}>("{p["name"]}", {l_get}, {l_set}, {meta_str});')
                else:
                    if p['is_static']:
                        registry_code.append(f'            m.registerStaticField<{full_name}, {p["type"]}>("{p["name"]}", &{full_name}::{p["name"]}, {meta_str});')
                    else:
                        registry_code.append(f'            m.registerField<{full_name}, {p["type"]}>("{p["name"]}", &{full_name}::{p["name"]}, {meta_str});')

            for f in c['funcs']:
                meta_entries = []
                for k, v in f['args'].items():
                    m_type, m_val_expr = infer_cpp_type_and_val(v, k)
                    meta_entries.append(f'{{"{k}", {m_val_expr}}}')
                meta_str = "IKIGAI::UTILS::Metadata{ " + ", ".join(meta_entries) + " }"
                registry_code.append(f'            m.registerMethod("{f["name"]}", &{full_name}::{f["name"]}, {meta_str});')

            cpp_c_content += f"    template<>\n"
            cpp_c_content += f"    struct ReflectionReg<{full_name}> {{\n"
            cpp_c_content += f"        static void Register() {{\n"
            cpp_c_content += " \n".join(registry_code) + "\n"
            cpp_c_content += f"        }}\n"
            cpp_c_content += f"    }};\n"
            cnamef = cname.replace("::", "__")
            file_registrations.append(f"ReflectionReg<{full_name}>::Register();")

        for e in enums:
            ename = e["name"]
            namespace = e["namespace"]
            full_name = f"{namespace}::{ename}" if namespace else ename
            pairs_str = ", ".join([f'{{"{v}", static_cast<int>({full_name}::{v})}}' for v in e['values']])
            
            cpp_c_content += f"    struct ReflectionReg_Enum_{ename} {{\n"
            cpp_c_content += f"        static void Register() {{\n"
            cpp_c_content += f"            auto& m = IKIGAI::UTILS::ReflectionManager::Instance();\n"
            cpp_c_content += f'            m.registerEnum<{full_name}>("{ename}", {{ {pairs_str} }});\n'
            cpp_c_content += f"        }}\n"
            cpp_c_content += f"    }};\n"
            cnamef = ename.replace("::", "__")
            file_registrations.append(f"ReflectionReg_Enum_{ename}::Register();")

        cpp_c_content += f"void RegisterReflection_{name_no_ext}() {{\n"
        cpp_c_content += "\n".join("    " + call for call in file_registrations)
        cpp_c_content += "\n}\n}\n"


        feature = {'scriptComponent': 'USE_LUA', 'ikigaiScriptComponent': 'USE_IKIGAI_SCRIPT',
                   'physics2dComponent': 'USE_PHYSICS_2D', 'forceField2dComponent': 'USE_PHYSICS_2D',
                   'cloth2dComponent': 'USE_PHYSICS_2D',
                   'physics3dComponent': 'USE_PHYSICS_3D',
                   'physicsComponent': 'USE_PHYSICS_3D'}.get(name_no_ext)
        registrations.append((name_no_ext, feature))
        if feature:
            cpp_c_content = f'#if {feature}\n' + cpp_c_content + '\n#endif\n'
        out_cpp_filepath = os.path.join(output_dir_base, f'{name_no_ext}.generated.cpp')
        for generated_path, content in ((out_filepath, cpp_h_content), (out_cpp_filepath, cpp_c_content)):
            if os.path.exists(generated_path):
                with open(generated_path, 'r', encoding='utf-8') as f:
                    if f.read() == content:
                        continue
            with open(generated_path, 'w', encoding='utf-8') as f:
                f.write(content)


    registry = '// AUTO GENERATED FILE. DO NOT MODIFY.\n#include <mutex>\nnamespace IKIGAI::UTILS {\n'
    for name, feature in registrations:
        declaration = f'void RegisterReflection_{name}();\n'
        registry += f'#if {feature}\n{declaration}#endif\n' if feature else declaration
    registry += 'void RegisterGeneratedReflection() {\n    static std::once_flag once;\n    std::call_once(once, [] {\n'
    for name, feature in registrations:
        call = f'        RegisterReflection_{name}();\n'
        registry += f'#if {feature}\n{call}#endif\n' if feature else call
    registry += '    });\n}\n}\n'
    registry_path = os.path.join(output_dir_base, 'reflectionRegistry.generated.cpp')
    if not os.path.exists(registry_path) or open(registry_path, encoding='utf-8').read() != registry:
        with open(registry_path, 'w', encoding='utf-8') as output:
            output.write(registry)


def main():
    if len(sys.argv) < 3:
        print("Usage: python IkigaiHeaderTool.py <src_dir> <output_dir>")
        sys.exit(1)
        
    root_dir = sys.argv[1] 
    output_dir = sys.argv[2]
    
    all_files_data = []
    for subdir, directories, files in os.walk(root_dir):
        # Old in-source outputs are not inputs to reflection discovery.
        directories[:] = sorted(name for name in directories if name != 'generated')
        for file in sorted(files):
            if file.endswith('.h') or file.endswith('.hpp'):
                fpath = os.path.join(subdir, file)
                # Keep the configure-time manifest machine readable.
                with contextlib.redirect_stdout(sys.stderr):
                    classes, enums = process_file(fpath)
                if classes or enums:
                    all_files_data.append({
                        'header': fpath,
                        'classes': classes,
                        'enums': enums
                    })
                    
    # CMake queries the output manifest at configure time without generating code.
    names = [os.path.splitext(os.path.basename(item['header']))[0] for item in all_files_data]
    if len(names) != len(set(names)):
        raise ValueError("Reflected headers must have unique basenames")
    if '--list-outputs' in sys.argv[3:]:
        print('reflectionRegistry.generated.cpp')
        for name in sorted(names):
            print(name + '.generated.h')
            print(name + '.generated.cpp')
        return
    generate_headers(all_files_data, output_dir, root_dir)
    # A renamed/deleted reflected header must not leave a translation unit behind.
    expected = {os.path.splitext(os.path.basename(item['header']))[0] for item in all_files_data}
    expected.add('reflectionRegistry')
    for filename in os.listdir(output_dir):
        for suffix in ('.generated.h', '.generated.cpp'):
            if filename.endswith(suffix) and filename[:-len(suffix)] not in expected:
                path = os.path.join(output_dir, filename)
                with open(path, 'r', encoding='utf-8') as generated:
                    first_line = generated.readline().strip()
                    if first_line.startswith('#if USE_'):
                        first_line = generated.readline().strip()
                    owned = first_line == '// AUTO GENERATED FILE. DO NOT MODIFY.'
                if owned:
                    os.unlink(path)

if __name__ == '__main__':
    main()
