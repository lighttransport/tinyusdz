#!/usr/bin/env python3
"""Offline integrity gate for the pinned OpenUSD schema manifest."""

from __future__ import annotations

import argparse
import json
import pathlib


PINNED_VERSION = "26.08"
PINNED_COMMIT = "ee47c679abde5b467a7b6a41f3b2285564a4222e"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "manifest", nargs="?",
        default="doc/generated/openusd-schema-26.08.json",
        type=pathlib.Path)
    args = parser.parse_args()
    payload = json.loads(args.manifest.read_text(encoding="utf-8"))

    assert payload["formatVersion"] == 1
    assert payload["openusd"] == {
        "version": PINNED_VERSION,
        "commit": PINNED_COMMIT,
    }
    schemas = payload["schemas"]
    assert payload["schemaCount"] == len(schemas)
    by_name: dict[tuple[str, str], dict[str, object]] = {}
    for schema in schemas:
        key = (schema["domain"], schema["name"])
        assert key not in by_name, f"duplicate schema {key}"
        by_name[key] = schema
        names = [prop["name"] for prop in schema["properties"]]
        assert len(names) == len(set(names)), f"duplicate property in {key}"

    required = {
        ("usdGeom", "BackPlateAPI"): {
            "image", "alpha:image", "depth:image", "plateVisibility"},
        ("usdGeom", "GeomModelAPI"): {"model:cardVisibility"},
        ("usdHydra", "HydraRenderPassAPI"): {"hydra:rendererName"},
        ("usdRender", "RenderPass"): {"renderSource", "inputPasses"},
        ("usdVol", "ParticleField3DGaussianSplat"): {
            "projectionModeHint", "sortingModeHint"},
        ("usdSemantics", "SemanticsLabelsAPI"): {"__INSTANCE_NAME__"},
    }
    for key, expected_properties in required.items():
        schema = by_name[key]
        actual = {prop["name"] for prop in schema["properties"]}
        missing = expected_properties - actual
        assert not missing, f"{key} missing {sorted(missing)}"

    assert by_name[("usdGeom", "BackPlateAPI")][
        "propertyNamespacePrefix"] == "backPlate"
    assert by_name[("usdSemantics", "SemanticsLabelsAPI")][
        "propertyNamespacePrefix"] == "semantics:labels"
    print(f"OpenUSD {PINNED_VERSION} schema manifest: {len(schemas)} schemas OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
