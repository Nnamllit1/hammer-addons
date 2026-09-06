"""Span-preserving text DMX editing; binary conversion is handled by Valve."""
from __future__ import annotations

import re
import uuid
from collections import Counter
from dataclasses import dataclass, field


def quote(value: str) -> str:
    if any(ord(c) < 32 for c in value):
        raise ValueError("DMX values cannot contain control characters.")
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


@dataclass
class Token:
    value: str
    start: int
    end: int
    quoted: bool = False


@dataclass
class Attribute:
    name: str
    kind: str
    value: object
    start: int
    end: int


@dataclass
class Element:
    kind: str
    start: int
    end: int = 0
    attributes: dict[str, Attribute] = field(default_factory=dict)

    def scalar(self, key: str, default: str = "") -> str:
        attr = self.attributes.get(key)
        return attr.value if attr and isinstance(attr.value, str) else default


class Document:
    def __init__(self, text: str):
        if not re.match(r"\s*<!-- dmx encoding keyvalues2(?:_flat)? \d+ format vmap \d+ -->", text):
            raise ValueError("Expected a text keyvalues2 VMAP; convert binary maps with dmxconvert first.")
        self.text = text
        self.tokens = []
        pattern = re.compile(r'\s+|<!--.*?-->|//[^\n]*|"(?:\\.|[^"\\])*"|[{}\[\],]', re.S)
        pos = 0
        for match in pattern.finditer(text):
            if match.start() != pos:
                raise ValueError(f"Unsupported DMX syntax at offset {pos}")
            pos = match.end()
            value = match[0]
            if value.isspace() or value.startswith(("<!--", "//")):
                continue
            quoted = value.startswith('"')
            if quoted:
                value = re.sub(r'\\(["\\])', r'\1', value[1:-1])
            self.tokens.append(Token(value, match.start(), match.end(), quoted))
        if pos != len(text):
            raise ValueError(f"Unsupported DMX syntax at offset {pos}")
        self.index = 0
        self.elements: list[Element] = []
        while self.index < len(self.tokens):
            self.element(self.pop().value)

    def pop(self, expected: str | None = None) -> Token:
        if self.index >= len(self.tokens):
            raise ValueError("Unexpected end of DMX")
        token = self.tokens[self.index]
        self.index += 1
        if expected is not None and (token.quoted or token.value != expected):
            raise ValueError(f"Expected {expected!r} at offset {token.start}")
        return token

    def at(self, value: str) -> bool:
        return self.index < len(self.tokens) and not self.tokens[self.index].quoted and self.tokens[self.index].value == value

    def element(self, kind: str) -> Element:
        elem = Element(kind, self.pop("{").start)
        self.elements.append(elem)
        while not self.at("}"):
            key, typ = self.pop().value, self.pop().value
            if self.index >= len(self.tokens):
                raise ValueError("Missing DMX attribute value")
            start = self.tokens[self.index].start
            if self.at("{"):
                value = self.element(typ)
            elif self.at("["):
                self.pop("[")
                value = []
                while not self.at("]"):
                    item = self.pop()
                    if typ == "element_array":
                        if self.at("{"):
                            value.append(self.element(item.value))
                        elif item.value == "element":
                            value.append(self.pop().value)
                        else:
                            raise ValueError("Unsupported element array entry")
                    else:
                        value.append(item.value)
                    if self.at(","):
                        self.pop(",")
                self.pop("]")
            else:
                value = self.pop().value
            if key in elem.attributes:
                raise ValueError(f"Duplicate DMX attribute: {key}")
            elem.attributes[key] = Attribute(key, typ, value, start, self.tokens[self.index - 1].end)
        elem.end = self.pop("}").end
        return elem

    def properties(self, elem: Element) -> Element | None:
        attr = elem.attributes.get("entity_properties")
        if attr and isinstance(attr.value, Element):
            return attr.value
        if attr and isinstance(attr.value, str):
            return next((e for e in self.elements if e.scalar("id") == attr.value), None)
        return None

    def entities(self) -> list[dict]:
        result = []
        for elem in self.elements:
            if elem.kind != "CMapEntity":
                continue
            props = self.properties(elem)
            values = {k: a.value for k, a in props.attributes.items() if a.kind == "string"} if props else {}
            result.append({"id": elem.scalar("id"), "node_id": elem.scalar("nodeID"),
                           "classname": values.get("classname", ""), "origin": elem.scalar("origin"),
                           "angles": elem.scalar("angles"), "properties": values})
        return result

    def summary(self) -> dict:
        entities = self.entities()
        counts = dict(Counter(e["classname"] for e in entities))
        warnings = []
        for classname in ("info_player_terrorist", "info_player_counterterrorist"):
            if not counts.get(classname):
                warnings.append(f"No {classname} spawn entity found.")
        ids = [e.scalar("id") for e in self.elements if e.scalar("id")]
        if len(ids) != len(set(ids)):
            warnings.append("Duplicate DMX element IDs found.")
        return {"entities": len(entities), "meshes": sum(e.kind == "CMapMesh" for e in self.elements),
                "classes": counts, "warnings": warnings,
                "validation_scope": "Structure and entity presence only; compile/playtest to check geometry and gameplay."}

    def edit_entity(self, entity_id: str, properties: dict[str, str], origin: str | None, angles: str | None) -> str:
        matches = [e for e in self.elements if e.kind == "CMapEntity" and e.scalar("id") == entity_id]
        if len(matches) != 1:
            raise ValueError("Expected one entity with the supplied DMX UUID.")
        elem = matches[0]
        props = self.properties(elem)
        if props is None:
            raise ValueError("Entity has no editable property block.")
        edits = []
        def update(target: Element, values: dict, default_type: str):
            additions = []
            for key, value in values.items():
                if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key) or key in ("id", "nodeID", "referenceID"):
                    raise ValueError(f"Invalid editable property: {key}")
                attr = target.attributes.get(key)
                if attr:
                    if not isinstance(attr.value, str) or attr.kind not in ("string", "vector3", "qangle"):
                        raise ValueError(f"Cannot replace {key} of type {attr.kind}")
                    edits.append((attr.start, attr.end, quote(value)))
                else:
                    additions.append(f'\n\t{quote(key)} "{default_type}" {quote(value)}\n')
            if additions:
                edits.append((target.end - 1, target.end - 1, "".join(additions)))
        update(props, properties, "string")
        if origin is not None:
            update(elem, {"origin": origin}, "vector3")
        if angles is not None:
            update(elem, {"angles": angles}, "qangle")
        text = self.text
        for start, end, value in sorted(edits, reverse=True):
            text = text[:start] + value + text[end:]
        Document(text)
        return text

    def add_entity(self, classname: str, properties: dict[str, str], origin: str, angles: str) -> tuple[str, str]:
        worlds = [e for e in self.elements if e.kind == "CMapWorld"]
        if len(worlds) != 1:
            raise ValueError("Expected exactly one CMapWorld.")
        children = worlds[0].attributes.get("children")
        if children is None or children.kind != "element_array":
            raise ValueError("World has no children array.")
        eid = str(uuid.uuid4())
        node = max((int(e.scalar("nodeID", "0")) for e in self.elements), default=0) + 1
        for key in properties:
            if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key) or key in ("id", "classname"):
                raise ValueError(f"Invalid property: {key}")
        lines = "\n".join(f'{quote(k)} "string" {quote(v)}' for k, v in {"classname": classname, **properties}.items())
        entity = f'''"CMapEntity"
{{
"id" "elementid" "{eid}"
"nodeID" "int" "{node}"
"referenceID" "uint64" "0x0"
"children" "element_array" []
"connectionsData" "element_array" []
"entity_properties" "EditGameClassProps"
{{
"id" "elementid" "{uuid.uuid4()}"
{lines}
}}
"origin" "vector3" {quote(origin)}
"angles" "qangle" {quote(angles)}
"scales" "vector3" "1 1 1"
"hitNormal" "vector3" "0 0 1"
"isProceduralEntity" "bool" "0"
"transformLocked" "bool" "0"
"force_hidden" "bool" "0"
"editorOnly" "bool" "0"
}}'''
        insertion = "\n" + entity + (",\n" if children.value else "\n")
        text = self.text[:children.start + 1] + insertion + self.text[children.start + 1:]
        Document(text)
        return text, eid
