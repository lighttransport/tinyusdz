"""Stage.visit_prims callback API and stage mutation after load."""
import tinyusdz


def test_visit_prims_visits_every_prim():
    s = tinyusdz.Stage()
    a = tinyusdz.Prim("Xform", name="A")
    b = tinyusdz.Prim("Sphere", name="B")
    c = tinyusdz.Prim("Cube", name="C")
    a.add_child(b)
    a.add_child(c)
    s.add_root_prim(a)

    visited = []

    def cb(prim, path, depth):
        visited.append((prim.name, path, depth))
        return True  # continue traversal

    s.visit_prims(cb)
    names = [v[0] for v in visited]
    assert "A" in names
    assert "B" in names
    assert "C" in names


def test_visit_prims_continue_visits_all():
    """Returning True from the callback continues; verify all 5
    siblings are visited."""
    s = tinyusdz.Stage()
    for i in range(5):
        s.add_root_prim(tinyusdz.Prim("Xform", name=f"R{i}"))

    visited = []

    def cb(prim, path, depth):
        visited.append(prim.name)
        return True

    s.visit_prims(cb)
    assert len(visited) == 5
    assert set(visited) == {f"R{i}" for i in range(5)}


def test_visit_prims_path_is_correct():
    s = tinyusdz.Stage()
    root = tinyusdz.Prim("Xform", name="root")
    child = tinyusdz.Prim("Xform", name="child")
    leaf = tinyusdz.Prim("Sphere", name="leaf")
    child.add_child(leaf)
    root.add_child(child)
    s.add_root_prim(root)

    paths = {}

    def cb(prim, path, depth):
        paths[prim.name] = path
        return True

    s.visit_prims(cb)
    assert paths["root"] == "/root"
    assert paths["child"] == "/root/child"
    assert paths["leaf"] == "/root/child/leaf"


def test_visit_prims_depth_increases():
    s = tinyusdz.Stage()
    root = tinyusdz.Prim("Xform", name="root")
    child = tinyusdz.Prim("Xform", name="child")
    leaf = tinyusdz.Prim("Sphere", name="leaf")
    child.add_child(leaf)
    root.add_child(child)
    s.add_root_prim(root)

    depths = {}

    def cb(prim, path, depth):
        depths[prim.name] = depth
        return True

    s.visit_prims(cb)
    assert depths["root"] < depths["child"] < depths["leaf"]


def test_traverse_helper_yields_all_prims():
    s = tinyusdz.Stage()
    root = tinyusdz.Prim("Xform", name="root")
    child = tinyusdz.Prim("Xform", name="child")
    root.add_child(child)
    s.add_root_prim(root)

    names = [p.name for p in tinyusdz.traverse(s)]
    assert names == ["root", "child"]


def test_mutate_attribute_after_load(tmp_path):
    """Author -> save -> load -> mutate the attribute -> save -> load
    -> verify the mutation took effect."""
    s1 = tinyusdz.Stage()
    p = tinyusdz.Prim("Xform", name="X")
    p.set_attribute("count", 1)
    s1.add_root_prim(p)
    out1 = tmp_path / "step1.usdc"
    s1.save(str(out1))

    s2 = tinyusdz.load(str(out1))
    p2 = s2.get_prim_at_path("/X")
    p2.set_attribute("count", 99)

    out2 = tmp_path / "step2.usdc"
    s2.save(str(out2))

    s3 = tinyusdz.load(str(out2))
    txt = s3.export_to_string()
    assert "count = 99" in txt
    assert "count = 1" not in txt


def test_add_prim_to_loaded_stage(tmp_path):
    """Load a stage, add a new prim, save, verify the added prim
    survives."""
    src = tmp_path / "src.usda"
    src.write_text('''#usda 1.0
def Xform "Existing" {}
''')
    s = tinyusdz.load(str(src))
    new_prim = tinyusdz.Prim("Sphere", name="New")
    new_prim.set_attribute("radius", 2.0, dtype="double")
    s.add_root_prim(new_prim)

    out = tmp_path / "merged.usdc"
    s.save(str(out))
    s2 = tinyusdz.load(str(out))
    txt = s2.export_to_string()
    assert "Existing" in txt
    assert 'def Sphere "New"' in txt
    assert "radius = 2" in txt


def test_root_prims_count_after_mutation(tmp_path):
    s = tinyusdz.Stage()
    s.add_root_prim(tinyusdz.Prim("Xform", name="A"))
    s.add_root_prim(tinyusdz.Prim("Xform", name="B"))
    assert len(s.root_prims()) == 2
    s.add_root_prim(tinyusdz.Prim("Xform", name="C"))
    assert len(s.root_prims()) == 3
