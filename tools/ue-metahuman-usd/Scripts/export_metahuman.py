"""Headless UE 5.8 MetaHuman export orchestrator.

This script intentionally uses public reflected editor APIs.  It creates a
transient character from an installed preset, exports generated geometry and
materials when those APIs are available, and always writes a diagnostic
inventory.  Groom conversion is completed by postprocess_usd.py from the data
made available by the editor export.
"""

import json
import os
import sys
import traceback

import unreal


LOG_PREFIX = "TinyUSDZMetaHuman:"


def log(message):
    unreal.log(f"{LOG_PREFIX} {message}")


def fail(message):
    unreal.log_error(f"{LOG_PREFIX} {message}")
    raise RuntimeError(message)


def command_line_value(name, default=""):
    command_line = unreal.SystemLibrary.get_command_line()
    marker = f"-{name}="
    for token in command_line.split():
        if token.startswith(marker):
            return token[len(marker):].strip('"')
    return default


def asset_paths(class_name, roots):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    result = []
    for root in roots:
        for data in registry.get_assets_by_path(root, recursive=True):
            if str(data.asset_class_path.asset_name) == class_name:
                result.append(str(data.package_name))
    return sorted(set(result))


def choose_preset():
    preferred = "/MetaHumanCharacter/Optional/Presets/Ada.Ada"
    if unreal.EditorAssetLibrary.does_asset_exist(preferred):
        return preferred
    presets = asset_paths("MetaHumanCharacter", ["/MetaHumanCharacter/Optional/Presets"])
    if not presets:
        fail("No installed MetaHuman Character preset was found")
    return f"{presets[0]}.{presets[0].rsplit('/', 1)[-1]}"


def create_character_from_preset():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    path = "/Game/Generated"
    name = "TinyUSDZ_DefaultHuman"
    existing = f"{path}/{name}.{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(existing):
        unreal.EditorAssetLibrary.delete_asset(existing)
    preset_path = choose_preset()
    preset = unreal.load_asset(preset_path)
    if not preset:
        fail(f"Could not load preset {preset_path}")
    # InitializeFromPreset is intentionally not reflected to Python in 5.8.
    # Duplicating the installed preset preserves its complete offline identity
    # state and is equivalent for this deterministic neutral export.
    character = tools.duplicate_asset(name, path, preset)
    if not character:
        fail(f"Could not duplicate preset {preset_path}")
    subsystem = unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)
    if not subsystem.try_add_object_to_edit(character):
        fail("Could not register MetaHuman character for editing")
    subsystem.assemble_for_preview(character)
    log(f"created {character.get_path_name()} from {preset_path}")
    return subsystem, character, preset_path


def export_geometry(character):
    library = unreal.MetaHumanCharacterExportBlueprintLibrary
    geometry = unreal.MetaHumanGeometryExportParams()
    geometry.project_path = "/Game/Generated/Exported"
    geometry.head_skeletal_mesh = True
    geometry.body_skeletal_mesh = True
    geometry.full_body_skeletal_mesh = False
    library.export_geometry(character, geometry)

    # ExportMaterials requires the optional high-resolution source textures.
    # Installed offline presets do not contain those sources, so portable
    # MaterialX materials are authored in the deterministic postprocess step.
    unreal.EditorAssetLibrary.save_directory("/Game/Generated", only_if_is_dirty=False, recursive=True)


def export_usd_assets(output_dir):
    exported = []
    assets = asset_paths("SkeletalMesh", ["/Game/Generated/Exported"])
    for package in assets:
        asset_name = package.rsplit("/", 1)[-1]
        obj = unreal.load_asset(f"{package}.{asset_name}")
        if not obj:
            continue
        options = unreal.SkeletalMeshExporterUSDOptions()
        mesh_options = options.get_editor_property("mesh_asset_options")
        # UE's USD material baker requires a live RHI and crashes under
        # -NullRHI.  Preserve geometry, skinning and material assignments here;
        # postprocess_usd.py supplies renderer-independent MaterialX shading.
        mesh_options.set_editor_property("bake_materials", False)
        mesh_options.set_editor_property("lowest_mesh_lod", 0)
        mesh_options.set_editor_property("highest_mesh_lod", 0)
        task = unreal.AssetExportTask()
        task.object = obj
        task.filename = os.path.join(output_dir, f"{asset_name}.usdc")
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        task.options = options
        if not unreal.Exporter.run_asset_export_task(task):
            fail(f"USD export failed for {package}")
        exported.append(task.filename)
    if not exported:
        fail("MetaHuman geometry export produced no skeletal meshes")
    return exported


def main():
    output_dir = os.path.abspath(command_line_value("TinyUSDZOutput", os.path.join(os.getcwd(), "output")))
    os.makedirs(output_dir, exist_ok=True)
    inventory = {
        "engine": unreal.SystemLibrary.get_engine_version(),
        "output": output_dir,
        "status": "failed",
    }
    try:
        subsystem, character, preset_path = create_character_from_preset()
        inventory["preset"] = preset_path
        export_geometry(character)
        inventory["usd"] = export_usd_assets(output_dir)
        inventory["groom_assets"] = asset_paths("GroomAsset", ["/Game", "/MetaHumanCharacter"])
        inventory["status"] = "exported"
        subsystem.remove_object_to_edit(character)
    except Exception as exc:
        inventory["error"] = str(exc)
        inventory["traceback"] = traceback.format_exc()
        unreal.log_error(inventory["traceback"])
    finally:
        with open(os.path.join(output_dir, "inventory.json"), "w", encoding="utf-8") as stream:
            json.dump(inventory, stream, indent=2)
    if inventory["status"] != "exported":
        sys.exit(1)
    log("export completed")


main()
