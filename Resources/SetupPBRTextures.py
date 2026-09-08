import os
import unreal


def _connect(from_expr, from_output, to_expr, to_input):
    """Connect two expressions and fail loudly if UE rejects the names.

    ConnectMaterialExpressions returns false for an unknown pin or output name
    and changes nothing. Ignoring that return is how the shipped materials ended
    up with dead wires, so treat it as an error here.
    """
    if not unreal.MaterialEditingLibrary.connect_material_expressions(
            from_expr, from_output, to_expr, to_input):
        raise RuntimeError(
            "connect failed: {}.{} -> {}.{} (check the exact pin/output name)".format(
                from_expr.get_class().get_name(), from_output or "<default>",
                to_expr.get_class().get_name(), to_input))

import traceback

# Authoring helper: imports the source rock textures and rebuilds the shipped materials.
# Not shipped with the plugin (see Config/FilterPlugin.ini).
#
# Source images are read from IOC_TEXTURE_SOURCE_DIR, defaulting to a "SourceTextures"
# folder next to this script. They used to be hard-coded to absolute paths inside the
# author's user profile, so the script could not run anywhere else.
IOC_TEXTURE_SOURCE_DIR = os.environ.get(
    "IOC_TEXTURE_SOURCE_DIR",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "SourceTextures"))


def log_to_file(msg):
    unreal.log("[IOC] " + str(msg))

def setup_pbr_textures():
    try:
        log_to_file("Starting PBR Texture import...")
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        
        # 1. Import tasks
        #
        # Extension is resolved rather than assumed: the original JPGs were lost, and the
        # masters now in SourceTextures/ are lossless PNGs recovered from UTexture::Source.
        # The base name is what matters -- it decides the imported asset's name below.
        tasks = []
        names = ["tex_rock_obsidian", "tex_rock_limestone", "tex_rock_alien"]
        extensions = [".png", ".jpg", ".jpeg", ".tga", ".exr"]

        files = {}
        missing = []
        for name in names:
            found = None
            for ext in extensions:
                candidate = os.path.join(IOC_TEXTURE_SOURCE_DIR, name + ext)
                if os.path.isfile(candidate):
                    found = candidate
                    break
            if found:
                files[name] = found
            else:
                missing.append(name + " (" + "/".join(extensions) + ")")

        if missing:
            log_to_file("Missing source textures, aborting: " + ", ".join(missing))
            log_to_file("Set IOC_TEXTURE_SOURCE_DIR or place the images in " + IOC_TEXTURE_SOURCE_DIR)
            return
        
        for name, path in files.items():
            task = unreal.AssetImportTask()
            task.filename = path
            task.destination_path = "/InstantOrganicCaves/Textures"
            task.destination_name = "T_" + name.split("_", 1)[1].title().replace("_", "")
            task.automated = True
            task.replace_existing = True
            task.save = True
            tasks.append(task)
            
        asset_tools.import_asset_tasks(tasks)
        log_to_file("Imported textures.")
        
        # 2. Rebuild M_IOC_MathRock completely to include the Switch
        log_to_file("Rebuilding M_IOC_MathRock...")
        
        # Delete existing asset to avoid collision
        mat_path = "/InstantOrganicCaves/Materials/M_IOC_MathRock"
        if unreal.EditorAssetLibrary.does_asset_exist(mat_path):
            unreal.EditorAssetLibrary.delete_asset(mat_path)
            
        mat_factory = unreal.MaterialFactoryNew()
        mat = asset_tools.create_asset("M_IOC_MathRock", "/InstantOrganicCaves/Materials", unreal.Material, mat_factory)
        
        if not mat:
            log_to_file("Material M_IOC_MathRock failed to load/create!")
            return

        # Pure Math Nodes
        wp_node = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -800, 0)
        scale_node = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionMultiply, -600, -100)
        scale_node.set_editor_property("const_b", 0.002)

        noise_node = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionNoise, -400, -100)
        noise_node.set_editor_property("scale", 1.0)
        noise_node.set_editor_property("quality", 2)

        color_dark = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, -300)
        color_dark.set_editor_property("constant", unreal.LinearColor(0.03, 0.035, 0.04, 1.0))
        
        color_light = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, -200)
        color_light.set_editor_property("constant", unreal.LinearColor(0.1, 0.12, 0.06, 1.0))

        lerp_color = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -200, -200)

        rough_lerp = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -200, 100)
        rough_min = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 100)
        rough_min.set_editor_property("r", 0.6)
        rough_max = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 150)
        rough_max.set_editor_property("r", 0.9)

        # PBR Texture Switch Nodes
        switch = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionStaticSwitchParameter, 0, -300)
        switch.set_editor_property("parameter_name", "Use PBR Texture")
        switch.set_editor_property("default_value", False)
        
        tex_param = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -200, -500)
        tex_param.set_editor_property("parameter_name", "CaveStyleTexture")
        
        tex_asset = unreal.EditorAssetLibrary.load_asset("/InstantOrganicCaves/Textures/T_RockObsidian")
        if tex_asset:
            tex_param.set_editor_property("texture", tex_asset)

        # CONNECTING
        unreal.MaterialEditingLibrary.connect_material_expressions(wp_node, "", scale_node, "A")
        # The pin is "World Position", not "Position". The old name made this
        # call a silent no-op, so the x0.002 scale never reached the noise.
        _connect(scale_node, "", noise_node, "World Position")
        
        unreal.MaterialEditingLibrary.connect_material_expressions(color_dark, "", lerp_color, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(color_light, "", lerp_color, "B")
        unreal.MaterialEditingLibrary.connect_material_expressions(noise_node, "", lerp_color, "Alpha")

        unreal.MaterialEditingLibrary.connect_material_expressions(rough_min, "", rough_lerp, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(rough_max, "", rough_lerp, "B")
        unreal.MaterialEditingLibrary.connect_material_expressions(noise_node, "", rough_lerp, "Alpha")

        # Switch logic
        unreal.MaterialEditingLibrary.connect_material_expressions(lerp_color, "", switch, "False")
        unreal.MaterialEditingLibrary.connect_material_expressions(tex_param, "RGB", switch, "True")

        unreal.MaterialEditingLibrary.connect_material_property(switch, "", unreal.MaterialProperty.MP_BASE_COLOR)
        unreal.MaterialEditingLibrary.connect_material_property(rough_lerp, "", unreal.MaterialProperty.MP_ROUGHNESS)

        # Render flags, set here because this function *deletes and recreates* the material
        # above -- a fresh UMaterial defaults both of these to false, so rebuilding textures
        # would silently reintroduce transparent tunnels and grey scatter props.
        #
        #   two_sided: the player stands inside the cave shell, so the tunnel bore's surfaces
        #     are backfaces; one-sided culls them and the wall renders transparent.
        #   used_with_instanced_static_meshes: scatter props are placed on HISM components,
        #     and without it the engine silently substitutes WorldGridMaterial.
        #
        # Resources/SetMaterialRenderFlags.py owns this for all three base materials and is
        # the thing to run after any other material work; Content.MaterialRenderFlags is the
        # automation guard. Kept in sync here so a texture rebuild alone is still correct.
        for flag in ("two_sided", "used_with_instanced_static_meshes"):
            mat.set_editor_property(flag, True)
            if not mat.get_editor_property(flag):
                raise RuntimeError("could not set '{}' on M_IOC_MathRock".format(flag))
        unreal.MaterialEditingLibrary.recompile_material(mat)

        unreal.EditorAssetLibrary.save_loaded_asset(mat)
        
        # 3. Setup MI_IOC_CaveWalls
        log_to_file("Loading MI_IOC_CaveWalls...")
        mi = unreal.EditorAssetLibrary.load_asset("/InstantOrganicCaves/MI_IOC_CaveWalls")
        if not mi:
            log_to_file("MI_IOC_CaveWalls not found, creating it...")
            mat_inst_factory = unreal.MaterialInstanceConstantFactoryNew()
            mi = asset_tools.create_asset("MI_IOC_CaveWalls", "/InstantOrganicCaves", unreal.MaterialInstanceConstant, mat_inst_factory)
            
        if mi:
            log_to_file("Setting parent and parameters on MI_IOC_CaveWalls...")
            unreal.MaterialEditingLibrary.set_material_instance_parent(mi, mat)
            unreal.MaterialEditingLibrary.set_material_instance_static_switch_parameter_value(mi, "Use PBR Texture", True)
            if tex_asset:
                unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(mi, "CaveStyleTexture", tex_asset)
            
            unreal.MaterialEditingLibrary.update_material_instance(mi)
            unreal.EditorAssetLibrary.save_loaded_asset(mi)
            log_to_file("Success!")
        else:
            log_to_file("Failed to get/create MI_IOC_CaveWalls.")

    except Exception as e:
        log_to_file("EXCEPTION: " + str(e))
        log_to_file(traceback.format_exc())

if __name__ == "__main__":
    setup_pbr_textures()
