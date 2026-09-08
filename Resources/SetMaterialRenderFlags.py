# Copyright (c) 2026 GregOrigin. All Rights Reserved.
#
# Authoring helper: sets the two render flags the shipped base materials need, and saves
# them. Excluded from the packaged plugin by Config/FilterPlugin.ini.
#
#   UnrealEditor-Cmd.exe <project> -run=pythonscript -script=".../SetMaterialRenderFlags.py"
#
# Run it on the UE 5.5 floor, like all content authoring, and re-run it after anything that
# regenerates or replaces a base material. Content.MaterialRenderFlags is the automation
# guard that catches it if nobody does.
#
# Why these two flags:
#
#   two_sided
#       A cave is a *shell* -- TunnelRadius of air inside WallThickness of rock -- so the
#       player stands inside it looking at the bore's inward-facing surface. With a
#       one-sided material those are backfaces and get culled: the tunnel wall renders as
#       transparent, and you see straight through it and out of the level. UE flips the
#       vertex normal on backfaces for two-sided materials, so this fixes the shading as
#       well as the visibility -- it is the correct setting for interior geometry, not a
#       workaround for bad winding.
#
#   used_with_instanced_static_meshes
#       Scatter props are placed as UHierarchicalInstancedStaticMeshComponents. A material
#       without this usage flag cannot be applied to one, and the engine silently
#       substitutes WorldGridMaterial -- every prop renders untextured grey *in game*.
#       AIOCProceduralActor calls CheckMaterialUsage() at runtime, which papers over this
#       in the editor (it sets the flag and dirties the package) but cannot in a cooked or
#       -game context, which is why this survived every editor-only check.
#
# Usage flags live on the base UMaterial, never on a UMaterialInstanceConstant -- the
# engine warning names the *instance* that tripped the check, which is misleading.

import json
import os

import unreal

OUT = os.environ.get("IOC_MATFLAGS_REPORT", "C:/IOCBuild/verify/matflags_report.json")

# Every base material the plugin ships. Instances inherit both flags from these.
BASE_MATERIALS = [
    "/InstantOrganicCaves/Materials/M_IOC_MathRock",    # parent of MI_IOC_CaveWalls
    "/InstantOrganicCaves/M_IOC_ProceduralRock",        # parent of MI_IOC_Crystal
    "/InstantOrganicCaves/M_IOC_SmartCave",             # parent of MI_IOC_SmartCave_Inst
]

REQUIRED = {
    "two_sided": True,
    "used_with_instanced_static_meshes": True,
}

report = {"materials": {}, "problems": []}


def problem(msg):
    report["problems"].append(str(msg)[:300])
    unreal.log_warning("[IOC-MATFLAGS] " + str(msg))


def main():
    # does_asset_exist reports False for assets the registry has not scanned, even though
    # they are plainly on disk.
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        ["/InstantOrganicCaves"], force_rescan=True)

    for path in BASE_MATERIALS:
        entry = {"before": {}, "after": {}, "saved": False}
        report["materials"][path] = entry

        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            problem("missing base material: " + path)
            continue

        mat = unreal.EditorAssetLibrary.load_asset(path)
        if mat is None:
            problem("could not load: " + path)
            continue

        changed = False
        for prop, want in REQUIRED.items():
            try:
                have = mat.get_editor_property(prop)
            except Exception as exc:
                problem("{}: cannot read '{}': {}".format(path, prop, exc))
                continue
            entry["before"][prop] = bool(have)
            if bool(have) != want:
                try:
                    mat.set_editor_property(prop, want)
                    changed = True
                except Exception as exc:
                    problem("{}: cannot set '{}': {}".format(path, prop, exc))

        if changed:
            # The shader permutations depend on both flags, so the material has to be
            # rebuilt -- saving alone leaves the old permutations on disk.
            try:
                unreal.MaterialEditingLibrary.recompile_material(mat)
            except Exception as exc:
                problem("{}: recompile failed: {}".format(path, exc))

        # Round-trip verify: read the property back rather than trusting the setter, which
        # no-ops silently on a name mismatch.
        for prop, want in REQUIRED.items():
            try:
                entry["after"][prop] = bool(mat.get_editor_property(prop))
            except Exception:
                entry["after"][prop] = None
            if entry["after"].get(prop) != want:
                problem("{}: '{}' is {} after the write, expected {}".format(
                    path, prop, entry["after"].get(prop), want))

        entry["saved"] = bool(unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))
        if not entry["saved"]:
            problem("could not save: " + path)

    out_dir = os.path.dirname(OUT)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(OUT, "w") as f:
        json.dump(report, f, indent=2, sort_keys=True)

    unreal.log("[IOC-MATFLAGS] {} material(s), {} problem(s) -> {}".format(
        len(report["materials"]), len(report["problems"]), OUT))


main()
