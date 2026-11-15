"""Structural parser for the deliberately restricted IKI annotation language.

This is not a C++ preprocessor: annotations must not depend on macro expansion or
conditional alternatives. Comments, literals, nested scopes and delimiters are
lexed; unsupported annotated declarators fail instead of silently generating code.
"""
from dataclasses import dataclass


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


def parse(source, filepath):
    tokens = lex(source)
    classes, enums = [], []

    def text(begin, end):
        return source[tokens[begin].start:tokens[end - 1].end] if begin < end else ''

    def scope(begin, end, namespace, owner=None, reflected=None):
        i = begin
        while i < end:
            value = tokens[i].value
            if value == 'namespace':
                j = i + 1
                while j < end and tokens[j].value not in ('{', ';', '='):
                    j += 1
                if j < end and tokens[j].value == '{':
                    names = ''.join(t.value for t in tokens[i + 1:j])
                    close = matching(tokens, j)
                    scope(j + 1, close, '::'.join(x for x in (namespace, names) if x))
                    i = close + 1
                    continue
            if value in ('IKI_CLASS', 'IKI_ENUM', 'IKI_PROPERTY', 'IKI_FUNCTION'):
                if i + 1 >= end or tokens[i + 1].value != '(':
                    raise ValueError(f'{filepath}: expected annotation arguments')
                close = matching(tokens, i + 1)
                args = arguments(text(i + 2, close))
                j = close + 1
                if value in ('IKI_CLASS', 'IKI_ENUM'):
                    expected = ('class', 'struct') if value == 'IKI_CLASS' else ('enum',)
                    if j >= end or tokens[j].value not in expected:
                        raise ValueError(f'{filepath}: annotation must immediately precede {expected}')
                    j += 1
                    if value == 'IKI_ENUM' and tokens[j].value in ('class', 'struct'):
                        j += 1
                    if tokens[j].value.endswith('_API'):
                        j += 1
                    name = tokens[j].value
                    if not name.isidentifier():
                        raise ValueError(f'{filepath}: unsupported annotated declaration')
                    j += 1
                    while j < end and tokens[j].value not in ('{', ';'):
                        j += 1
                    if j >= end or tokens[j].value != '{':
                        raise ValueError(f'{filepath}: reflected declaration requires a body')
                    body_end = matching(tokens, j)
                    qualified = '::'.join(x for x in (owner, name) if x)
                    if value == 'IKI_ENUM':
                        values = [lex(part)[0].value for part in split_top(text(j + 1, body_end)) if part]
                        enums.append(dict(name=qualified, namespace=namespace, args=args, values=values))
                    else:
                        declared_name = args.get('Name', qualified)
                        if declared_name != qualified:
                            raise ValueError(f'{filepath}: Name must match nested C++ name {qualified}')
                        args['Name'] = qualified
                        item = dict(name=name, namespace=namespace, args=args, props=[], funcs=[], header=filepath)
                        classes.append(item)
                        scope(j + 1, body_end, namespace, qualified, item)
                    i = body_end + 1
                    continue
                if reflected is None:
                    raise ValueError(f'{filepath}: member annotation outside reflected class')
                if value == 'IKI_FUNCTION':
                    while j < end and tokens[j].value not in ('(', ';', '{'):
                        j += 1
                    if j >= end or tokens[j].value != '(' or not tokens[j - 1].value.isidentifier():
                        raise ValueError(f'{filepath}: unsupported reflected function declarator')
                    reflected['funcs'].append(dict(name=tokens[j - 1].value, args=args))
                else:
                    for key in list(args):
                        if key.startswith('SEREALIZE('):
                            args['SEREALIZE'] = arguments(key[len('SEREALIZE('):-1])
                            del args[key]
                    if ('Name' in args) != ('Type' in args):
                        raise ValueError(f'{filepath}: property requires both Name and Type')
                    is_static = False
                    if 'Name' in args:
                        name, field_type = args.pop('Name'), args.pop('Type')
                    else:
                        field_start = j
                        while j < end and tokens[j].value not in (';', '=', '{'):
                            j += 1
                        field = tokens[field_start:j]
                        if len(field) < 2 or not field[-1].value.isidentifier():
                            raise ValueError(f'{filepath}: unsupported field; use explicit Name and Type')
                        name = field[-1].value
                        is_static = any(t.value == 'static' for t in field)
                        field_type = ' '.join(t.value for t in field[:-1] if t.value not in ('inline', 'static', 'constexpr'))
                        if any(t.value in ('(', ')', '[', ']', ',') for t in field[:-1]) and '<' not in field_type:
                            raise ValueError(f'{filepath}: unsupported field; use explicit Name and Type')
                    reflected['props'].append(dict(name=name, type=field_type, is_static=is_static, args=args))
                i = close + 1
                continue
            if value == '{':
                i = matching(tokens, i) + 1
            else:
                i += 1
    scope(0, len(tokens), '')
    return classes, enums
