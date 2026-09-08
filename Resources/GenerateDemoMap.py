"""Authoring helper: generates the shipped demo map.

Not shipped with the plugin (see Config/FilterPlugin.ini) -- it writes into the
plugin's own content folder, so it is part of the build, not the product.

The map is deliberately almost empty. AIOCShowcaseLauncher has bAutoStart and
runs the showcase on BeginPlay, and the showcase builds all eight cave sections
plus its own Movable lighting at runtime. So the map holds one launcher and a
PlayerStart and nothing else:

  * nothing to rebuild -- no baked lighting, so no "Lighting needs to be rebuilt"
  * nothing to rot -- no content references beyond a native class
  * regenerable -- re-run this instead of hand-maintaining a binary asset
  * guarded -- Packaging.ContentEngineFloor already scans *.umap, so the map is
    held to the 5.5 floor like every other shipped package

Run on the 5.5 floor, after the content rebuild and mesh generation:
  UnrealEditor-Cmd.exe <project> -run=pythonscript -script=".../GenerateDemoMap.py"
"""
import json
import os

import unreal

MAP_PACKAGE = "/InstantOrganicCaves/Maps/IOC_DemoMap"
REPORT = os.environ.get("IOC_DEMOMAP_REPORT", "C:/IOCBuild/verify/demomap_report.json")

report = {"map": MAP_PACKAGE, "problems": []}


def problem(msg):
    report["problems"].append(str(msg)[:300])
    unreal.log_warning("[IOC-DEMOMAP] " + str(msg))


def main():
    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if level_subsystem is None or actor_subsystem is None:
        problem("editor subsystems unavailable")
        return

    # The registry does not necessarily know about a Maps folder this script created
    # on a previous run, and does_asset_exist then reports False for a map that is
    # very much there -- so new_level is attempted and fails on the collision.
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        ["/InstantOrganicCaves"], force_rescan=True)

    exists = unreal.EditorAssetLibrary.does_asset_exist(MAP_PACKAGE)
    ok = level_subsystem.load_level(MAP_PACKAGE) if exists \
        else level_subsystem.new_level(MAP_PACKAGE)
    if not ok:
        # Whichever branch was guessed, try the other before giving up.
        ok = level_subsystem.new_level(MAP_PACKAGE) if exists \
            else level_subsystem.load_level(MAP_PACKAGE)
    if not ok:
        problem("could not open or create {}".format(MAP_PACKAGE))
        return

    launcher_class = unreal.load_object(
        None, "/Script/InstantOrganicCaves.IOCShowcaseLauncher")
    if launcher_class is None:
        problem("IOCShowcaseLauncher class unavailable -- compile the plugin first")
        return

    # Idempotent: re-running must not stack duplicate launchers into the map.
    existing = [a for a in actor_subsystem.get_all_level_actors()
                if a.get_class().get_name() == "IOCShowcaseLauncher"]
    for extra in existing[1:]:
        actor_subsystem.destroy_actor(extra)

    launcher = existing[0] if existing else actor_subsystem.spawn_actor_from_class(
        launcher_class, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    if launcher is None:
        problem("failed to spawn IOCShowcaseLauncher")
        return
    launcher.set_actor_label("IOC_ShowcaseLauncher")

    # Explicit rather than relying on class defaults, so the shipped map keeps
    # behaving the same if those defaults are ever retuned.
    wanted = {"auto_start": True, "capture_mode": True,
              "show_captions": True, "loop_showcase": True}
    for prop, value in wanted.items():
        try:
            launcher.set_editor_property(prop, value)
        except Exception as exc:  # noqa: BLE001
            problem("could not set {}: {}".format(prop, str(exc)[:100]))
        else:
            got = launcher.get_editor_property(prop)
            if got != value:
                problem("{} did not take (got {})".format(prop, got))

    starts = [a for a in actor_subsystem.get_all_level_actors()
              if a.get_class().get_name() == "PlayerStart"]
    for extra in starts[1:]:
        actor_subsystem.destroy_actor(extra)
    if not starts:
        start = actor_subsystem.spawn_actor_from_class(
            unreal.PlayerStart, unreal.Vector(0, 0, 200), unreal.Rotator(0, 0, 0))
        if start is None:
            problem("failed to spawn PlayerStart")
        else:
            start.set_actor_label("IOC_PlayerStart")

    if not level_subsystem.save_current_level():
        problem("saving the level failed")

    # Read the map back: a save that silently did not happen looks identical to
    # one that did until a customer opens the package.
    actors = actor_subsystem.get_all_level_actors()
    by_class = {}
    for a in actors:
        by_class[a.get_class().get_name()] = by_class.get(a.get_class().get_name(), 0) + 1
    report["actor_classes"] = by_class
    report["actor_count"] = len(actors)
    if by_class.get("IOCShowcaseLauncher", 0) != 1:
        problem("expected exactly 1 IOCShowcaseLauncher, found {}".format(
            by_class.get("IOCShowcaseLauncher", 0)))

    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(REPORT, "w") as fh:
        json.dump(report, fh, indent=1, default=str)
    unreal.log("[IOC-DEMOMAP] {} actors, {} problems -> {}".format(
        len(actors), len(report["problems"]), REPORT))


main()
