# SPDX-License-Identifier: Apache-2.0
"""MTL dependencies supplied entirely through the in-memory resolver."""
import base64
import math

import lightusd
import pytest

OBJ = b"mtllib materials.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl test\nf 1 2 3\n"


def material_stage(properties, **options):
    resolver = lightusd.AssetResolver()
    resolver.register_memory("materials.mtl", b"newmtl test\n" + properties)
    resolver.register_memory("123.png", base64.b64decode(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j1ZkAAAAASUVORK5CYII="))
    return lightusd.load_bytes(OBJ, format="obj", resolver=resolver, **options)


@pytest.mark.parametrize("properties", [b"Kd nan 0 0", b"Kd 1e100 0 0", b"Ks 0 .1junk 0",
    b"Ni infinity", b"Ns -2", b"Ns -3", b"Pr", b"illum 2147483648", b"illum 2junk",
    b"map_Kd -s 1e999 123.png", b"map_Kd -s 1 nan 123.png",
    b"map_Kd -mm 1 inf 123.png", b"map_Kd -texres 1junk 123.png",
    b"map_Kd -clamp maybe 123.png", b"map_Kd -s 1"])
def test_invalid_mtl_rejects_import(properties):
    with pytest.raises(lightusd.UsdError, match="invalid|parse"):
        material_stage(properties)


def test_optional_uv_components_and_numeric_texture_filename():
    stage = material_stage(b"Kd .5 .25 .125\nmap_Kd -s 2 3 -o .1 .2 -clamp on 123.png\n")
    transform = stage.prim_at("/World/Materials/test/Map_0_Transform")
    assert list(transform["inputs:scale"]) == pytest.approx([2, 3])
    assert list(transform["inputs:translation"]) == pytest.approx([.1, .2])
    texture = stage.prim_at("/World/Materials/test/Map_0")
    assert texture["inputs:file"] == "123.png"
    assert texture["inputs:wrapS"] == "clamp"
    assert not stage.warnings()


def test_large_finite_shininess_maps_to_finite_roughness():
    stage = material_stage(b"Ns 3e38\n")
    roughness = stage.prim_at("/World/Materials/test/PreviewSurface")["inputs:roughness"]
    assert math.isfinite(roughness) and 0 < roughness < 1


@pytest.mark.parametrize("in_memory", [True, False])
def test_oversized_mtl_is_fatal(tmp_path, in_memory):
    properties = b"#" + b"x" * (512 * 1024) + b"\n"
    with pytest.raises(lightusd.UsdError, match="limit|budget"):
        if in_memory:
            material_stage(properties, max_memory=256 * 1024)
        else:
            (tmp_path / "materials.mtl").write_bytes(properties)
            lightusd.load_bytes(OBJ, format="obj", source=tmp_path / "scene.obj", max_memory=256 * 1024)


def test_oversized_texture_is_fatal():
    resolver = lightusd.AssetResolver()
    resolver.register_memory("materials.mtl", b"newmtl test\nmap_Kd 123.png\n")
    resolver.register_memory("123.png", b"x" * (512 * 1024))
    with pytest.raises(lightusd.UsdError, match="limit|budget"):
        lightusd.load_bytes(OBJ, format="obj", resolver=resolver, max_memory=256 * 1024)


def test_repeated_material_library_is_loaded_once():
    resolver = lightusd.AssetResolver()
    resolver.register_memory("materials.mtl", b"newmtl test\nKd .2 .3 .4\n")
    source = OBJ.replace(b"mtllib materials.mtl\n", b"mtllib materials.mtl materials.mtl\nmtllib materials.mtl\n")
    stage = lightusd.load_bytes(source, format="obj", resolver=resolver)
    assert len(stage.prims_of_type("Material")) == 1
    assert stage.prims_of_type("GeomSubset")[0].relationship("material:binding").targets == ("/World/Materials/test",)
    assert not stage.warnings()


def test_many_material_bindings_preserve_source_ids():
    count = 513
    definitions = "".join(f"newmtl material_{i}\nKd {i / (count - 1)} .25 .5\n"
                          for i in reversed(range(count))).encode()
    source = b"mtllib materials.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n" + "".join(
        f"usemtl material_{i}\nf 1 2 3\n" for i in range(count)).encode()
    resolver = lightusd.AssetResolver()
    resolver.register_memory("materials.mtl", definitions)
    stage = lightusd.load_bytes(source, format="obj", resolver=resolver)
    assert len(stage.prims_of_type("Material")) == count
    subsets = stage.prims_of_type("GeomSubset")
    assert len(subsets) == count
    visited = set()
    for subset in subsets:
        faces = list(subset["indices"])
        assert len(faces) == 1
        face = faces[0]
        visited.add(face)
        target, = subset.relationship("material:binding").targets
        assert target == f"/World/Materials/material_{face}"
        color = stage.prim_at(target + "/PreviewSurface")["inputs:diffuseColor"]
        assert list(color) == pytest.approx([face / (count - 1), .25, .5])
    assert visited == set(range(count))


@pytest.mark.parametrize("newline", [b"\n", b"\r\n", b"\r"])
@pytest.mark.parametrize("bom", [b"", b"\xef\xbb\xbf"])
def test_duplicate_material_names_keep_their_source_metadata(tmp_path, newline, bom):
    png = base64.b64decode("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j1ZkAAAAASUVORK5CYII=")
    for directory, properties in [("first", b"Ns 100"), ("second", b"Pr .8\nPm .7")]:
        folder = tmp_path / directory
        folder.mkdir()
        (folder / "color.png").write_bytes(png)
        text = b"newmtl shared\n" + properties + b"\nmap_Kd color.png\n"
        (folder / "library.mtl").write_bytes(bom + text.replace(b"\n", newline))
    source = OBJ.replace(b"mtllib materials.mtl", b"mtllib first/library.mtl second/library.mtl").replace(b"usemtl test", b"usemtl shared")
    stage = lightusd.load_bytes(source, format="obj", source=tmp_path / "scene.obj")
    first = stage.prim_at("/World/Materials/shared/PreviewSurface")
    second = stage.prim_at("/World/Materials/shared_1/PreviewSurface")
    assert first["inputs:roughness"] == pytest.approx(math.sqrt(2 / 102))
    assert first["inputs:useSpecularWorkflow"] == 1
    assert second["inputs:roughness"] == pytest.approx(.8)
    assert second["inputs:useSpecularWorkflow"] == 0
    assert stage.prim_at("/World/Materials/shared/Map_0")["inputs:file"] == str(tmp_path / "first/color.png")
    assert stage.prim_at("/World/Materials/shared_1/Map_0")["inputs:file"] == str(tmp_path / "second/color.png")
    assert stage.prims_of_type("GeomSubset")[0].relationship("material:binding").targets == ("/World/Materials/shared",)
