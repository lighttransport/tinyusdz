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

    with resolver.cache_scope():
        assert resolver.resolve_new("/tmp/cache.usda")[0] == "/tmp/cache.usda"


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
