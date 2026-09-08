// Copyright (c) 2026 GregOrigin. All Rights Reserved.


using UnrealBuildTool;

public class InstantOrganicCaves : ModuleRules
{
    public InstantOrganicCaves(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

        // 1. Public Dependencies
        // "PCG" is the only module needed to inherit from UPCGSettings/FPCGSimpleElement
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "EnhancedInput",
                "PCG", // <--- The only PCG module you need here
				"PhysicsCore",             // Required for Physics Enums
				"GeometryCore",            // Core Math
				"GeometryFramework",       // Components (UDynamicMeshComponent)
				"DynamicMesh",
				"GeometryScriptingCore",
                "GeometryAlgorithms",
                "NavigationSystem",
                "DeveloperSettings",
                "NetCore"                  // FFastArraySerializer

            }
        );

        // 2. Private Dependencies
        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Projects",
                "Slate",
                "SlateCore",
               	"RenderCore", 
				"RHI"
            }
        );

        if (Target.bBuildEditor)
        {
            // Editor UI (the setup wizard, the Tools menu, validation) now lives in the
            // InstantOrganicCavesEditor module. What remains here is the WITH_EDITOR bake
            // path on AIOCProceduralActor, which genuinely has to sit on the runtime actor
            // because it is a CallInEditor UFUNCTION -- so it keeps the asset-authoring
            // dependencies, and nothing else. ToolMenus, LevelEditor and ApplicationCore
            // are gone.
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "UnrealEd",              // GEditor, ObjectTools for the bake
                "AssetRegistry",         // registering the baked asset
                "AssetTools",            // CreateUniqueAssetName
                "MeshDescription",       // FMeshDescription, FPolygonGroupID
                "StaticMeshDescription", // FStaticMeshAttributes
            });
        }
    }
}
