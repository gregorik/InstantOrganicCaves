# DEPRECATED: This script is maintained as a manual fallback only.
# Prefer the IOC Setup Wizard (Window > IOC Setup Wizard... > Create Starter Level),
# which handles launcher configuration natively in C++.
import unreal

SHOWCASE_MAP = "/Game/IOC_Showcase"
PLUGIN_CONTENT = "/InstantOrganicCaves"
LAUNCHER_BP_NAME = "BP_IOC_ShowcaseLauncher"


def _set_prop(obj, names, value):
    for name in names:
        try:
            obj.set_editor_property(name, value)
            return True
        except Exception:
            pass
    unreal.log_warning("IOC showcase setup: could not set property " + "/".join(names))
    return False


def create_launcher_blueprint():
    bp_path = PLUGIN_CONTENT + "/" + LAUNCHER_BP_NAME
    if unreal.EditorAssetLibrary.does_asset_exist(bp_path):
        return unreal.load_asset(bp_path)

    parent_class = unreal.load_object(None, "/Script/InstantOrganicCaves.IOCShowcaseLauncher")
    if not parent_class:
        unreal.log_error("IOC showcase setup: IOCShowcaseLauncher class is unavailable. Compile the plugin first.")
        return None

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    bp_asset = asset_tools.create_asset(LAUNCHER_BP_NAME, PLUGIN_CONTENT, unreal.Blueprint, factory)
    unreal.EditorAssetLibrary.save_asset(bp_asset.get_path_name())
    unreal.log("IOC showcase setup: created " + bp_asset.get_path_name())
    return bp_asset


def open_or_create_showcase_map():
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(SHOWCASE_MAP):
        level_subsystem.load_level(SHOWCASE_MAP)
        return True

    created = level_subsystem.new_level(SHOWCASE_MAP)
    if not created:
        unreal.log_error("IOC showcase setup: failed to create " + SHOWCASE_MAP)
        return False
    return True


def run_setup():
    unreal.log_warning("IOC_CreateShowcaseMap.py is deprecated. Use Window > IOC Setup Wizard... > Create Starter Level instead.")
    create_launcher_blueprint()
    if not open_or_create_showcase_map():
        return

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing = [a for a in actor_subsystem.get_all_level_actors() if a.get_name().startswith("IOC_ShowcaseLauncher")]
    if existing:
        launcher = existing[0]
    else:
        launcher_bp = unreal.load_asset(PLUGIN_CONTENT + "/" + LAUNCHER_BP_NAME)
        if launcher_bp:
            launcher = actor_subsystem.spawn_actor_from_object(launcher_bp, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        else:
            launcher_class = unreal.load_object(None, "/Script/InstantOrganicCaves.IOCShowcaseLauncher")
            launcher = actor_subsystem.spawn_actor_from_class(launcher_class, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        launcher.set_actor_label("IOC_ShowcaseLauncher")

    _set_prop(launcher, ["b_auto_start", "auto_start", "bAutoStart"], True)
    _set_prop(launcher, ["b_capture_mode", "capture_mode", "bCaptureMode"], True)
    _set_prop(launcher, ["b_show_captions", "show_captions", "bShowCaptions"], True)
    _set_prop(launcher, ["start_delay", "StartDelay"], 0.5)

    unreal.EditorLevelLibrary.save_current_level()
    unreal.log("IOC showcase setup: saved " + SHOWCASE_MAP + ". Press Play to start the capture-ready demo.")


if __name__ == "__main__":
    run_setup()
