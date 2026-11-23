"""Tokenize IKI macro arguments, preserving source ranges and C++ literals."""
from dataclasses import dataclass

from .reflection_model import CppValue


@dataclass(frozen=True)
class Token:
    value: str
    start: int
    end: int


def lex(source):
    result = []
    i = 0
    while i < len(source):
        start = i
        c = source[i]
        if c.isspace():
            i += 1
            continue
        if source.startswith('//', i):
            end = source.find('\n', i)
            i = len(source) if end < 0 else end
            continue
        if source.startswith('/*', i):
            end = source.find('*/', i + 2)
            if end < 0:
                raise ValueError('Unterminated block comment')
            i = end + 2
            continue
        if c == '#' and not source[source.rfind('\n', 0, i) + 1:i].strip():
            while True:
                end = source.find('\n', i)
                if end < 0:
                    i = len(source)
                    break
                continued = source[i:end].rstrip().endswith('\\')
                i = end + 1
                if not continued:
                    break
            continue
        literal_prefix = next((prefix for prefix in ('u8R', 'uR', 'UR', 'LR', 'R', 'u8', 'u', 'U', 'L')
                               if source.startswith(prefix + '"', i)), '')
        if literal_prefix:
            i += len(literal_prefix)
            c = source[i]
        if literal_prefix.endswith('R'):
            opening = source.find('(', i + 1)
            if opening < 0:
                raise ValueError('Malformed raw string')
            closing = ')' + source[i + 1:opening] + '"'
            end = source.find(closing, opening + 1)
            if end < 0:
                raise ValueError('Unterminated raw string')
            i = end + len(closing)
        elif c in '\"\'':
            i += 1
            while i < len(source) and source[i] != c:
                i += 2 if source[i] == '\\' else 1
            if i >= len(source):
                raise ValueError('Unterminated string or character literal')
            i += 1
        elif c.isalnum() or c == '_':
            i += 1
            while i < len(source) and (source[i].isalnum() or source[i] == '_' or (c.isdigit() and source[i] == chr(39))):
                i += 1
        elif source.startswith('::', i):
            i += 2
        else:
            i += 1
        result.append(Token(source[start:i], start, i))
    return result


def matching(tokens, start):
    pairs = {'(': ')', '[': ']', '{': '}'}
    stack = []
    for i in range(start, len(tokens)):
        value = tokens[i].value
        if value in pairs:
            stack.append(pairs[value])
        elif value in pairs.values():
            if not stack or value != stack.pop():
                raise ValueError('Unbalanced annotation/declaration delimiters')
            if not stack:
                return i
    raise ValueError('Unterminated annotation/declaration')


def split_top(text, separator=','):
    tokens = lex(text)
    start = 0
    i = 0
    parts = []
    angle_depth = 0
    while i < len(tokens):
        token = tokens[i]
        if token.value in ('(', '[', '{'):
            i = matching(tokens, i)
        elif token.value == '<':
            angle_depth += 1
        elif token.value == '>' and angle_depth:
            angle_depth -= 1
        elif token.value == separator and angle_depth == 0:
            parts.append(text[start:token.start].strip())
            start = token.end
        i += 1
    parts.append(text[start:].strip())
    return parts


def arguments(text):
    result = {}
    for part in split_top(text):
        if not part:
            continue
        pieces = split_top(part, '=')
        if len(pieces) > 2:
            raise ValueError('Metadata expressions with = must be parenthesized')
        result[pieces[0]] = pieces[1] if len(pieces) == 2 else 'true'
    return result


def cpp_value(value, key=None):
    """Return the C++ type and initializer of an IKI metadata value."""
    value = value.strip()
    if key == "EditWidget":
        return CppValue("IKIGAI::UTILS::WidgetType", f"IKIGAI::UTILS::WidgetType::{value}")
    if value in ("true", "false"):
        return CppValue("bool", value)
    if value.startswith('"') and value.endswith('"'):
        return CppValue("std::string", f"std::string({value})")
    if value.startswith("[") and value.endswith("]"):
        return sequence_value(value[1:-1])
    if value.startswith("{") and value.endswith("}"):
        mapping = map_value(value[1:-1])
        if mapping is not None:
            return mapping
    if value.isidentifier():
        return CppValue("std::string", f'std::string("{value}")')
    try:
        int(value)
        return CppValue("int", value)
    except ValueError:
        pass
    try:
        float(value.removesuffix("f"))
        return CppValue("float", value)
    except ValueError:
        return CppValue(f"decltype({value})", value)


def sequence_value(contents):
    elements = split_top(contents, "|")
    if len(elements) > 1:
        container = "set"
    else:
        container = "vector"
        elements = split_top(contents)
    if not all(elements):
        raise ValueError("cannot infer an element type in an empty sequence")
    items = [cpp_value(element) for element in elements]
    cpp_type = f"std::{container}<{items[0].cpp_type}>"
    expressions = ", ".join(item.expression for item in items)
    return CppValue(cpp_type, f"{cpp_type}{{{expressions}}}")


def map_value(contents):
    if not contents.strip():
        raise ValueError("cannot infer an empty map")
    pairs = [split_top(entry, ":") for entry in split_top(contents)]
    if all(len(pair) == 1 for pair in pairs):
        return None
    if any(len(pair) != 2 or not all(pair) for pair in pairs):
        raise ValueError(f"expected key: value pairs in {{{contents}}}")
    items = [(cpp_value(key), cpp_value(value)) for key, value in pairs]
    first_key, first_value = items[0]
    cpp_type = f"std::map<{first_key.cpp_type}, {first_value.cpp_type}>"
    expressions = ", ".join(f"{{{key.expression}, {value.expression}}}" for key, value in items)
    return CppValue(cpp_type, f"{cpp_type}{{{expressions}}}")


def metadata(options):
    return {key: cpp_value(value, key) for key, value in options.items()}
