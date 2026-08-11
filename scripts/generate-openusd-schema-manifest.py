#!/usr/bin/env python3
"""Generate a deterministic compact manifest from OpenUSD schema.usda files.

The generated JSON is build-time documentation/test data. TinyUSDZ does not
depend on OpenUSD at runtime. The parser intentionally understands only the
declarative schema.usda surface: classes, inheritance, API kind, and top-level
property declarations.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
from typing import Iterator


CLASS_RE = re.compile(
    r'^class\s+(?:(?P<abstract>"[^"]+")|(?P<cpp>[A-Za-z_]\w*)\s+'
    r'(?P<typed>"[^"]+"))\s*\(', re.MULTILINE)
PROPERTY_RE = re.compile(
    r"^(?:custom\s+)?(?:(uniform|varying)\s+)?"
    r"([A-Za-z_][A-Za-z0-9_:]*(?:\[\])?)\s+"
    r"([A-Za-z_][A-Za-z0-9_:]*)(.*)$", re.DOTALL)


def _skip_string(text: str, pos: int) -> int:
    quote = text[pos]
    triple = text.startswith(quote * 3, pos)
    pos += 3 if triple else 1
    terminator = quote * (3 if triple else 1)
    while pos < len(text):
        if text.startswith(terminator, pos):
            return pos + len(terminator)
        if not triple and text[pos] == "\\":
            pos += 2
        else:
            pos += 1
    raise ValueError("unterminated string in schema.usda")


def _matching(text: str, pos: int, opening: str, closing: str) -> int:
    if pos >= len(text) or text[pos] != opening:
        raise ValueError(f"expected {opening!r} at byte {pos}")
    depth = 1
    pos += 1
    while pos < len(text):
        char = text[pos]
        if char in "\"'":
            pos = _skip_string(text, pos)
            continue
        if char == "#":
            newline = text.find("\n", pos)
            pos = len(text) if newline < 0 else newline + 1
            continue
        if char == opening:
            depth += 1
        elif char == closing:
            depth -= 1
            if depth == 0:
                return pos
        pos += 1
    raise ValueError(f"unterminated {opening}{closing} block")


def _statements(body: str) -> Iterator[str]:
    start = 0
    paren = bracket = brace = 0
    pos = 0
    while pos < len(body):
        char = body[pos]
        if char in "\"'":
            pos = _skip_string(body, pos)
            continue
        if char == "#":
            newline = body.find("\n", pos)
            pos = len(body) if newline < 0 else newline
            continue
        if char == "(":
            paren += 1
        elif char == ")":
            paren -= 1
        elif char == "[":
            bracket += 1
        elif char == "]":
            bracket -= 1
        elif char == "{":
            brace += 1
        elif char == "}":
            brace -= 1
        elif char == "\n" and paren == bracket == brace == 0:
            statement = body[start:pos].strip()
            if statement:
                yield statement
            start = pos + 1
        pos += 1
    statement = body[start:].strip()
    if statement:
        yield statement


def _compact(value: str) -> str:
    return " ".join(value.strip().split())


def _fallback(rest: str) -> str | None:
    rest = rest.strip()
    if not rest.startswith("="):
        return None
    value = rest[1:].lstrip()
    if not value:
        return None
    if value[0] in "([{":
        closing = {"(": ")", "[": "]", "{": "}"}[value[0]]
        end = _matching(value, 0, value[0], closing)
        return value[:end + 1]
    if value[0] in "\"'":
        return value[:_skip_string(value, 0)]
    if value[0] == "@":
        end = value.find("@", 1)
        return value if end < 0 else value[:end + 1]
    metadata = re.search(r"\s+\(", value)
    return value if not metadata else value[:metadata.start()]


def _properties(body: str) -> list[dict[str, object]]:
    properties: list[dict[str, object]] = []
    for statement in _statements(body):
        match = PROPERTY_RE.match(statement)
        if not match:
            continue
        variability, type_name, name, rest = match.groups()
        if type_name in {"prepend", "append", "delete", "reorder"}:
            continue
        prop: dict[str, object] = {
            "name": name,
            "type": type_name,
            "variability": variability or "varying",
        }
        if type_name == "rel":
            prop["kind"] = "relationship"
        else:
            prop["kind"] = "attribute"
        fallback = _fallback(rest)
        if fallback is not None and fallback.strip():
            prop["fallback"] = _compact(fallback)
        allowed = re.search(r"allowedTokens\s*=\s*\[(.*?)\]", statement,
                            re.DOTALL)
        if allowed:
            prop["allowedTokens"] = re.findall(r'"([^"]*)"', allowed.group(1))
        properties.append(prop)
    return sorted(properties, key=lambda item: str(item["name"]))


def _classes(path: pathlib.Path, root: pathlib.Path) -> list[dict[str, object]]:
    text = path.read_text(encoding="utf-8")
    domain = path.parent.name
    result: list[dict[str, object]] = []
    for match in CLASS_RE.finditer(text):
        metadata_open = text.find("(", match.start(), match.end())
        metadata_close = _matching(text, metadata_open, "(", ")")
        body_open = text.find("{", metadata_close)
        if body_open < 0:
            raise ValueError(f"missing class body after byte {match.start()} in {path}")
        body_close = _matching(text, body_open, "{", "}")
        metadata = text[metadata_open + 1:metadata_close]
        body = text[body_open + 1:body_close]
        abstract = match.group("abstract")
        schema_name = (abstract or match.group("typed")).strip('"')
        parent = re.search(r"inherits\s*=\s*</([^>]+)>", metadata)
        api_kind = re.search(r"apiSchemaType\s*=\s*\"([^\"]+)\"", metadata)
        namespace = re.search(
            r"propertyNamespacePrefix\s*=\s*\"([^\"]+)\"", metadata)
        entry: dict[str, object] = {
            "domain": domain,
            "name": schema_name,
            "kind": "abstract" if abstract else "concrete",
            "properties": _properties(body),
            "source": str(path.relative_to(root)),
        }
        if parent:
            entry["inherits"] = parent.group(1)
        if api_kind:
            entry["kind"] = api_kind.group(1)
        if namespace:
            entry["propertyNamespacePrefix"] = namespace.group(1)
        result.append(entry)
    return result


def _git_commit(root: pathlib.Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--openusd-root", required=True, type=pathlib.Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", default="")
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    root = args.openusd_root.resolve()
    schema_files = sorted((root / "pxr" / "usd").glob("*/schema.usda"))
    if not schema_files:
        parser.error(f"no pxr/usd/*/schema.usda files below {root}")

    schemas: list[dict[str, object]] = []
    for path in schema_files:
        schemas.extend(_classes(path, root))
    schemas.sort(key=lambda item: (str(item["domain"]), str(item["name"])))
    manifest = {
        "formatVersion": 1,
        "openusd": {
            "version": args.version,
            "commit": args.commit or _git_commit(root),
        },
        "schemaCount": len(schemas),
        "schemas": schemas,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
