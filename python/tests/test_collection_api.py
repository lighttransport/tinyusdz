"""CollectionAPI applied schema with instance names round-trip."""
import tinyusdz


def _rt(tmp_path, usda):
    src = tmp_path / "x.usda"
    src.write_text(usda)
    s = tinyusdz.load(str(src))
    out = tmp_path / "x.usdc"
    s.save(str(out))
    return tinyusdz.load(str(out)).export_to_string()


def test_collection_api_applied_with_instance_name_usdc(tmp_path):
    """USDC round-trip preserves apiSchemas + collection:* rel after
    the writer-side fix that re-emits Collection storage as relations."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "World" (
    apiSchemas = ["CollectionAPI:plants"]
)
{
    rel collection:plants:includes = [</World/Tree>, </World/Bush>]
    def Xform "Tree" {}
    def Xform "Bush" {}
}
''')
    assert "CollectionAPI:plants" in txt
    assert "collection:plants:includes" in txt
    assert "</World/Tree>" in txt


def test_multiple_collections_on_one_prim(tmp_path):
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "World" (
    apiSchemas = ["CollectionAPI:lights", "CollectionAPI:cameras"]
)
{
    rel collection:lights:includes = [</World/Key>]
    rel collection:cameras:includes = [</World/MainCam>]
    def SphereLight "Key" {}
    def Camera "MainCam" {}
}
''')
    assert "CollectionAPI:lights" in txt
    assert "CollectionAPI:cameras" in txt


def test_python_apply_collection_api_with_instance_name():
    p = tinyusdz.Prim("Xform", name="World")
    p.apply_api_schema("CollectionAPI", instance_name="plants")
    schemas = p.api_schemas()
    assert "CollectionAPI:plants" in schemas


def test_python_apply_multiple_collection_instances(tmp_path):
    s = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="World")
    p.apply_api_schema("CollectionAPI", instance_name="lights")
    p.apply_api_schema("CollectionAPI", instance_name="cameras")
    s.add_root_prim(p)

    out = tmp_path / "x.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    p2 = s2.get_prim_at_path("/World")
    schemas = p2.api_schemas()
    assert "CollectionAPI:lights" in schemas
    assert "CollectionAPI:cameras" in schemas


def test_collection_includeRoot_usdc(tmp_path):
    """USDC round-trip preserves `includeRoot` after the writer-side
    fix that re-emits Collection typed attrs from
    `Collection::instances()` storage."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "World" (
    apiSchemas = ["CollectionAPI:everything"]
)
{
    uniform bool collection:everything:includeRoot = 1
    rel collection:everything:includes = [</World>]
}
''')
    assert "CollectionAPI:everything" in txt
    assert "includeRoot" in txt


def test_collection_expansionRule_usdc(tmp_path):
    """USDC round-trip preserves expansionRule typed attr."""
    txt = _rt(tmp_path, '''#usda 1.0
def Xform "World" (
    apiSchemas = ["CollectionAPI:plants"]
)
{
    uniform token collection:plants:expansionRule = "explicitOnly"
    rel collection:plants:includes = [</World/Tree>]
    def Xform "Tree" {}
}
''')
    assert "expansionRule" in txt
    assert '"explicitOnly"' in txt
