# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import lightusd


def test_resolver_context_and_new_asset(tmp_path):
    resolver = lightusd.AssetResolver()
    anchor = str(tmp_path / "look" / "layer.usda")
    resolved, exists, is_package = resolver.resolve_new("sub/new.usda", anchor)
    assert resolved == str(tmp_path / "look" / "sub" / "new.usda")
    assert not exists
    assert not is_package

    assert resolver.current_context == ""
    assert resolver.is_context_dependent("tex.usda")
    assert not resolver.is_context_dependent("/abs/tex.usda")

    with resolver.bind_context(str(tmp_path)):
        assert resolver.current_context == str(tmp_path)
        assert resolver.is_context_dependent("tex.usda")
    assert resolver.current_context == ""

    with resolver.bind_context(str(tmp_path), scheme_contexts={"StUdIo": "/ctx"}):
        assert resolver.current_context == str(tmp_path)
    try:
        resolver.bind_context(scheme_contexts={"studio": 1})
    except TypeError:
        pass
    else:
        raise AssertionError("non-string scheme context was accepted")

    with resolver.cache_scope():
        assert resolver.resolve_new("/tmp/cache.usda")[0] == "/tmp/cache.usda"


def test_resolver_memory_alias_and_report():
    resolver = lightusd.AssetResolver()
    dep = b"#usda 1.0\ndef Xform \"Base\" {}\n"
    resolver.register_memory("demo:dep.usda", dep)
    assert resolver.read("demo:dep.usda") == dep
    resolver.set_alias("dep.usda", "demo:dep.usda")
    assert resolver.memory_stats[0] == 1

    root = b"#usda 1.0\ndef Xform \"Root\" (references = @dep.usda@) {}\n"
    resolver.register_memory("demo:root.usda", root)
    report, complete = resolver.dependency_report("demo:root.usda")
    assert "demo:dep.usda" in report
    assert complete
    assert resolver.unregister_memory("demo:dep.usda") is True
    assert resolver.unregister_memory("demo:dep.usda") is False


def test_resolver_scheme_callbacks():
    resolver = lightusd.AssetResolver()
    seen = []

    def resolve(identifier, anchor, scheme_context):
        seen.append(scheme_context)
        return identifier

    resolver.register_scheme(
        "studio", resolve, lambda identifier: b"#usda 1.0\n"
    )
    with resolver.bind_context(scheme_contexts={"studio": "ctx"}):
        path, exists, is_package = resolver.resolve("studio:layer.usda")
        assert path == "studio:layer.usda"
        assert exists
        assert not is_package
        assert resolver.read("studio:layer.usda") == b"#usda 1.0\n"
    assert seen == ["ctx"]

    resolver.register_scheme("boom", lambda *args: 1 / 0)
    try:
        resolver.resolve("boom:layer.usda")
    except ZeroDivisionError:
        pass
    else:
        raise AssertionError("scheme resolver exception was not propagated")

    assert resolver.unregister_scheme("studio") is True
    assert resolver.unregister_scheme("studio") is False


def test_resolver_load_integration(tmp_path):
    assets = tmp_path / "assets"
    assets.mkdir(parents=True)
    (assets / "dep.usda").write_text(
        '#usda 1.0\n(defaultPrim = "Base")\n'
        'def Xform "Base" { def Xform "Child" {} }\n',
        encoding="utf-8",
    )
    root = tmp_path / "root.usda"
    root.write_text(
        '#usda 1.0\ndef Xform "Root" (references = @dep.usda@) {}\n',
        encoding="utf-8",
    )
    resolver = lightusd.AssetResolver()
    with resolver.bind_context(str(assets)):
        stage = lightusd.load(str(root), resolver=resolver)
    assert stage.prim_at("/Root/Child").path == "/Root/Child"


def test_resolver_package_callback():
    resolver = lightusd.AssetResolver()
    resolver.register_package(
        ".PKG", lambda package, entry: f"virtual:{package}|{entry}"
    )
    resolved, exists, is_package = resolver.resolve(
        "bundle.pkg[dir/entry.usda]"
    )
    assert resolved == "virtual:bundle.pkg|dir/entry.usda"
    assert exists
    assert is_package

    assert resolver.unregister_package("pkg") is True
    assert resolver.unregister_package("pkg") is False

    resolver.register_package("pkg", lambda package, entry: 1 / 0)
    try:
        resolver.resolve("bundle.pkg[entry.usda]")
    except ZeroDivisionError:
        pass
    else:
        raise AssertionError("package callback exception was not propagated")
