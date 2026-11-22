"""Creates, rigs and assembles a MetaHuman from the command line (run by Tools/metahuman.ps1).

Steps, each skipped when its result already exists:
  create    make the MetaHumanCharacter asset (no network)
  rig       cloud auto-rig (needs an Epic sign-in; the first run opens a browser)
  textures  download the source textures from the cloud
  build     assemble with the UE Optimized pipeline into /Game/MetaHumans/<name>
  inspect   list what the build produced

Inputs come from environment variables set by the wrapper script:
  SANDBOX_RESULT      path of the result JSON (always written, also on failure)
  SANDBOX_MH_NAME     character name
  SANDBOX_MH_STEPS    comma-separated steps
  SANDBOX_MH_QUALITY  assembly quality level name (see result["quality_levels"])
"""

import json
import os
import time
import traceback

import unreal

NAME = os.environ.get("SANDBOX_MH_NAME", "SandboxNPC")
STEPS = [s.strip() for s in os.environ.get("SANDBOX_MH_STEPS", "create,rig,textures,build,inspect").split(",") if s.strip()]
QUALITY = os.environ.get("SANDBOX_MH_QUALITY", "MEDIUM")
RESULT_PATH = os.environ.get("SANDBOX_RESULT", "")

CHARACTER_DIR = "/Game/Characters/MetaHumans"
CHARACTER_PATH = f"{CHARACTER_DIR}/{NAME}"
BUILD_ROOT = "/Game/MetaHumans"
COMMON_PATH = f"{BUILD_ROOT}/Common"

result = {
    "ok": False,
    "name": NAME,
    "character_asset": CHARACTER_PATH,
    "build_path": f"{BUILD_ROOT}/{NAME}",
    "steps": [],
}


def enum_names(enum_type):
    return sorted(n for n in dir(enum_type) if n.isupper())


def subsystem():
    return unreal.get_editor_subsystem(unreal.MetaHumanCharacterEditorSubsystem)


def load_character():
    character = unreal.load_asset(f"{CHARACTER_PATH}.{NAME}")
    if character is None:
        raise RuntimeError(f"Character asset {CHARACTER_PATH} does not exist; run the 'create' step first")
    return character


def has_textures(character):
    return bool(character.get_editor_property("has_high_resolution_textures"))


class Editing:
    """Registers the character with the MetaHuman subsystem for the duration of a step."""

    def __init__(self, character):
        self.character = character

    def __enter__(self):
        if not subsystem().try_add_object_to_edit(self.character):
            raise RuntimeError("Unable to open the character for editing (is it open in the editor?)")
        return self.character

    def __exit__(self, *exc):
        if subsystem().is_object_added_for_editing(self.character):
            subsystem().remove_object_to_edit(self.character)
        return False


def save(character):
    unreal.EditorAssetLibrary.save_loaded_asset(character, only_if_is_dirty=False)


def step_create():
    if unreal.EditorAssetLibrary.does_asset_exist(CHARACTER_PATH):
        return "already exists"
    character = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name=NAME,
        package_path=CHARACTER_DIR,
        asset_class=unreal.MetaHumanCharacter,
        factory=unreal.new_object(type=unreal.MetaHumanCharacterFactoryNew),
    )
    if character is None:
        raise RuntimeError("create_asset returned nothing")
    save(character)
    return "created"


def step_rig():
    # The rig state itself is not exposed to scripting. can_build_meta_human() is true only for a
    # character that is both rigged and textured, so it serves as the "nothing left to do" test here
    # and as the combined verification in the build step.
    character = load_character()
    with Editing(character):
        if subsystem().can_build_meta_human(character, False):
            return "already rigged"
        params = unreal.MetaHumanCharacterAutoRiggingRequestParams()
        params.blocking = True
        params.report_progress = False
        params.rig_type = unreal.MetaHumanRigType.JOINTS_ONLY
        subsystem().request_auto_rigging(character, params)
    save(character)
    return "auto-rig requested (verified in the build step)"


def step_textures():
    character = load_character()
    if has_textures(character):
        return "already downloaded"
    with Editing(character):
        params = unreal.MetaHumanCharacterTextureRequestParams()
        params.blocking = True
        params.report_progress = False
        subsystem().request_texture_sources(character, params)
        downloaded = has_textures(character)
    if not downloaded:
        raise RuntimeError("Texture download did not complete. Most likely the Epic sign-in was not finished or the auto-rig failed (see the log).")
    save(character)
    return "downloaded"


def built_assets():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([BUILD_ROOT], True)
    by_class = {}
    for asset in registry.get_assets_by_path(BUILD_ROOT, recursive=True):
        by_class.setdefault(str(asset.asset_class_path.asset_name), []).append(str(asset.package_name))
    return by_class


def step_build():
    character = load_character()
    quality = getattr(unreal.MetaHumanQualityLevel, QUALITY, None)
    if quality is None:
        raise RuntimeError(f"Unknown quality '{QUALITY}'. Available: {enum_names(unreal.MetaHumanQualityLevel)}")
    reported = None
    with Editing(character):
        if not subsystem().can_build_meta_human(character, True):
            raise RuntimeError("The character cannot be assembled: it is not rigged or its textures are missing (see the log)")
        params = unreal.MetaHumanCharacterEditorBuildParameters()
        params.pipeline_type = unreal.MetaHumanDefaultPipelineType.OPTIMIZED
        params.pipeline_quality = quality
        params.absolute_build_path = BUILD_ROOT
        params.common_folder_path = COMMON_PATH
        params.enable_wardrobe_item_validation = False
        try:
            subsystem().build_meta_human(character=character, params=params)
        except Exception as error:
            # The engine raises script errors for Control Rig graph problems inside its own common
            # assets ("Cannot break link ...") even when the log says the assembly succeeded.
            # So the outcome is judged by what was produced, and the message is kept as a warning.
            reported = str(error).strip().splitlines()[0]
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)

    meshes = [m for m in built_assets().get("SkeletalMesh", []) if f"/{NAME}/" in m]
    if not meshes:
        raise RuntimeError(f"Assembly produced no skeletal mesh under {BUILD_ROOT}/{NAME}. Engine message: {reported}")
    if reported:
        result.setdefault("warnings", []).append(f"build: {reported}")
    return f"built with OPTIMIZED / {QUALITY}, {len(meshes)} skeletal mesh(es)"


def step_inspect():
    if unreal.EditorAssetLibrary.does_asset_exist(CHARACTER_PATH):
        result["has_textures"] = has_textures(load_character())
    by_class = built_assets()
    result["built_asset_counts"] = {k: len(v) for k, v in sorted(by_class.items())}
    result["skeletal_meshes"] = sorted(by_class.get("SkeletalMesh", []))
    result["physics_assets"] = sorted(by_class.get("PhysicsAsset", []))
    result["blueprints"] = sorted(by_class.get("Blueprint", []))
    return f"{sum(len(v) for v in by_class.values())} assets under {BUILD_ROOT}"


HANDLERS = {
    "create": step_create,
    "rig": step_rig,
    "textures": step_textures,
    "build": step_build,
    "inspect": step_inspect,
}


def main():
    result["quality_levels"] = enum_names(unreal.MetaHumanQualityLevel)
    result["engine_version"] = unreal.SystemLibrary.get_engine_version()
    all_ok = True
    for name in STEPS:
        entry = {"step": name, "ok": False}
        started = time.time()
        try:
            if name not in HANDLERS:
                raise RuntimeError(f"Unknown step '{name}'. Known: {sorted(HANDLERS)}")
            entry["detail"] = HANDLERS[name]()
            entry["ok"] = True
        except Exception as error:  # report every failure in the result file
            entry["error"] = str(error)
            entry["traceback"] = traceback.format_exc()
            all_ok = False
        entry["seconds"] = round(time.time() - started, 1)
        result["steps"].append(entry)
        unreal.log(f"[metahuman_build] {name}: {'ok' if entry['ok'] else 'FAILED'} {entry.get('detail', entry.get('error', ''))}")
        if not entry["ok"]:
            break
    result["ok"] = all_ok


try:
    main()
except Exception as error:
    result["error"] = str(error)
    result["traceback"] = traceback.format_exc()
finally:
    if RESULT_PATH:
        os.makedirs(os.path.dirname(RESULT_PATH), exist_ok=True)
        with open(RESULT_PATH, "w", encoding="utf-8") as handle:
            json.dump(result, handle, indent=2)
