// Copyright (c) 2026 GregOrigin. All Rights Reserved.


using UnrealBuildTool;

public class InstantOrganicCaves : ModuleRules
{
    public InstantOrganicCaves(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	//	bUseUnity = false;

        // 1. Public Dependencies
        // "PCG" is the only module needed to inherit from UPCGSettings/FPCGSimpleElement
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "PCG", // <--- The only PCG module you need here
				"PhysicsCore",             // Required for Physics Enums
				"GeometryCore",            // Core Math
				"GeometryFramework",       // Components (UDynamicMeshComponent)
				"DynamicMesh",
				"GeometryScriptingCore",
                "GeometryAlgorithms"

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
    }
}