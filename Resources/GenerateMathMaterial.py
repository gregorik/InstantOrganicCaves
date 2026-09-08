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
import sys

# Authoring helper: regenerates the shipped M_IOC_MathRock master material.
# Not shipped with the plugin (see Config/FilterPlugin.ini).


def log_to_file(msg):
    # Goes to the Output Log. This used to append to a hard-coded absolute path on the
    # author's machine, which simply fails anywhere else.
    unreal.log("[IOC] " + str(msg))

def create_math_material():
    try:
        log_to_file("Starting material creation...")
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        mat_factory = unreal.MaterialFactoryNew()
        
        # Use /Game/ to ensure it mounts properly
        package_path = "/Game/Materials"
        asset_name = "M_IOC_MathRock"
        
        log_to_file(f"Creating asset at {package_path}/{asset_name}...")
        mat = asset_tools.create_asset(asset_name, package_path, unreal.Material, mat_factory)
        if not mat:
            log_to_file("Failed to create material asset.")
            return

        log_to_file("Asset created. Adding nodes...")
        # 1. World Position
        wp_node = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -800, 0)
        
        # 2. Scale
        scale_node = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionMultiply, -600, -100)
        scale_node.set_editor_property("const_b", 0.002)

        # 3. Noise
        noise_node = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionNoise, -400, -100)
        noise_node.set_editor_property("scale", 1.0)
        noise_node.set_editor_property("quality", 2)

        # 4. Colors
        color_dark = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, -300)
        color_dark.set_editor_property("constant", unreal.LinearColor(0.03, 0.035, 0.04, 1.0))
        
        color_light = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, -200)
        color_light.set_editor_property("constant", unreal.LinearColor(0.1, 0.12, 0.06, 1.0))

        # 5. Lerp Color
        lerp_color = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -200, -200)

        # 6. Roughness
        rough_lerp = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -200, 100)
        rough_min = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 100)
        rough_min.set_editor_property("r", 0.6)
        rough_max = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 150)
        rough_max.set_editor_property("r", 0.9)

        log_to_file("Connecting nodes...")
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

        unreal.MaterialEditingLibrary.connect_material_property(lerp_color, "", unreal.MaterialProperty.MP_BASE_COLOR)
        unreal.MaterialEditingLibrary.connect_material_property(rough_lerp, "", unreal.MaterialProperty.MP_ROUGHNESS)
        
        log_to_file("Saving asset...")
        unreal.EditorAssetLibrary.save_loaded_asset(mat)
        
        log_to_file("Moving asset to plugin folder...")
        success = unreal.EditorAssetLibrary.rename_asset("/Game/Materials/M_IOC_MathRock", "/InstantOrganicCaves/Materials/M_IOC_MathRock")
        if success:
            log_to_file("Successfully created and moved M_IOC_MathRock!")
        else:
            log_to_file("Asset created but failed to move to plugin folder.")
            
    except Exception as e:
        log_to_file("EXCEPTION: " + str(e))
        log_to_file(traceback.format_exc())

if __name__ == "__main__":
    create_math_material()
