#!/usr/bin/env python3
"""
Example: Working with USD Variants using TinyUSDZ Python Bindings

This example demonstrates how to:
1. Load a USD file
2. Extract variant information using VariantConverter
3. List all available variants
4. Select different variants
5. Query variant statistics
"""

import tinyusdz

def list_variants(stage):
    """Extract and list all variants in a USD stage."""
    scene = tinyusdz.tydra.RenderScene()
    converter = tinyusdz.tydra.VariantConverter()

    if not converter.convert_variants(stage, scene):
        print("Failed to convert variants")
        return None

    print(f"Found {len(scene.variant_groups)} variant groups:\n")

    for group in scene.variant_groups:
        print(f"Prim: {group.prim_path}")
        print(f"  Variant sets: {len(group.variant_sets)}")

        for variant_set in group.variant_sets:
            print(f"    - {variant_set.name}")
            print(f"      Options: {[opt.name for opt in variant_set.options]}")
            print(f"      Default: {variant_set.options[variant_set.default_option_index].name}")
        print()

    return scene


def work_with_variants(scene):
    """Demonstrate how to use the VariantManager API."""
    manager = tinyusdz.tydra.DefaultVariantManager()
    manager.set_variant_groups(scene.variant_groups)

    # Get variant statistics
    stats = manager.get_statistics()
    print("Variant Statistics:")
    print(f"  Total groups: {stats.num_variant_groups}")
    print(f"  Total sets: {stats.num_variant_sets}")
    print(f"  Total options: {stats.num_variant_options}")
    print(f"  Max nesting depth: {stats.max_nesting_depth}\n")

    # Find specific variant group
    if manager.has_variants():
        # Get first variant group
        groups = manager.get_variant_groups()
        if groups:
            first_group = groups[0]
            print(f"First variant group: {first_group.prim_path}")

            # Try to select a variant
            if first_group.variant_sets:
                var_set = first_group.variant_sets[0]
                if var_set.options:
                    option_name = var_set.options[0].name
                    print(f"  Selecting '{option_name}' from variant set '{var_set.name}'")

                    # Select the variant
                    success = manager.select_variant(0, var_set.name, option_name)
                    if success:
                        print(f"  ✓ Variant selection successful")

                        # Get current selection
                        selection = manager.get_current_selection(0)
                        if selection:
                            print(f"  Current selection: {selection.selected_option_index}")
                    else:
                        print(f"  ✗ Failed to select variant")

            # Reset to defaults
            print("\n  Resetting to defaults...")
            manager.reset_to_defaults()
            print("  ✓ Reset complete")


def main():
    """Main example demonstrating variant API usage."""
    import sys

    if len(sys.argv) < 2:
        print("Usage: python variant_example.py <usd_file>")
        print("\nExample:")
        print("  python variant_example.py model.usda")
        sys.exit(1)

    usd_file = sys.argv[1]

    # Load USD file
    print(f"Loading USD file: {usd_file}\n")
    stage = tinyusdz.load_usd_from_file(usd_file)
    if not stage:
        print(f"Error: Failed to load {usd_file}")
        sys.exit(1)

    # Extract and list variants
    print("=" * 60)
    print("VARIANT LISTING")
    print("=" * 60 + "\n")
    scene = list_variants(stage)

    if scene and scene.variant_groups:
        # Work with variants
        print("=" * 60)
        print("VARIANT MANAGEMENT")
        print("=" * 60 + "\n")
        work_with_variants(scene)
    else:
        print("No variants found in the USD file.")


if __name__ == "__main__":
    main()
