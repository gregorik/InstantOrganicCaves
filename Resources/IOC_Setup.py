import unreal
import os

def create_smart_material(asset_path, asset_name):
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    package_path = asset_path
    
    if unreal.EditorAssetLibrary.does_asset_exist(package_path + "/" + asset_name):
        return unreal.load_asset(package_path + "/" + asset_name)

    mat_factory = unreal.MaterialFactoryNew()
    material = asset_tools.create_asset(asset_name, package_path, unreal.Material, mat_factory)
    
    # --- Nodes ---
    # Vertex Color
    vtx_color = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVertexColor, -600, 0)

    # Colors
    color_wall = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, -200)
    color_wall.set_editor_property("parameter_name", "Color_Wall")
    color_wall.set_editor_property("default_value", unreal.LinearColor(0.15, 0.13, 0.12, 1.0)) # Brown-Grey

    color_floor = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, 200)
    color_floor.set_editor_property("parameter_name", "Color_Floor")
    color_floor.set_editor_property("default_value", unreal.LinearColor(0.05, 0.15, 0.05, 1.0)) # Moss Green

    color_ceiling = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, 400)
    color_ceiling.set_editor_property("parameter_name", "Color_Ceiling")
    color_ceiling.set_editor_property("default_value", unreal.LinearColor(0.02, 0.02, 0.05, 1.0)) # Wet Dark

    # Roughness
    rough_val = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -600, 600)
    rough_val.set_editor_property("parameter_name", "Roughness")
    rough_val.set_editor_property("default_value", 0.8)

    # Blending Logic (Lerp)
    # Base = Wall
    # Lerp 1: Wall -> Floor (using Green)
    lerp_floor = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -300, 0)
    
    # Lerp 2: Result -> Ceiling (using Blue)
    lerp_ceiling = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionLinearInterpolate, -100, 0)

    # Connections
    # Wall -> A of Lerp 1
    unreal.MaterialEditingLibrary.connect_material_expressions(color_wall, "", lerp_floor, "A")
    # Floor -> B of Lerp 1
    unreal.MaterialEditingLibrary.connect_material_expressions(color_floor, "", lerp_floor, "B")
    # Vtx Green -> Alpha of Lerp 1
    unreal.MaterialEditingLibrary.connect_material_expressions(vtx_color, "Green", lerp_floor, "Alpha")

    # Lerp 1 -> A of Lerp 2
    unreal.MaterialEditingLibrary.connect_material_expressions(lerp_floor, "", lerp_ceiling, "A")
    # Ceiling -> B of Lerp 2
    unreal.MaterialEditingLibrary.connect_material_expressions(color_ceiling, "", lerp_ceiling, "B")
    # Vtx Blue -> Alpha of Lerp 2
    unreal.MaterialEditingLibrary.connect_material_expressions(vtx_color, "Blue", lerp_ceiling, "Alpha")

    # Final Connect
    unreal.MaterialEditingLibrary.connect_material_property(lerp_ceiling, "", unreal.MaterialProperty.MP_BASE_COLOR)
    unreal.MaterialEditingLibrary.connect_material_property(rough_val, "", unreal.MaterialProperty.MP_ROUGHNESS)

    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(material.get_path_name())
    unreal.log("Created Smart Material: " + asset_name)
    return material

def create_blueprint(parent_class_name, package_path, asset_name, material_asset):
    if unreal.EditorAssetLibrary.does_asset_exist(package_path + "/" + asset_name):
        return

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    parent_class = unreal.load_object(None, "/Script/InstantOrganicCaves.IOCProceduralActor")
    
    if not parent_class:
        unreal.log_error("Could not find parent class IOCProceduralActor!")
        return

    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)

    bp_asset = asset_tools.create_asset(asset_name, package_path, unreal.Blueprint, factory)
    
    # We can't easily set CDO properties via Python safely without loading the generated class
    # But we can save it.
    unreal.EditorAssetLibrary.save_asset(bp_asset.get_path_name())
    unreal.log("Created Blueprint: " + asset_name)

def run_setup():
    plugin_content_path = "/InstantOrganicCaves"
    
    # 1. Create Smart Material
    mat = create_smart_material(plugin_content_path, "M_IOC_SmartCave")
    
    # 2. Create Material Instance
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    if mat and not unreal.EditorAssetLibrary.does_asset_exist(plugin_content_path + "/MI_IOC_SmartCave_Inst"):
        mi_factory = unreal.MaterialInstanceConstantFactoryNew()
        mi_asset = asset_tools.create_asset("MI_IOC_SmartCave_Inst", plugin_content_path, unreal.MaterialInstanceConstant, mi_factory)
        mi_asset.set_editor_property("parent", mat)
        unreal.EditorAssetLibrary.save_asset(mi_asset.get_path_name())
        unreal.log("Created Material Instance: MI_IOC_SmartCave_Inst")

    # 3. Create Blueprint
    create_blueprint("IOCProceduralActor", plugin_content_path, "BP_IOC_Cave", mat)

if __name__ == "__main__":
    run_setup()