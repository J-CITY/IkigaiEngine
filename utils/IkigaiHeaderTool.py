import os
import re
import sys

# Regex definitions
class_re = re.compile(r'^\s*IKI_CLASS\((.*?)\)[\s\S]*?(?:class|struct)\s+(?:[A-Z_0-9]+_API\s+)?(\w+)', re.MULTILINE)
enum_re = re.compile(r'^\s*IKI_ENUM\((.*?)\)[\s\S]*?enum\s+(?:class\s+)?(\w+)[^{]*{([^}]*)}', re.MULTILINE)

def parse_balanced_args(text, start_index):
    lvl = 1
    for i in range(start_index + 1, len(text)):
        if text[i] == '(': lvl += 1
        elif text[i] == ')':
            lvl -= 1
            if lvl == 0:
                return text[start_index+1:i], i+1
    return None, -1

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
            
    if re.match(r'^[a-zA-Z_][a-zA-Z0-9_]*$', val):
        return "std::string", f'std::string("{val}")'
        
    return f"decltype({val})", val

def parse_args_string(arg_str):
    args = {}
    if not arg_str.strip(): return args
    
    parts = []
    lvl = 0
    curr = ""
    for char in arg_str:
        if char in '({[': lvl += 1
        elif char in ')}]': lvl -= 1
        
        if char == ',' and lvl == 0:
            parts.append(curr.strip())
            curr = ""
        else:
            curr += char
    if curr: parts.append(curr.strip())
            
    for p in parts:
        eq_idx = -1
        lvl_eq = 0
        for i, char in enumerate(p):
            if char in '({[': lvl_eq += 1
            elif char in ')}]': lvl_eq -= 1
            elif char == '=' and lvl_eq == 0:
                eq_idx = i
                break
                
        if eq_idx != -1:
            k = p[:eq_idx].strip()
            v = p[eq_idx+1:].strip()
            args[k] = v
        else:
            args[p.strip()] = "true"
    return args

def extract_enum_values(body):
    pairs = []
    for item in body.split(','):
        item = item.strip()
        if not item: continue
        if '//' in item: item = item.split('//')[0].strip()
        if not item: continue
        
        if '=' in item:
            name, _ = item.split('=', 1)
            pairs.append(name.strip())
        else:
            pairs.append(item.strip())
    return pairs

def strip_specifiers(type_str):
    specifiers = ['inline', 'static', 'constexpr', 'virtual', 'override', 'final', 'extern', 'explicit']
    for spec in specifiers:
        type_str = re.sub(rf'\b{spec}\b', '', type_str)
    type_str = re.sub(r'\s+', ' ', type_str).strip()
    return type_str

def extract_properties(class_content, filepath):
    props = []
    idx = 0
    while True:
        idx = class_content.find('IKI_PROPERTY', idx)
        if idx == -1: break
        
        paren_idx = class_content.find('(', idx)
        if paren_idx == -1:
            idx += 12
            continue
            
        args_str, end_idx = parse_balanced_args(class_content, paren_idx)
        if args_str is None:
            idx += 12
            continue
            
        p_args = parse_args_string(args_str)
        
        # Check if any key is SEREALIZE(...)
        serealize_data = None
        keys_to_remove = []
        for k in p_args.keys():
            if k.startswith('SEREALIZE(') and k.endswith(')'):
                serealize_data = parse_args_string(k[10:-1])
                keys_to_remove.append(k)
        for k in keys_to_remove:
            del p_args[k]
            
        if serealize_data is not None:
            p_args['SEREALIZE'] = serealize_data
            
        has_name = 'Name' in p_args
        has_type = 'Type' in p_args
        is_static = False
        
        if has_name and not has_type:
            print(f"Error in {filepath}: IKI_PROPERTY '{p_args['Name']}' specifies Name but missing Type.")
            sys.exit(1)
        if has_type and not has_name:
            print(f"Error in {filepath}: IKI_PROPERTY specifies Type '{p_args['Type']}' but missing Name.")
            sys.exit(1)
            
        if has_name and has_type:
            prop_name = p_args.pop('Name')
            prop_type = p_args.pop('Type')
        else:
            search_end = class_content.find(';', end_idx)
            if search_end == -1: search_end = len(class_content)
            
            field_str = class_content[end_idx:search_end].strip()
            field_str = field_str.split('=')[0].strip()
            
            is_static = 'static' in field_str.split()
            
            tokens = []
            lvl = 0
            curr = ""
            for c in field_str:
                if c == '<': lvl += 1
                elif c == '>': lvl -= 1
                if c.isspace() and lvl == 0:
                    if curr: tokens.append(curr)
                    curr = ""
                else:
                    curr += c
            if curr: tokens.append(curr)
            
            if len(tokens) < 2:
                print(f"Error in {filepath}: Could not parse field below IKI_PROPERTY! Field str: '{field_str}'")
                sys.exit(1)
                
            prop_name = tokens[-1]
            raw_type = " ".join(tokens[:-1])
            prop_type = strip_specifiers(raw_type)
            
        props.append({
            'name': prop_name,
            'type': prop_type,
            'is_static': is_static,
            'args': p_args
        })
        
        idx = end_idx
        
    return props

def extract_functions(class_content, filepath):
    funcs = []
    idx = 0
    while True:
        idx = class_content.find('IKI_FUNCTION', idx)
        if idx == -1: break
        
        paren_idx = class_content.find('(', idx)
        if paren_idx == -1:
            idx += 12
            continue
            
        args_str, end_idx = parse_balanced_args(class_content, paren_idx)
        if args_str is None:
            idx += 12
            continue
            
        p_args = parse_args_string(args_str)
        
        search_end = class_content.find(';', end_idx)
        bracket_end = class_content.find('{', end_idx)
        
        if search_end == -1: search_end = len(class_content)
        if bracket_end != -1 and bracket_end < search_end:
            search_end = bracket_end
            
        func_str = class_content[end_idx:search_end].strip()
        
        match = re.search(r'(\w+)\s*\(', func_str)
        if match:
            func_name = match.group(1)
            funcs.append({
                'name': func_name,
                'args': p_args
            })
        else:
            print(f"Warning in {filepath}: Could not parse function name below IKI_FUNCTION! str: '{func_str}'")
            
        idx = end_idx
    return funcs

def process_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
        
    enums = []
    for enum_match in enum_re.finditer(content):
        enum_args_str = enum_match.group(1)
        enum_name = enum_match.group(2)
        enum_body = enum_match.group(3)
        
        ns_matches = list(re.finditer(r'namespace\s+([a-zA-Z0-9_:]+)', content[:enum_match.start()]))
        namespace = ns_matches[-1].group(1) if ns_matches else ""
        
        enums.append({
            'name': enum_name,
            'namespace': namespace,
            'values': extract_enum_values(enum_body),
            'args': parse_args_string(enum_args_str)
        })
    
    classes = []
    for class_match in class_re.finditer(content):
        class_args_str = class_match.group(1)
        class_name = class_match.group(2)
        
        ns_matches = list(re.finditer(r'namespace\s+([a-zA-Z0-9_:]+)', content[:class_match.start()]))
        namespace = ns_matches[-1].group(1) if ns_matches else ""
        
        start_pos = class_match.end()
        brace_idx = content.find('{', start_pos)
        end_pos = len(content)
        if brace_idx != -1:
            lvl = 1
            for i in range(brace_idx + 1, len(content)):
                if content[i] == '{': lvl += 1
                elif content[i] == '}':
                    lvl -= 1
                    if lvl == 0:
                        end_pos = i + 1
                        break
        
        class_content = content[start_pos:end_pos]
        
        masked_content = class_content
        nested_idx = 0
        while True:
            nested_idx = masked_content.find('IKI_CLASS', nested_idx)
            if nested_idx == -1: break
            
            nested_brace = masked_content.find('{', nested_idx)
            if nested_brace != -1:
                n_lvl = 1
                n_end = len(masked_content)
                for i in range(nested_brace + 1, len(masked_content)):
                    if masked_content[i] == '{': n_lvl += 1
                    elif masked_content[i] == '}':
                        n_lvl -= 1
                        if n_lvl == 0:
                            n_end = i + 1
                            break
                masked_content = masked_content[:nested_brace] + " " * (n_end - nested_brace) + masked_content[n_end:]
            nested_idx += 9
            
        props = extract_properties(masked_content, filepath)
        funcs = extract_functions(masked_content, filepath)
            
        classes.append({
            'name': class_name,
            'namespace': namespace,
            'args': parse_args_string(class_args_str),
            'props': props,
            'funcs': funcs,
            'header': filepath
        })
        
    return classes, enums

def generate_headers(all_files_data, output_dir_base, root_dir):
    os.makedirs(output_dir_base, exist_ok=True)
    
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
            cpp_c_content += f"        ReflectionReg() {{\n"
            cpp_c_content += " \n".join(registry_code) + "\n"
            cpp_c_content += f"        }}\n"
            cpp_c_content += f"    }};\n"
            cnamef = cname.replace("::", "__")
            cpp_c_content += f"    static ReflectionReg<{full_name}> _is_registered_{cnamef};\n\n"

        for e in enums:
            ename = e["name"]
            namespace = e["namespace"]
            full_name = f"{namespace}::{ename}" if namespace else ename
            pairs_str = ", ".join([f'{{"{v}", static_cast<int>({full_name}::{v})}}' for v in e['values']])
            
            cpp_c_content += f"    struct ReflectionReg_Enum_{ename} {{\n"
            cpp_c_content += f"        ReflectionReg_Enum_{ename}() {{\n"
            cpp_c_content += f"            auto& m = IKIGAI::UTILS::ReflectionManager::Instance();\n"
            cpp_c_content += f'            m.registerEnum<{full_name}>("{ename}", {{ {pairs_str} }});\n'
            cpp_c_content += f"        }}\n"
            cpp_c_content += f"    }};\n"
            cnamef = ename.replace("::", "__")
            cpp_c_content += f"    static ReflectionReg_Enum_{ename} _is_registered_Enum_{cnamef};\n\n"

        cpp_c_content += f"}}\n\n"

        out_cpp_filepath = os.path.join(output_dir_base, f"{name_no_ext}.generated.cpp")
        for generated_path, content in ((out_filepath, cpp_h_content), (out_cpp_filepath, cpp_c_content)):
            if os.path.exists(generated_path):
                with open(generated_path, 'r', encoding='utf-8') as f:
                    if f.read() == content:
                        continue
            with open(generated_path, 'w', encoding='utf-8') as f:
                f.write(content)

def main():
    if len(sys.argv) < 3:
        print("Usage: python IkigaiHeaderTool.py <src_dir> <output_dir>")
        sys.exit(1)
        
    root_dir = sys.argv[1] 
    output_dir = sys.argv[2]
    
    all_files_data = []
    for subdir, _, files in os.walk(root_dir):
        for file in files:
            if file.endswith('.h') or file.endswith('.hpp'):
                fpath = os.path.join(subdir, file)
                classes, enums = process_file(fpath)
                if classes or enums:
                    all_files_data.append({
                        'header': fpath,
                        'classes': classes,
                        'enums': enums
                    })
                    
    generate_headers(all_files_data, output_dir, root_dir)

if __name__ == '__main__':
    main()
