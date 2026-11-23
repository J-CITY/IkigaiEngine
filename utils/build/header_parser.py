"""Associate IKI annotations with declarations in a tree-sitter C++ syntax tree."""
from dataclasses import dataclass
from pathlib import Path

from tree_sitter import Language, Parser
import tree_sitter_cpp

from .reflection_annotations import arguments, lex, matching, metadata, split_top
from .reflection_model import Header, Method, Property, ReflectedClass, ReflectedEnum


CPP = Language(tree_sitter_cpp.language())
CLASS_NODES = {"class_specifier", "struct_specifier"}
MEMBER_NODES = {"field_declaration", "function_definition", "declaration"}
MARKERS = {"IKI_CLASS", "IKI_PROPERTY", "IKI_FUNCTION", "IKI_ENUM"}


@dataclass(frozen=True)
class Annotation:
    kind: str
    options: dict[str, str]
    offset: int
    following: int
    line: int


def annotated_source(source, path):
    """Blank macros in the parser input, retaining original UTF-8 byte positions."""
    tokens = lex(source)
    data = bytearray(source.encode())
    offsets = [0]
    for char in source:
        offsets.append(offsets[-1] + len(char.encode()))
    annotations = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        index += 1
        if token.value.endswith("_API") and index >= 2 and tokens[index - 2].value in {"class", "struct", "enum"}:
            end = token.end
        elif token.value in MARKERS or token.value == "IKI_GENERATED_BODY":
            line = source.count("\n", 0, token.start) + 1
            try:
                if index == len(tokens) or tokens[index].value != "(":
                    raise ValueError("expected macro arguments")
                close = matching(tokens, index)
                end = tokens[close].end
                if token.value in MARKERS:
                    options = arguments(source[tokens[index].end:tokens[close].start])
                    following = tokens[close + 1].start if close + 1 < len(tokens) else len(source)
                    annotations.append(Annotation(token.value, options, offsets[token.start], offsets[following], line))
                index = close + 1
            except ValueError as error:
                raise ValueError(f"{path}:{line}: {error}") from error
        else:
            continue
        start_byte, end_byte = offsets[token.start], offsets[end]
        data[start_byte:end_byte] = bytes(c if c in (10, 13) else 32 for c in data[start_byte:end_byte])
    return bytes(data), annotations


def ancestors(node):
    while node is not None:
        yield node
        node = node.parent


def spelling(node):
    return node.text.decode()


def identifier(node):
    name = node.child_by_field_name("name")
    if name is None or not name.text:
        raise ValueError("expected a named declaration")
    return spelling(name)


def declaration_at(root, offset, kinds):
    leaf = root.descendant_for_byte_range(offset, offset)
    for node in ancestors(leaf):
        if node.start_byte == offset and node.type in kinds:
            return node
    raise ValueError("expected an annotated C++ declaration")


def enclosing_names(node, kinds):
    return "::".join(reversed([identifier(parent) for parent in ancestors(node) if parent.type in kinds]))


def type_tokens(node, end):
    """Spell the AST type and pointer qualifiers up to the field identifier."""
    if node.start_byte >= end or node.type in {"comment", "attribute_declaration", "storage_class_specifier"}:
        return
    if not node.children:
        if spelling(node) != "constexpr":
            yield spelling(node)
        return
    for child in node.children:
        yield from type_tokens(child, end)


def field_declaration(node):
    declarators = node.children_by_field_name("declarator")
    if len(declarators) != 1 or node.has_error:
        raise ValueError("expected one field; use explicit Name and Type")
    declarator = declarators[0]
    while declarator.type in {"pointer_declarator", "reference_declarator"}:
        declarator = declarator.named_children[-1]
    if declarator.type != "field_identifier":
        raise ValueError("unsupported field declarator; use explicit Name and Type")
    cpp_type = " ".join(type_tokens(node, declarator.start_byte))
    is_static = any(child.type == "storage_class_specifier" and child.text == b"static" for child in node.children)
    return spelling(declarator), cpp_type, is_static


def property_from(node, options):
    name = options.pop("Name", None)
    cpp_type = options.pop("Type", None)
    if (name is None) != (cpp_type is None):
        raise ValueError("a virtual property requires both Name and Type")
    is_static = False
    if name is None:
        name, cpp_type, is_static = field_declaration(node)
    serde = None
    for key in tuple(options):
        if key.startswith("SEREALIZE("):
            serde = arguments(key[len("SEREALIZE("):-1])
            del options[key]
    getter = options.pop("Getter", None)
    setter = options.pop("Setter", None)
    return Property(name, cpp_type, is_static, getter, setter, serde, metadata(options))


def class_from(name, namespace, options):
    if options.pop("Name", name) != name:
        raise ValueError(f"Name must match nested C++ name {name}")
    groups = options.pop("Groups", "")
    if groups.startswith("[") and groups.endswith("]"):
        groups = split_top(groups[1:-1])
    else:
        groups = [groups]
    qualified = "::".join(filter(None, (namespace, name)))
    return ReflectedClass(
        name=name,
        namespace=namespace,
        groups=tuple(filter(None, groups)),
        type_name=options.pop("TypeName", f'"class {qualified}"'),
        class_name=options.pop("ClassName", f'"{name}"'),
        metadata=metadata(options),
    )


def method_from(node, options):
    declarator = node.child_by_field_name("declarator")
    while declarator is not None and declarator.type != "function_declarator":
        declarator = declarator.child_by_field_name("declarator")
    name = declarator.child_by_field_name("declarator") if declarator else None
    if name is None or name.type != "field_identifier":
        raise ValueError("unsupported reflected function declarator")
    return Method(spelling(name), metadata(options))


def read_annotation(root, marker, header, owners):
    options = marker.options.copy()
    if marker.kind in {"IKI_CLASS", "IKI_ENUM"}:
        kinds = CLASS_NODES if marker.kind == "IKI_CLASS" else {"enum_specifier"}
        node = declaration_at(root, marker.following, kinds)
        body = node.child_by_field_name("body")
        if body is None:
            raise ValueError("reflected declaration requires a body")
        namespace = enclosing_names(node.parent, {"namespace_definition"})
        name = enclosing_names(node, CLASS_NODES | {"enum_specifier"})
        if marker.kind == "IKI_ENUM":
            values = tuple(identifier(child) for child in body.named_children if child.type == "enumerator")
            header.enums.append(ReflectedEnum(name, namespace, values))
        else:
            reflected = class_from(name, namespace, options)
            owners[node.id] = reflected
            header.classes.append(reflected)
        return

    # Explicit properties can be adjacent and have no corresponding declaration.
    scope = root.descendant_for_byte_range(marker.offset, marker.offset)
    while scope is not None and scope.type.startswith("preproc_"):
        scope = scope.parent
    if scope is None or scope.type != "field_declaration_list" or scope.parent.id not in owners:
        raise ValueError("member annotation outside a reflected class body")
    owner = owners[scope.parent.id]
    if marker.kind == "IKI_PROPERTY" and ("Name" in options or "Type" in options):
        owner.properties.append(property_from(None, options))
        return
    node = declaration_at(root, marker.following, MEMBER_NODES)
    if marker.kind == "IKI_PROPERTY":
        owner.properties.append(property_from(node, options))
    else:
        owner.methods.append(method_from(node, options))


def parse(source: str, path: Path) -> Header:
    header = Header(path)
    if not any(marker in source for marker in MARKERS):
        return header
    data, annotations = annotated_source(source, path)
    root = Parser(CPP).parse(data).root_node
    owners = {}
    for annotation in annotations:
        try:
            read_annotation(root, annotation, header, owners)
        except ValueError as error:
            raise ValueError(f"{path}:{annotation.line}: {annotation.kind}: {error}") from error
    return header
