"""Declarations consumed by the reflection and serde code generators."""
from dataclasses import dataclass, field
from pathlib import Path
from typing import NamedTuple


class CppValue(NamedTuple):
    cpp_type: str
    expression: str


@dataclass(frozen=True)
class Property:
    name: str
    cpp_type: str
    is_static: bool
    getter: str | None
    setter: str | None
    serde: dict[str, str] | None
    metadata: dict[str, CppValue]


@dataclass(frozen=True)
class Method:
    name: str
    metadata: dict[str, CppValue]


@dataclass
class ReflectedClass:
    name: str
    namespace: str
    groups: tuple[str, ...]
    type_name: str
    class_name: str
    metadata: dict[str, CppValue]
    properties: list[Property] = field(default_factory=list)
    methods: list[Method] = field(default_factory=list)

    @property
    def qualified_name(self):
        return "::".join(filter(None, (self.namespace, self.name)))


@dataclass(frozen=True)
class ReflectedEnum:
    name: str
    namespace: str
    values: tuple[str, ...]

    @property
    def qualified_name(self):
        return "::".join(filter(None, (self.namespace, self.name)))


@dataclass
class Header:
    path: Path
    classes: list[ReflectedClass] = field(default_factory=list)
    enums: list[ReflectedEnum] = field(default_factory=list)
