"""Authoring helper: generates the plugin's original scatter mesh library.

Not shipped with the plugin (see Config/FilterPlugin.ini) -- it writes into the
plugin's own content folder, so it is part of the build, not the product.

Everything here is generated from GeometryScripting primitives plus noise, which
makes the output original by construction: no third-party art, no licence
surface, and every asset reproducible from the seed recorded in ScatterSeeds.json.

The props share the cave walls' triplanar material, so UV layout is close to
irrelevant for them -- which is what makes a generated library viable at all.

Run:
  UnrealEditor-Cmd.exe <project> -run=pythonscript -script=".../GenerateScatterMeshes.py"

Env:
  IOC_SCATTER_REPORT  where to write the run report (default Saved/IOCScatter.json)
"""
import json
import math
import os
import random
import traceback

import unreal

GEO_DIR = "/InstantOrganicCaves/InstantOrganicCaves/Geometry"
MAT_DIR = "/InstantOrganicCaves"
ROCK_MATERIAL = "/InstantOrganicCaves/MI_IOC_CaveWalls"
CRYSTAL_MATERIAL = "/InstantOrganicCaves/MI_IOC_Crystal"
CRYSTAL_PARENT = "/InstantOrganicCaves/M_IOC_ProceduralRock"

SEEDS_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "ScatterSeeds.json")
REPORT = os.environ.get("IOC_SCATTER_REPORT", "C:/IOCBuild/verify/scatter_report.json")

# unreal.Rotator's POSITIONAL args are (roll, pitch, yaw), not (pitch, yaw,
# roll) as the C++ constructor implies. Always build Rotators with named
# arguments here -- getting it wrong produces plausible-looking but wrong
# geometry with no error.
Prim = unreal.GeometryScript_Primitives
Deform = unreal.GeometryScript_MeshDeformers
Simplify = unreal.GeometryScript_MeshSimplification
Booleans = unreal.GeometryScript_MeshBooleans
Normals = unreal.GeometryScript_Normals
Xform = unreal.GeometryScript_MeshTransforms
Queries = unreal.GeometryScript_MeshQueries
Collision = unreal.GeometryScript_Collision
NewAsset = unreal.GeometryScript_NewAssetUtils

report = {"assets": [], "problems": []}


def problem(where, msg):
    report["problems"].append({"where": where, "issue": str(msg)[:300]})
    unreal.log_warning("[IOC-SCATTER] {}: {}".format(where, msg))


# --------------------------------------------------------------------------
# Catalogue. Sizes are in cm and chosen so props read at human scale inside a
# cave whose default voxel size is 100cm.
# --------------------------------------------------------------------------

CATALOGUE = [
    # Sized against the cave itself: default TunnelRadius is 300cm, so a tunnel
    # is ~6m across. The wizard scales rock layers up to 1.65x, so a prop's
    # base size must stay well under half the tunnel or scatter swallows it.
    # Noise magnitude and frequency scale with radius, otherwise small props get
    # over-displaced and large ones look smooth.
    # name,              family,     seed, params
    ("SM_IOC_Rock_A",    "rock",     1001, dict(radius=58.0,  squash=(1.25, 1.0, 0.72),
                                                tris=2200, coarse=(13.0, 0.021), fine=(4.2, 0.072))),
    ("SM_IOC_Rock_B",    "rock",     1002, dict(radius=35.0,  squash=(1.0, 1.15, 0.85),
                                                tris=1500, coarse=(8.5, 0.033), fine=(2.7, 0.107))),
    ("SM_IOC_Rock_C",    "rock",     1003, dict(radius=17.0,  squash=(1.1, 0.95, 0.8),
                                                tris=700,  coarse=(4.3, 0.060), fine=(1.5, 0.190))),
    ("SM_IOC_Crystal_A", "crystal",  2001, dict(shards=6, height=(55.0, 100.0),
                                                radius=(7.0, 12.0), tilt=14.0, tris=900)),
    ("SM_IOC_Crystal_B", "crystal",  2002, dict(shards=5, height=(22.0, 42.0),
                                                radius=(8.0, 14.0), tilt=20.0, tris=750)),
    ("SM_IOC_Geode",     "geode",    3001, dict(radius=37.0, cavity=0.62,
                                                tris=1800, coarse=(6.6, 0.037), fine=(2.4, 0.117))),
    ("SM_IOC_Stalactite", "spike",   4001, dict(base_radius=21.0, height=150.0,
                                                hanging=True, tris=800, noise=(2.6, 0.037))),
    ("SM_IOC_Stalagmite", "spike",   4002, dict(base_radius=26.0, height=110.0,
                                                hanging=False, tris=800, noise=(3.0, 0.035))),
]

# Per-family triangle budgets, asserted later by Content.ScatterMeshLibrary.
TRI_BUDGET = {"rock": (400, 3000), "crystal": (200, 1500),
              "geode": (400, 3000), "spike": (200, 1500)}


# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------

def new_mesh():
    try:
        return unreal.new_object(unreal.DynamicMesh)
    except Exception:
        return unreal.DynamicMesh()


def prim_options():
    return unreal.GeometryScriptPrimitiveOptions()


def noise(mesh, magnitude, frequency, seed, along_normal=True):
    """One octave of Perlin displacement. The options struct carries a single
    layer, so octaves are separate calls rather than a list."""
    layer = unreal.GeometryScriptPerlinNoiseLayerOptions()
    layer.set_editor_property("magnitude", magnitude)
    layer.set_editor_property("frequency", frequency)
    layer.set_editor_property("random_seed", int(seed))
    opts = unreal.GeometryScriptPerlinNoiseOptions()
    opts.set_editor_property("base_layer", layer)
    opts.set_editor_property("apply_along_normal", along_normal)
    return Deform.apply_perlin_noise_to_mesh(
        mesh, unreal.GeometryScriptMeshSelection(), opts)


def smooth(mesh, iterations, alpha):
    opts = unreal.GeometryScriptIterativeMeshSmoothingOptions()
    opts.set_editor_property("num_iterations", int(iterations))
    opts.set_editor_property("alpha", float(alpha))
    return Deform.apply_iterative_smoothing_to_mesh(
        mesh, unreal.GeometryScriptMeshSelection(), opts)


def simplify_to(mesh, target_tris):
    return Simplify.apply_simplify_to_triangle_count(
        mesh, int(target_tris), unreal.GeometryScriptSimplifyMeshOptions())


def facet(mesh, angle_deg=18.0):
    """Hard edges, so crystal faces read as flats rather than a smooth blob."""
    split = unreal.GeometryScriptSplitNormalsOptions()
    split.set_editor_property("split_by_opening_angle", True)
    split.set_editor_property("opening_angle_deg", float(angle_deg))
    calc = unreal.GeometryScriptCalculateNormalsOptions()
    return Normals.compute_split_normals(mesh, split, calc)


def set_pivot(mesh, mode):
    """Translate so the prop sits correctly when placed on a surface.

    Scatter aligns instances to the surface normal, so a floor prop must have
    its origin at its base and a hanging prop at its top; otherwise half the
    mesh intersects the rock.
    """
    box = Queries.get_mesh_bounding_box(mesh)
    lo, hi = box.min, box.max
    cx = (lo.x + hi.x) * 0.5
    cy = (lo.y + hi.y) * 0.5
    if mode == "top":
        dz = -hi.z
    else:
        dz = -lo.z
    return Xform.translate_mesh(mesh, unreal.Vector(-cx, -cy, dz))


def tri_count(mesh):
    # There is no get_triangle_count; the binding exposes get_num_triangle_i_ds.
    return Queries.get_num_triangle_i_ds(mesh)


# --------------------------------------------------------------------------
# Family builders
# --------------------------------------------------------------------------

def build_rock(rng, p):
    mesh = new_mesh()
    r = p["radius"]
    sx, sy, sz = p["squash"]
    xf = unreal.Transform(
        location=unreal.Vector(0, 0, 0),
        rotation=unreal.Rotator(pitch=rng.uniform(-20, 20),
                                yaw=rng.uniform(0, 360),
                                roll=rng.uniform(-20, 20)),
        scale=unreal.Vector(sx, sy, sz))
    Prim.append_sphere_lat_long(mesh, prim_options(), xf, r, 24, 32)
    # Two octaves: a coarse pass for silhouette, a fine one for surface break-up.
    mag, freq = p["coarse"]
    noise(mesh, mag, freq, rng.randint(1, 10 ** 6))
    mag, freq = p["fine"]
    noise(mesh, mag, freq, rng.randint(1, 10 ** 6))
    smooth(mesh, 4, 0.18)
    simplify_to(mesh, p["tris"])
    set_pivot(mesh, "base")
    return mesh


def build_crystal(rng, p):
    """A union of hexagonal prisms. Faceting is why crystals cannot come from
    the rock path -- a smoothed noise field has no flats."""
    mesh = new_mesh()
    lo_h, hi_h = p["height"]
    lo_r, hi_r = p["radius"]
    for i in range(p["shards"]):
        shard = new_mesh()
        h = rng.uniform(lo_h, hi_h)
        rad = rng.uniform(lo_r, hi_r)
        # RadialSteps=6 makes a hexagonal prism; TopRadius < BaseRadius tapers
        # it to a point like a natural crystal termination.
        Prim.append_cone(shard, prim_options(), unreal.Transform(),
                         rad, rad * 0.18, h, 6, 1, True)
        tilt = p["tilt"]
        xf = unreal.Transform(
            location=unreal.Vector(rng.uniform(-rad, rad),
                                   rng.uniform(-rad, rad), 0.0),
            rotation=unreal.Rotator(pitch=rng.uniform(-tilt, tilt),
                                    yaw=rng.uniform(0, 360),
                                    roll=rng.uniform(-tilt, tilt)),
            scale=unreal.Vector(1, 1, 1))
        if i == 0:
            Xform.transform_mesh(shard, xf)
            Booleans.apply_mesh_boolean(mesh, unreal.Transform(), shard,
                                        unreal.Transform(),
                                        unreal.GeometryScriptBooleanOperation.UNION,
                                        unreal.GeometryScriptMeshBooleanOptions())
        else:
            Booleans.apply_mesh_boolean(mesh, unreal.Transform(), shard, xf,
                                        unreal.GeometryScriptBooleanOperation.UNION,
                                        unreal.GeometryScriptMeshBooleanOptions())
    simplify_to(mesh, p["tris"])
    set_pivot(mesh, "base")
    facet(mesh)
    return mesh


def build_geode(rng, p):
    mesh = new_mesh()
    r = p["radius"]
    Prim.append_sphere_lat_long(mesh, prim_options(), unreal.Transform(), r, 24, 32)
    mag, freq = p["coarse"]
    noise(mesh, mag, freq, rng.randint(1, 10 ** 6))
    mag, freq = p["fine"]
    noise(mesh, mag, freq, rng.randint(1, 10 ** 6))
    smooth(mesh, 3, 0.15)

    # Hollow it: subtract a noised inner sphere, offset so the opening sits to
    # one side rather than splitting the shell symmetrically.
    cavity = new_mesh()
    Prim.append_sphere_lat_long(cavity, prim_options(), unreal.Transform(),
                                r * p["cavity"], 20, 26)
    noise(cavity, r * 0.05, 0.03, rng.randint(1, 10 ** 6))
    offset = unreal.Transform(
        location=unreal.Vector(0, 0, r * 0.45),
        rotation=unreal.Rotator(), scale=unreal.Vector(1, 1, 1))
    Booleans.apply_mesh_boolean(mesh, unreal.Transform(), cavity, offset,
                                unreal.GeometryScriptBooleanOperation.SUBTRACT,
                                unreal.GeometryScriptMeshBooleanOptions())
    simplify_to(mesh, p["tris"])
    set_pivot(mesh, "base")
    facet(mesh, 32.0)
    return mesh


def build_spike(rng, p):
    """Stalactite / stalagmite: a tapered cone with light noise."""
    mesh = new_mesh()
    Prim.append_cone(mesh, prim_options(), unreal.Transform(),
                     p["base_radius"], p["base_radius"] * 0.08,
                     p["height"], 16, 6, True)
    mag, freq = p["noise"]
    noise(mesh, mag, freq, rng.randint(1, 10 ** 6))
    smooth(mesh, 2, 0.12)
    simplify_to(mesh, p["tris"])
    if p["hanging"]:
        # Flip so it points downward, then pivot at the top where it meets rock.
        Xform.transform_mesh(mesh, unreal.Transform(
            location=unreal.Vector(0, 0, 0),
            rotation=unreal.Rotator(pitch=180.0, yaw=0.0, roll=0.0),
            scale=unreal.Vector(1, 1, 1)))
        set_pivot(mesh, "top")
    else:
        set_pivot(mesh, "base")
    return mesh


BUILDERS = {"rock": build_rock, "crystal": build_crystal,
            "geode": build_geode, "spike": build_spike}


# --------------------------------------------------------------------------
# Material
# --------------------------------------------------------------------------

def ensure_crystal_material():
    """One new material instance: crystals want their own colour.

    M_IOC_ProceduralRock is the parent because it is the only shipped material
    whose colour and roughness are real parameters -- M_IOC_MathRock's colours
    are constants an instance cannot override. It also had no consumers before.
    """
    if unreal.EditorAssetLibrary.does_asset_exist(CRYSTAL_MATERIAL):
        return unreal.load_asset(CRYSTAL_MATERIAL)
    parent = unreal.load_asset(CRYSTAL_PARENT)
    if parent is None:
        problem("MI_IOC_Crystal", "parent material not found: " + CRYSTAL_PARENT)
        return None
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mi = tools.create_asset("MI_IOC_Crystal", MAT_DIR,
                            unreal.MaterialInstanceConstant,
                            unreal.MaterialInstanceConstantFactoryNew())
    if mi is None:
        problem("MI_IOC_Crystal", "could not create instance")
        return None
    lib = unreal.MaterialEditingLibrary
    lib.set_material_instance_parent(mi, parent)
    lib.set_material_instance_vector_parameter_value(
        mi, "BaseColor", unreal.LinearColor(0.12, 0.30, 0.46, 1.0))
    lib.set_material_instance_scalar_parameter_value(mi, "Roughness", 0.18)
    lib.update_material_instance(mi)
    unreal.EditorAssetLibrary.save_loaded_asset(mi, only_if_is_dirty=False)

    # Verify: set_material_instance_* can no-op if the parameter name is wrong.
    got = lib.get_material_instance_vector_parameter_value(mi, "BaseColor")
    if abs(got.b - 0.46) > 1e-3:
        problem("MI_IOC_Crystal",
                "BaseColor did not take (got {})".format(got))
    return mi


# --------------------------------------------------------------------------

def build_asset(name, family, seed, params, material):
    rng = random.Random(seed)
    lod0 = BUILDERS[family](rng, params)

    # LOD1 from a rebuilt-and-simplified copy of the same seed, so LODs are
    # deterministic rather than dependent on the editor's reducer settings.
    rng_lod = random.Random(seed)
    lod1 = BUILDERS[family](rng_lod, params)
    simplify_to(lod1, max(60, int(tri_count(lod0) * 0.35)))

    opts = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    opts.set_editor_property("enable_recompute_normals", family == "rock")
    opts.set_editor_property("enable_recompute_tangents", True)
    opts.set_editor_property("enable_nanite", False)
    opts.set_editor_property("enable_collision", True)

    path = "{}/{}".format(GEO_DIR, name)
    # NOTE: UE's Python binding mangles the "LODs" acronym to "lo_ds".
    create_lods = getattr(NewAsset, "create_new_static_mesh_asset_from_mesh_lo_ds",
                          None) or getattr(
        NewAsset, "create_new_static_mesh_asset_from_mesh_lods")
    result = create_lods([lod0, lod1], path, opts)
    asset = result[0] if isinstance(result, tuple) else result
    if asset is None:
        problem(name, "asset creation returned None")
        return None

    # Simple collision from a heavily simplified copy: scatter props are
    # decoration, so a convex approximation is right and cheap.
    coll_src = BUILDERS[family](random.Random(seed), params)
    simplify_to(coll_src, 40)
    try:
        Collision.set_static_mesh_collision_from_mesh(
            coll_src, asset,
            unreal.GeometryScriptCollisionFromMeshOptions(),
            unreal.GeometryScriptSetStaticMeshCollisionOptions())
    except Exception as exc:  # noqa: BLE001
        problem(name, "collision generation failed: {}".format(exc))

    if material is not None:
        try:
            asset.set_editor_property(
                "static_materials",
                [unreal.StaticMaterial(material_interface=material,
                                       material_slot_name="IOC_Scatter")])
        except Exception as exc:  # noqa: BLE001
            problem(name, "material assignment failed: {}".format(exc))

    unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)

    box = asset.get_bounding_box()
    entry = {
        "tris_lod0": tri_count(lod0), "tris_lod1": tri_count(lod1),
        "name": name, "family": family, "seed": seed, "path": path,
        "params": {k: list(v) if isinstance(v, tuple) else v
                   for k, v in params.items()},
        "bounds_cm": [round(box.max.x - box.min.x, 1),
                      round(box.max.y - box.min.y, 1),
                      round(box.max.z - box.min.z, 1)],
        "lods": asset.get_num_lods() if hasattr(asset, "get_num_lods") else None,
        "material": material.get_path_name() if material else None,
    }
    report["assets"].append(entry)
    unreal.log("[IOC-SCATTER] built {} ({} bounds {})".format(
        name, family, entry["bounds_cm"]))
    return asset


PCG_MASTER = "/InstantOrganicCaves/InstantOrganicCaves/PCG/IOC_Graph_Master"

# Which generated meshes each StaticMeshSpawner in the master graph should use,
# keyed by the node's graph position (node names are not stable across rebuilds).
SPAWNER_MESHES = {
    (640, 32):  ["SM_IOC_Rock_A", "SM_IOC_Rock_B", "SM_IOC_Rock_C"],
    (800, 272): ["SM_IOC_Crystal_A", "SM_IOC_Crystal_B"],
}


def wire_pcg_spawners():
    """Point the shipped PCG graph at the library this script just generated.

    Done here rather than in the content rebuild because the meshes must exist
    first, and because rebuilding content deletes M_IOC_ProceduralRock -- which
    MI_IOC_Crystal now references, and deleting a referenced asset crashes the
    editor outright.
    """
    graph = unreal.load_asset(PCG_MASTER)
    if graph is None:
        problem("PCG", "master graph not found: " + PCG_MASTER)
        return

    entry_type = getattr(unreal, "PCGMeshSelectorWeightedEntry", None)
    if entry_type is None:
        problem("PCG", "PCGMeshSelectorWeightedEntry unavailable")
        return

    wired = 0
    for node in graph.get_editor_property("nodes"):
        settings = node.get_settings()
        if settings is None or settings.get_class().get_name() != \
                "PCGStaticMeshSpawnerSettings":
            continue
        x, y = node.get_node_position()
        names = SPAWNER_MESHES.get((x, y))
        if names is None:
            problem("PCG", "unmapped spawner at ({}, {})".format(x, y))
            continue
        try:
            sel = settings.get_editor_property("mesh_selector_parameters")
        except Exception as exc:  # noqa: BLE001
            problem("PCG", "no mesh selector at ({}, {}): {}".format(x, y, exc))
            continue

        # Build entries from scratch: mutating the existing descriptors in place
        # does not stick.
        rebuilt, missing = [], []
        for n in names:
            mesh = unreal.load_asset("{}/{}".format(GEO_DIR, n))
            if mesh is None:
                missing.append(n)
                continue
            e = entry_type()
            d = e.get_editor_property("descriptor")
            d.set_editor_property("static_mesh", mesh)
            e.set_editor_property("descriptor", d)
            e.set_editor_property("weight", 1)
            rebuilt.append(e)
        sel.set_editor_property("mesh_entries", rebuilt)

        # Verify by value -- "an entry has some mesh" is not the same as "the
        # entry has the mesh we asked for".
        got = []
        for e in sel.get_editor_property("mesh_entries"):
            try:
                m = e.get_editor_property("descriptor").get_editor_property("static_mesh")
            except Exception:
                m = None
            got.append(str(m) if m else None)
        matched = sum(1 for n, g in zip(names, got) if g and n in g)
        if matched != len(names) or missing:
            problem("PCG", "spawner at ({}, {}): {}/{} meshes wired{}".format(
                x, y, matched, len(names),
                "; missing " + str(missing) if missing else ""))
        else:
            wired += 1
            unreal.log("[IOC-SCATTER] spawner at ({}, {}) -> {}".format(x, y, names))

    unreal.EditorAssetLibrary.save_loaded_asset(graph, only_if_is_dirty=False)
    report["pcg_spawners_wired"] = wired


def main():
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        ["/InstantOrganicCaves"], force_rescan=True)

    if not unreal.EditorAssetLibrary.does_directory_exist(GEO_DIR):
        unreal.EditorAssetLibrary.make_directory(GEO_DIR)

    rock_mat = unreal.load_asset(ROCK_MATERIAL)
    if rock_mat is None:
        problem("materials", "rock material missing: " + ROCK_MATERIAL)
    crystal_mat = ensure_crystal_material()

    for name, family, seed, params in CATALOGUE:
        mat = crystal_mat if family == "crystal" else rock_mat
        try:
            build_asset(name, family, seed, params, mat)
        except Exception:
            problem(name, "build raised")
            report["problems"][-1]["traceback"] = traceback.format_exc()

    wire_pcg_spawners()

    seeds = {
        "generator": "GenerateScatterMeshes.py",
        "note": "Regenerate any asset exactly by re-running with these seeds.",
        "triangle_budgets": {k: list(v) for k, v in TRI_BUDGET.items()},
        "assets": {a["name"]: {"family": a["family"], "seed": a["seed"],
                               "params": a["params"]}
                   for a in report["assets"]},
    }
    with open(SEEDS_FILE, "w") as fh:
        json.dump(seeds, fh, indent=2, sort_keys=True)

    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(REPORT, "w") as fh:
        json.dump(report, fh, indent=2, default=str)
    unreal.log("[IOC-SCATTER] {} assets, {} problems -> {}".format(
        len(report["assets"]), len(report["problems"]), REPORT))


main()
