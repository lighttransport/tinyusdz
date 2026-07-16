#!/usr/bin/env python3
"""Validate the generated next AOUSD Core schema/field coverage tables."""

import argparse
import pathlib
import re
import sys


SCHEMAS = {
    "ColorSpaceDefinitionAPI": {
        "name", "redChroma", "greenChroma", "blueChroma", "whitePoint",
        "gamma", "linearBias",
    },
    "ColorSpaceAPI": {"colorSpace:name"},
    "CollectionAPI": {
        "collection:__INSTANCE__:expansionRule",
        "collection:__INSTANCE__:includeRoot",
        "collection:__INSTANCE__:includes",
        "collection:__INSTANCE__:excludes",
        "collection:__INSTANCE__",
    },
}

MINIMUM_FIELDS = {
    ("Layer", "expressionVariables"), ("Prim", "displayGroupOrder"),
    ("Property", "comment"), ("Attribute", "allowedTokens"),
    ("Relationship", "targetPaths"),
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--check", action="store_true",
                        help="fail when generated coverage is incomplete")
    args = parser.parse_args()
    generated = args.root / "src/next/schema/generated"
    schema_text = (generated / "aousd-core-schema-definitions.inc").read_text()
    actual = {}
    for schema, name in re.findall(
            r'AOUSD_SCHEMA_(?:FALLBACK|DECLARE)\("([^"]+)",\s*"([^"]+)"',
            schema_text, re.S):
        actual.setdefault(schema, set()).add(name)
    missing = []
    for schema, names in SCHEMAS.items():
        for name in sorted(names - actual.get(schema, set())):
            missing.append(f"schema {schema}.{name}")

    field_text = (generated / "aousd-elective-field-coverage.inc").read_text()
    fields = set(re.findall(r"AOUSD_FIELD\((\w+),\s*([\w]+),\s*\w+\)",
                            field_text))
    for scope, name in sorted(MINIMUM_FIELDS - fields):
        missing.append(f"field {scope}.{name}")
    if len(fields) < 70:
        missing.append(f"field table has only {len(fields)} entries (expected >=70)")
    if missing:
        print("AOUSD generated coverage is incomplete:", file=sys.stderr)
        for item in missing:
            print(f"  {item}", file=sys.stderr)
        return 1
    print(f"AOUSD generated tables: {sum(map(len, actual.values()))} schema "
          f"properties, {len(fields)} elective fields")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
