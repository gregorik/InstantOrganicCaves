// Copyright (c) 2026 GregOrigin. All Rights Reserved.

using UnrealBuildTool;

/**
 * Editor-only half of Instant Organic Caves.
 *
 * The setup wizard, the Tools menu, installation validation and the authoring capture
 * command all used to live in the Runtime module behind WITH_EDITOR. That worked, but it
 * meant a Runtime module carrying ~6.5k lines of Slate and pulling UnrealEd, LevelEditor,
 * AssetTools and ToolMenus into every editor build of a shipping game module. Editor tooling
 * belongs in an Editor-type module.
 */
public class InstantOrganicCavesEditor : ModuleRules
{
    public InstantOrganicCavesEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InstantOrganicCaves",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "InputCore",
                "Projects",
                "Slate",
                "SlateCore",
                "ApplicationCore",
                "UnrealEd",
                "AssetRegistry",
                "AssetTools",
                "ToolMenus",
                "LevelEditor",
                "EditorFramework",
                "EditorSubsystem",
                "RenderCore",
                "RHI",
                // The wizard previews and configures cave actors, so it needs the same
                // geometry types the runtime module exposes on them.
                "GeometryCore",
                "GeometryFramework",
                "DynamicMesh",
            }
        );
    }
}
