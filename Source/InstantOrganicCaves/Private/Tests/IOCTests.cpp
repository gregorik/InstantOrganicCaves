// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "HAL/IConsoleManager.h"
#include "IOCProceduralActor.h"
#include "IOCStreamingManager.h"
#include "IOCCharacter.h"
#include "UDynamicMesh.h"
#include "IOCCarvingComponent.h"
#include "Components/SplineComponent.h"
#include "IOCSettings.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/FileManager.h"
#include "UObject/PackageFileSummary.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "UObject/UnrealType.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/PackageName.h"
#include "InstantOrganicCavesModule.h"
#include "IOCShowcaseLauncher.h"
#include "Components/LightComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

// -----------------------------------------------------------------------
// Shipped content must be authored on the oldest engine the plugin claims to support.
//
// A package records the object version that wrote it, and an older engine refuses to load
// anything above its own ceiling -- forward compatibility only runs one way. So a .uasset
// saved in 5.6 is unusable in 5.5 no matter what the descriptor says, and there is no
// downgrade path: it has to be re-authored on the floor version.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCContentEngineFloorTest,
    "InstantOrganicCaves.Packaging.ContentEngineFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCContentEngineFloorTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    const TCHAR* OldestSupportedEngine = TEXT("5.5");

    // Two independent gates, both read straight off disk.
    //
    // FPackageFileSummary's own serialiser is NOT usable for this: when it meets a package
    // whose LegacyFileVersion predates the running engine it calls FileVersionUE.Reset() and
    // hands back zeros, so an incompatible asset reads as version 0 and looks fine. The header
    // layout is stable, so parse it directly instead.
    //
    // 1. LegacyFileVersion: 5.5 writes -8 and rejects anything lower; 5.6+ write -9.
    // 2. FileVersionUE5: AUTOMATIC_VERSION on 5.5 is
    //    ASSETREGISTRY_PACKAGEBUILDDEPENDENCIES == 1014 (5.6 = 1018, 5.7/5.8 = 1019).
    //
    // Bump both only if the supported floor itself moves.
    constexpr int32 OldestSupportedLegacyFileVersion = -8;
    constexpr int32 OldestSupportedUE5PackageVersion = 1014;
    constexpr int32 PackageFileTag = 0x9E2A83C1;

    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("InstantOrganicCaves"));
    if (!Plugin.IsValid())
    {
        AddError(TEXT("InstantOrganicCaves plugin not found."));
        return false;
    }

    const FString ContentDir = Plugin->GetContentDir();
    TArray<FString> PackageFiles;
    IFileManager::Get().FindFilesRecursive(PackageFiles, *ContentDir, TEXT("*.uasset"), true, false);
    IFileManager::Get().FindFilesRecursive(PackageFiles, *ContentDir, TEXT("*.umap"), true, false, false);

    if (PackageFiles.IsEmpty())
    {
        AddWarning(FString::Printf(TEXT("No plugin content found under '%s'; nothing to verify."), *ContentDir));
        return true;
    }

    int32 IncompatibleCount = 0;
    for (const FString& PackageFile : PackageFiles)
    {
        TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*PackageFile));
        if (!Reader)
        {
            AddError(FString::Printf(TEXT("Could not open '%s' for version inspection."), *PackageFile));
            ++IncompatibleCount;
            continue;
        }

        int32 Tag = 0;
        int32 LegacyFileVersion = 0;
        *Reader << Tag;
        *Reader << LegacyFileVersion;

        if (Tag != PackageFileTag)
        {
            AddError(FString::Printf(TEXT("'%s' is not a recognisable Unreal package."),
                *FPaths::GetCleanFilename(PackageFile)));
            ++IncompatibleCount;
            continue;
        }

        if (LegacyFileVersion != -4)
        {
            int32 LegacyUE3Version = 0;
            *Reader << LegacyUE3Version;
        }

        int32 FileVersionUE4 = 0;
        int32 FileVersionUE5 = 0;
        *Reader << FileVersionUE4;
        if (LegacyFileVersion <= -8)
        {
            *Reader << FileVersionUE5;
        }

        const bool bLegacyTooNew = LegacyFileVersion < OldestSupportedLegacyFileVersion;
        const bool bObjectVersionTooNew = FileVersionUE5 > OldestSupportedUE5PackageVersion;

        if (bLegacyTooNew || bObjectVersionTooNew)
        {
            ++IncompatibleCount;
            AddError(FString::Printf(
                TEXT("'%s' was saved by a newer engine (legacy version %d, UE5 object version %d; ")
                TEXT("UE %s accepts %d and %d). It will not load on the declared floor. ")
                TEXT("Re-author and re-save this asset on UE %s."),
                *FPaths::GetCleanFilename(PackageFile),
                LegacyFileVersion,
                FileVersionUE5,
                OldestSupportedEngine,
                OldestSupportedLegacyFileVersion,
                OldestSupportedUE5PackageVersion,
                OldestSupportedEngine));
        }
    }

    AddInfo(FString::Printf(TEXT("Checked %d shipped packages against the UE %s floor; %d incompatible."),
        PackageFiles.Num(), OldestSupportedEngine, IncompatibleCount));

    return IncompatibleCount == 0;
#else
    return true;
#endif
}

// -----------------------------------------------------------------------
// The shipped scatter meshes must actually be usable as scatter.
//
// These are generated by Resources/GenerateScatterMeshes.py rather than hand-authored, so
// nothing stops a bad parameter from producing a mesh that still loads but is useless --
// a prop the size of the tunnel, no collision, one LOD, or a pivot in the middle so half
// of it sinks into the rock when placed on a surface. "Looks fine in the content browser"
// is not a check; these are.
//
// What this deliberately does NOT check is whether the props look good. That needs a human.
// -----------------------------------------------------------------------
// The shipped materials need two render flags, and neither failure is visible in the editor.
//
//   TwoSided
//       A cave is a shell: TunnelRadius of air inside WallThickness of rock. The player
//       stands *inside* it, looking at the bore's inward-facing surface, which a one-sided
//       material culls as backfaces -- the tunnel wall renders transparent and you see
//       straight out of the level.
//
//   MATUSAGE_InstancedStaticMeshes
//       Scatter props are UHierarchicalInstancedStaticMeshComponents. Without the usage
//       flag the engine silently substitutes WorldGridMaterial and every prop renders grey
//       *in game only*. AIOCProceduralActor calls CheckMaterialUsage() at runtime, which
//       sets the flag and dirties the package in the editor but cannot do so in a cooked or
//       -game build -- so the editor always looked correct and the shipped product did not.
//
// Both are properties of the base UMaterial, so this walks each instance to its base.
// Resources/SetMaterialRenderFlags.py is what sets them; this is what proves it happened.
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCMaterialRenderFlagsTest,
    "InstantOrganicCaves.Content.MaterialRenderFlags",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCMaterialRenderFlagsTest::RunTest(const FString& Parameters)
{
    // The materials as they are actually applied -- the instances the plugin references,
    // plus the bases, so a broken parent link is caught too.
    static const TCHAR* MaterialPaths[] = {
        TEXT("/InstantOrganicCaves/MI_IOC_CaveWalls.MI_IOC_CaveWalls"),
        TEXT("/InstantOrganicCaves/MI_IOC_Crystal.MI_IOC_Crystal"),
        TEXT("/InstantOrganicCaves/MI_IOC_SmartCave_Inst.MI_IOC_SmartCave_Inst"),
        TEXT("/InstantOrganicCaves/Materials/M_IOC_MathRock.M_IOC_MathRock"),
        TEXT("/InstantOrganicCaves/M_IOC_ProceduralRock.M_IOC_ProceduralRock"),
        TEXT("/InstantOrganicCaves/M_IOC_SmartCave.M_IOC_SmartCave"),
    };

    int32 Checked = 0;
    for (const TCHAR* Path : MaterialPaths)
    {
        UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, Path);
        if (Material == nullptr)
        {
            AddError(FString::Printf(
                TEXT("Shipped material '%s' could not be loaded."), Path));
            continue;
        }

        if (!Material->IsTwoSided())
        {
            AddError(FString::Printf(
                TEXT("Material '%s' is one-sided. The player stands inside the cave shell, ")
                TEXT("so its inward-facing wall surfaces are backfaces and get culled -- ")
                TEXT("tunnels render transparent."), Path));
        }

        // Usage flags live on the base material; an instance has none of its own.
        const UMaterial* Base = Material->GetMaterial();
        if (Base == nullptr)
        {
            AddError(FString::Printf(
                TEXT("Material '%s' has no base material."), Path));
            continue;
        }

        if (!Base->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes))
        {
            AddError(FString::Printf(
                TEXT("Base material '%s' (of '%s') lacks bUsedWithInstancedStaticMeshes, so ")
                TEXT("scatter props placed on HISM components fall back to the default grey ")
                TEXT("material in a packaged build."), *Base->GetPathName(), Path));
        }

        ++Checked;
    }

    if (Checked == 0)
    {
        AddError(TEXT("No shipped materials could be checked at all."));
    }
    AddInfo(FString::Printf(TEXT("Checked render flags on %d shipped material(s)."), Checked));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCScatterMeshLibraryTest,
    "InstantOrganicCaves.Content.ScatterMeshLibrary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    struct FIOCScatterMeshSpec
    {
        const TCHAR* AssetPath;
        int32 MinTriangles;
        int32 MaxTriangles;
        float MinLargestDimCm;
        float MaxLargestDimCm;
        // Scatter aligns instances to the surface normal, so a floor prop must sit on its
        // origin and a hanging prop must hang from it. Pivot in the middle = half buried.
        bool bPivotAtTop;
    };

    // Bounds are generous: they exist to catch an order-of-magnitude authoring slip, not to
    // pin the art. Default TunnelRadius is 300cm, so nothing here may approach 6m.
    static const FIOCScatterMeshSpec GIOCScatterMeshes[] = {
        { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Rock_A"),      400, 3000,  60.f, 220.f, false },
        { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Rock_B"),      400, 3000,  35.f, 130.f, false },
        { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Rock_C"),      200, 3000,  15.f,  70.f, false },
        { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Crystal_A"),   200, 1500,  50.f, 200.f, false },
        { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Crystal_B"),   200, 1500,  25.f, 110.f, false },
        { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Geode"),       400, 3000,  40.f, 150.f, false },
        { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Stalactite"),  200, 1500,  70.f, 260.f, true  },
        { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Stalagmite"),  200, 1500,  60.f, 220.f, false },
    };
}

bool FIOCScatterMeshLibraryTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    int32 Failures = 0;

    for (const FIOCScatterMeshSpec& Spec : GIOCScatterMeshes)
    {
        const FString Name = FPackageName::GetShortName(Spec.AssetPath);

        UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Spec.AssetPath);
        if (!Mesh)
        {
            AddError(FString::Printf(TEXT("Scatter mesh '%s' is missing. Regenerate the ")
                TEXT("library with Resources/GenerateScatterMeshes.py."), *Name));
            ++Failures;
            continue;
        }

        // --- material: exactly one slot, and it must be filled ---
        const TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
        if (Materials.Num() != 1)
        {
            AddError(FString::Printf(TEXT("'%s' has %d material slots; scatter props must have exactly 1."),
                *Name, Materials.Num()));
            ++Failures;
        }
        else if (Materials[0].MaterialInterface == nullptr)
        {
            AddError(FString::Printf(TEXT("'%s' has an empty material slot; it would render with the ")
                TEXT("default checker material."), *Name));
            ++Failures;
        }

        // --- LODs: a scatter prop is instanced in the hundreds, so it needs at least one
        //     reduction step or distant instances cost full detail ---
        const int32 NumLODs = Mesh->GetNumLODs();
        if (NumLODs < 2)
        {
            AddError(FString::Printf(TEXT("'%s' has %d LOD(s); scatter props need at least 2."),
                *Name, NumLODs));
            ++Failures;
        }

        // --- triangle budget on LOD0 ---
        if (Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0)
        {
            const int32 Tris = Mesh->GetRenderData()->LODResources[0].GetNumTriangles();
            if (Tris < Spec.MinTriangles || Tris > Spec.MaxTriangles)
            {
                AddError(FString::Printf(TEXT("'%s' LOD0 has %d triangles, outside the %d-%d budget."),
                    *Name, Tris, Spec.MinTriangles, Spec.MaxTriangles));
                ++Failures;
            }

            if (NumLODs >= 2)
            {
                const int32 LodTris = Mesh->GetRenderData()->LODResources[1].GetNumTriangles();
                if (LodTris >= Tris)
                {
                    AddError(FString::Printf(TEXT("'%s' LOD1 has %d triangles vs LOD0's %d; ")
                        TEXT("the LOD saves nothing."), *Name, LodTris, Tris));
                    ++Failures;
                }
            }
        }
        else
        {
            AddError(FString::Printf(TEXT("'%s' has no render data."), *Name));
            ++Failures;
        }

        // --- simple collision: scatter props are walked into ---
        const UBodySetup* Body = Mesh->GetBodySetup();
        const int32 CollisionPrims = Body
            ? Body->AggGeom.ConvexElems.Num() + Body->AggGeom.BoxElems.Num()
              + Body->AggGeom.SphereElems.Num() + Body->AggGeom.SphylElems.Num()
            : 0;
        if (CollisionPrims == 0)
        {
            AddError(FString::Printf(TEXT("'%s' has no simple collision."), *Name));
            ++Failures;
        }

        // --- real-world scale ---
        const FBox Bounds = Mesh->GetBoundingBox();
        const FVector Size = Bounds.GetSize();
        const float Largest = FMath::Max3(Size.X, Size.Y, Size.Z);
        if (Largest < Spec.MinLargestDimCm || Largest > Spec.MaxLargestDimCm)
        {
            AddError(FString::Printf(TEXT("'%s' largest dimension is %.1fcm, outside the ")
                TEXT("%.0f-%.0fcm range expected for its family."),
                *Name, Largest, Spec.MinLargestDimCm, Spec.MaxLargestDimCm));
            ++Failures;
        }

        // --- pivot placement ---
        const float Tolerance = FMath::Max(2.0f, Size.Z * 0.05f);
        const float PivotOffset = Spec.bPivotAtTop ? Bounds.Max.Z : Bounds.Min.Z;
        if (FMath::Abs(PivotOffset) > Tolerance)
        {
            AddError(FString::Printf(TEXT("'%s' pivot is %.1fcm from its %s; placed on a surface ")
                TEXT("it would float or sink."),
                *Name, PivotOffset, Spec.bPivotAtTop ? TEXT("top") : TEXT("base")));
            ++Failures;
        }
    }

    AddInfo(FString::Printf(TEXT("Checked %d scatter meshes; %d failure(s)."),
        (int32)UE_ARRAY_COUNT(GIOCScatterMeshes), Failures));

    return Failures == 0;
#else
    return true;
#endif
}

// -----------------------------------------------------------------------
// The demo commands must be safe to run repeatedly and must leave no residue.
//
// The showcase family had no coverage at all until now, and a manual run found two real
// defects: SpawnTunnelDemo and SpawnSpectacular each spawned their own "IOC_Player", so
// running both left two pawns and made PIE possession ambiguous; and ClearShowcase only
// cleared the showcase, so the tunnel and spectacular demos accumulated in the user's level
// with no command to remove them.
//
// A customer evaluating the plugin runs these commands in their own level. Anything left
// behind is litter they have to find and delete by hand.
// -----------------------------------------------------------------------
// The shipped demo map must actually ship, and must still auto-run.
//
// It exists so a customer has a route in that does not depend on the setup wizard: open the
// level, press Play, watch the guided showcase. That only works if the map is in the package
// AND still holds a launcher with auto-start on -- either of which a content rebuild or a
// packaging-filter edit could quietly drop.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCDemoMapTest,
    "InstantOrganicCaves.Demo.DemoMap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCDemoMapTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    static const TCHAR* MapPackage = TEXT("/InstantOrganicCaves/Maps/IOC_DemoMap");

    if (!FPackageName::DoesPackageExist(MapPackage))
    {
        AddError(FString::Printf(
            TEXT("The demo map '%s' is not installed. Regenerate it with ")
            TEXT("Resources/GenerateDemoMap.py."), MapPackage));
        return false;
    }

    UWorld* DemoWorld = LoadObject<UWorld>(nullptr,
        TEXT("/InstantOrganicCaves/Maps/IOC_DemoMap.IOC_DemoMap"));
    if (!DemoWorld || !DemoWorld->PersistentLevel)
    {
        AddError(TEXT("The demo map package exists but its world could not be loaded."));
        return false;
    }

    int32 Launchers = 0;
    bool bAutoStarts = false;
    for (AActor* Actor : DemoWorld->PersistentLevel->Actors)
    {
        if (AIOCShowcaseLauncher* Launcher = Cast<AIOCShowcaseLauncher>(Actor))
        {
            ++Launchers;
            bAutoStarts |= Launcher->bAutoStart;
        }
    }

    if (Launchers != 1)
    {
        AddError(FString::Printf(
            TEXT("The demo map contains %d IOCShowcaseLauncher actor(s); expected exactly 1. ")
            TEXT("Without it, pressing Play does nothing."), Launchers));
    }
    else if (!bAutoStarts)
    {
        AddError(TEXT("The demo map's launcher has Start Automatically disabled, so pressing ")
            TEXT("Play would show an empty level."));
    }

    AddInfo(FString::Printf(TEXT("Demo map present with %d launcher(s), auto-start %s."),
        Launchers, bAutoStarts ? TEXT("on") : TEXT("off")));

    return !HasAnyErrors();
#else
    return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCShowcaseLifecycleTest,
    "InstantOrganicCaves.Demo.ShowcaseLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    static int32 IOCCountActorsLabelled(UWorld* World, const TCHAR* Prefix)
    {
        int32 Count = 0;
#if WITH_EDITOR
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (IsValid(*It) && It->GetActorLabel().StartsWith(Prefix))
            {
                ++Count;
            }
        }
#endif
        return Count;
    }
}

bool FIOCShowcaseLifecycleTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor)
    {
        return false;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        AddError(TEXT("No Editor World found."));
        return false;
    }

    // Start from a clean slate regardless of what other tests left behind.
    FInstantOrganicCavesModule::ClearAllDemos(World);
    const int32 Baseline = IOCCountActorsLabelled(World, TEXT("IOC_"));
    if (Baseline != 0)
    {
        AddError(FString::Printf(
            TEXT("ClearAllDemos left %d IOC_ actor(s) behind before the test began."), Baseline));
    }

    // Snapshot the lights already in the level so the mobility check below can tell which
    // ones the showcase itself added.
    TSet<AActor*> LightsBeforeShowcase;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (IsValid(*It) && It->FindComponentByClass<ULightComponentBase>())
        {
            LightsBeforeShowcase.Add(*It);
        }
    }

    // --- showcase spawns its full set ---
    FInstantOrganicCavesModule::SpawnShowcase(FIOCShowcaseOptions(), World);
    const int32 AfterFirst = IOCCountActorsLabelled(World, TEXT("IOC_Showcase_"));
    if (AfterFirst < 8)
    {
        AddError(FString::Printf(
            TEXT("SpawnShowcase produced %d IOC_Showcase_ actors; expected the 8 documented sections."),
            AfterFirst));
    }

    // --- re-running must not duplicate ---
    FInstantOrganicCavesModule::SpawnShowcase(FIOCShowcaseOptions(), World);
    const int32 AfterSecond = IOCCountActorsLabelled(World, TEXT("IOC_Showcase_"));
    if (AfterSecond != AfterFirst)
    {
        AddError(FString::Printf(
            TEXT("Re-running SpawnShowcase changed the actor count from %d to %d; it must be idempotent."),
            AfterFirst, AfterSecond));
    }

    // --- the showcase must actually show the plugin's own content ---
    //
    // It previously scattered only /Engine/BasicShapes primitives while its own caption
    // claimed otherwise, so the flagship demo never displayed the shipped mesh library.
    // A live run was the only thing that caught it; this makes it a build failure instead.
    {
        int32 ScatterLayers = 0;
        int32 PluginMeshLayers = 0;
        for (TActorIterator<AIOCProceduralActor> It(World); It; ++It)
        {
            if (!IsValid(*It) || !It->GetActorLabel().StartsWith(TEXT("IOC_Showcase_")))
            {
                continue;
            }
            for (const FIOCScatterLayer& Layer : It->DecorationLayers)
            {
                if (!Layer.Mesh)
                {
                    continue;
                }
                ++ScatterLayers;
                if (Layer.Mesh->GetPathName().Contains(TEXT("/InstantOrganicCaves/")))
                {
                    ++PluginMeshLayers;
                }
            }
        }

        if (ScatterLayers == 0)
        {
            AddError(TEXT("No showcase cave declared any scatter layers; the Scatter Props ")
                TEXT("section would show nothing."));
        }
        else if (PluginMeshLayers == 0)
        {
            AddError(FString::Printf(
                TEXT("All %d showcase scatter layers use non-plugin meshes. The demo is meant to ")
                TEXT("display the shipped SM_IOC_* library, not engine primitives."), ScatterLayers));
        }
        AddInfo(FString::Printf(TEXT("Showcase scatter: %d of %d layers use plugin meshes."),
            PluginMeshLayers, ScatterLayers));
    }

    // --- lights must be Movable or Lumen GI ignores them and the caves render near-black ---
    //
    // Checking only IOC_DemoEnvironment-tagged actors would cover just the sun and sky and
    // miss the nine point lights that actually light the cave interiors -- those are tracked
    // in the showcase's own spawn list, not tagged. So compare against the lights that
    // existed before the showcase spawned and check everything it added.
    {
        int32 Lights = 0;
        int32 NonMovable = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (!IsValid(*It) || LightsBeforeShowcase.Contains(*It))
            {
                continue;
            }
            TArray<ULightComponentBase*> LightComps;
            It->GetComponents<ULightComponentBase>(LightComps);
            for (const ULightComponentBase* LC : LightComps)
            {
                ++Lights;
                if (LC->Mobility != EComponentMobility::Movable)
                {
                    ++NonMovable;
                    AddError(FString::Printf(
                        TEXT("Showcase light on '%s' is %s, not Movable. Lumen global illumination ")
                        TEXT("only considers Movable lights, so the demo renders near-black."),
                        *It->GetActorLabel(),
                        LC->Mobility == EComponentMobility::Static ? TEXT("Static") : TEXT("Stationary")));
                }
            }
        }
        if (Lights == 0)
        {
            AddError(TEXT("The showcase spawned no lights at all; the demo would be unlit."));
        }
        AddInfo(FString::Printf(TEXT("Showcase lights: %d spawned, %d not Movable."), Lights, NonMovable));
    }

    // --- ClearShowcase clears the showcase ---
    FInstantOrganicCavesModule::ClearShowcase(World);
    const int32 AfterClear = IOCCountActorsLabelled(World, TEXT("IOC_Showcase_"));
    if (AfterClear != 0)
    {
        AddError(FString::Printf(
            TEXT("ClearShowcase left %d IOC_Showcase_ actor(s) behind."), AfterClear));
    }

    // --- the two demos must share one character, not spawn one each ---
    FInstantOrganicCavesModule::SpawnTunnelDemo(World);
    FInstantOrganicCavesModule::SpawnSpectacularDemo(World);
    const int32 Players = IOCCountActorsLabelled(World, TEXT("IOC_Player"));
    if (Players != 1)
    {
        AddError(FString::Printf(
            TEXT("Running the tunnel and spectacular demos produced %d IOC_Player actor(s); ")
            TEXT("expected exactly 1. Two pawns make PIE possession ambiguous."), Players));
    }

    // --- and ClearAllDemos must leave the level as it was found ---
    FInstantOrganicCavesModule::ClearAllDemos(World);
    const int32 Residue = IOCCountActorsLabelled(World, TEXT("IOC_"));
    if (Residue != 0)
    {
        AddError(FString::Printf(
            TEXT("ClearAllDemos left %d IOC_ actor(s) in the level; the demos must not litter ")
            TEXT("a user's level."), Residue));
    }

    AddInfo(FString::Printf(
        TEXT("Showcase %d actors (idempotent on re-run), %d demo character(s), %d residue after cleanup."),
        AfterFirst, Players, Residue));

    return !HasAnyErrors();
#else
    return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCSpawnTunnelTest, "InstantOrganicCaves.Demo.SpawnTunnel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCSpawnTunnelTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    // 1. Run the Console Command
    // We assume GEditor is available since we are in EditorContext
    if (!GEditor) return false;

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        AddError(TEXT("No Editor World found."));
        return false;
    }

    // Clean up any existing demo actors to ensure fresh test
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetActorLabel() == TEXT("IOC_Tunnel_Demo") || It->GetActorLabel() == TEXT("IOC_Player"))
        {
            World->DestroyActor(*It);
        }
    }

    // Execute Command
    GEngine->Exec(World, TEXT("IOC.SpawnTunnelDemo"));

    // 2. Verify Actor Exists
    AIOCProceduralActor* DemoActor = nullptr;
    for (TActorIterator<AIOCProceduralActor> It(World); It; ++It)
    {
        if (It->GetActorLabel() == TEXT("IOC_Tunnel_Demo"))
        {
            DemoActor = *It;
            break;
        }
    }

    if (!DemoActor)
    {
        AddError(TEXT("IOC_Tunnel_Demo actor was not spawned."));
        return false;
    }

    // 3. Verify Properties
    if (!DemoActor->bGenerateTunnel)
    {
        AddError(TEXT("bGenerateTunnel should be true."));
        return false;
    }

    if (!FMath::IsNearlyEqual(DemoActor->TunnelRadius, 450.0f))
    {
        AddError(TEXT("TunnelRadius was not set correctly (expected 450)."));
        return false;
    }

    // 4. Verify Mesh Generation (Async Note)
    AddInfo(TEXT("Mesh generation is Async. Skipping immediate triangle check to avoid race conditions in simple test."));

    // 5. Verify Player Character
    AIOCCharacter* DemoPlayer = nullptr;
    for (TActorIterator<AIOCCharacter> It(World); It; ++It)
    {
        if (It->GetActorLabel() == TEXT("IOC_Player"))
        {
            DemoPlayer = *It;
            break;
        }
    }

    if (!DemoPlayer)
    {
        AddError(TEXT("IOC_Player character was not spawned."));
        return false;
    }

    AddInfo(TEXT("Successfully spawned Tunnel Demo and Player Character (Mesh generation is Async)."));

    return true;
#else
    return true; // Skip in Game builds
#endif
}

// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCProductionContractTest, "InstantOrganicCaves.Production.RuntimeContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCProductionContractTest::RunTest(const FString& Parameters)
{
    const AIOCProceduralActor* CaveDefaults = GetDefault<AIOCProceduralActor>();
    TestNotNull(TEXT("Cave class default object exists"), CaveDefaults);
    if (!CaveDefaults)
    {
        return false;
    }

    TestTrue(TEXT("Caves replicate by default"), CaveDefaults->GetIsReplicated());
    TestTrue(TEXT("Voxel budget is enabled"), CaveDefaults->MaxVoxelCount > 0);
    TestTrue(TEXT("Triangle budget is enabled"), CaveDefaults->MaxGeneratedTriangles > 0);
    TestTrue(TEXT("Scatter budget is enabled"), CaveDefaults->MaxScatterInstances > 0);
    TestTrue(TEXT("Runtime carve history is bounded"), CaveDefaults->MaxRuntimeCarves > 0);
    TestTrue(TEXT("Runtime carve radius range is valid"),
        CaveDefaults->MinRuntimeCarveRadius > 0.0f &&
        CaveDefaults->MaxRuntimeCarveRadius >= CaveDefaults->MinRuntimeCarveRadius);

    const FProperty* RuntimeCarvesProperty = FindFProperty<FProperty>(
        AIOCProceduralActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, RuntimeCarves));
    TestNotNull(TEXT("Runtime carve history is reflected"), RuntimeCarvesProperty);
    if (RuntimeCarvesProperty)
    {
        TestTrue(TEXT("Runtime carve history is replicated"), RuntimeCarvesProperty->HasAnyPropertyFlags(CPF_Net));
        TestTrue(TEXT("Runtime carve history is save-game serializable"), RuntimeCarvesProperty->HasAnyPropertyFlags(CPF_SaveGame));

        // Omitting TStructOpsTypeTraits<FIOCCarveHistory>::WithNetDeltaSerializer still
        // compiles and still replicates -- it just silently degrades to resending the whole
        // array, which is the entire thing the FastArray exists to prevent. This is the only
        // check that can tell the two apart.
        const FStructProperty* CarveHistoryProperty = CastField<FStructProperty>(RuntimeCarvesProperty);
        TestNotNull(TEXT("Runtime carve history is a struct property"), CarveHistoryProperty);
        if (CarveHistoryProperty && CarveHistoryProperty->Struct)
        {
            TestTrue(
                TEXT("Runtime carve history replicates as a net delta, not a whole array"),
                (CarveHistoryProperty->Struct->StructFlags & STRUCT_NetDeltaSerializeNative) != 0);
        }
    }

    const FProperty* PresetProperty = FindFProperty<FProperty>(
        AIOCProceduralActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, CavePreset));
    TestTrue(TEXT("Generation settings replicate"),
        PresetProperty && PresetProperty->HasAnyPropertyFlags(CPF_Net));

    const AIOCStreamingManager* StreamingDefaults = GetDefault<AIOCStreamingManager>();
    TestNotNull(TEXT("Streaming manager class default object exists"), StreamingDefaults);
    if (StreamingDefaults)
    {
        TestTrue(TEXT("Streaming manager replicates by default"), StreamingDefaults->GetIsReplicated());
        TestFalse(TEXT("Player coupling is opt-in"), StreamingDefaults->bAutoCouplePlayerAtStart);
        TestTrue(TEXT("Streaming has a loaded-chunk cap"), StreamingDefaults->MaxLoadedChunks > 0);
        TestTrue(TEXT("Streaming load work is throttled"),
            StreamingDefaults->MaxChunkLoadsPerUpdate > 0 &&
            StreamingDefaults->MaxChunkLoadsPerUpdate <= StreamingDefaults->MaxLoadedChunks);
        TestTrue(TEXT("Per-chunk voxel budget is enabled"), StreamingDefaults->MaxVoxelCountPerChunk > 0);
        TestTrue(TEXT("Per-chunk triangle budget is enabled"), StreamingDefaults->MaxTrianglesPerChunk > 0);
        TestTrue(TEXT("Per-chunk scatter budget is enabled"), StreamingDefaults->MaxScatterInstancesPerChunk > 0);
        TestTrue(TEXT("Streamed carve history is bounded"), StreamingDefaults->MaxStreamedRuntimeCarves > 0);

        const FProperty* StreamedCarvesProperty = FindFProperty<FProperty>(
            AIOCStreamingManager::StaticClass(),
            GET_MEMBER_NAME_CHECKED(AIOCStreamingManager, StreamedRuntimeCarves));
        TestTrue(TEXT("Streamed carve history is save-game serializable"),
            StreamedCarvesProperty && StreamedCarvesProperty->HasAnyPropertyFlags(CPF_SaveGame));

        const FVector EffectiveChunkSize = StreamingDefaults->GetEffectiveChunkSize();
        const double PrimaryVoxelSize = FMath::Max(10.0, StreamingDefaults->VoxelSize);
        const double StreamingLODMultiplier = StreamingDefaults->bEnableLOD
            ? (double)FMath::Clamp(FMath::RoundToInt(StreamingDefaults->LODVoxelSizeMultiplier), 1, 16)
            : 1.0;
        TestTrue(TEXT("Effective X chunk size aligns to the primary and LOD voxel grids"),
            FMath::IsNearlyZero(FMath::Fmod(EffectiveChunkSize.X, PrimaryVoxelSize * StreamingLODMultiplier), 0.01));
        TestTrue(TEXT("Effective Y chunk size aligns to the primary and LOD voxel grids"),
            FMath::IsNearlyZero(FMath::Fmod(EffectiveChunkSize.Y, PrimaryVoxelSize * StreamingLODMultiplier), 0.01));
        TestTrue(TEXT("Effective Z chunk size aligns to the primary and LOD voxel grids"),
            FMath::IsNearlyZero(FMath::Fmod(EffectiveChunkSize.Z, PrimaryVoxelSize * StreamingLODMultiplier), 0.01));
    }

    return !HasAnyErrors();
}

#if WITH_EDITOR
namespace
{
    void IOCCollectInterfaceCells(
        const AIOCProceduralActor* Cave,
        double InterfaceWorldX,
        double VoxelSize,
        TSet<FIntPoint>& OutCells)
    {
        if (!Cave || !Cave->MeshComponent || !Cave->MeshComponent->GetDynamicMesh())
        {
            return;
        }

        const FTransform ActorTransform = Cave->GetActorTransform();
        Cave->MeshComponent->GetDynamicMesh()->ProcessMesh(
            [&](const UE::Geometry::FDynamicMesh3& Mesh)
            {
                for (int32 TriangleId : Mesh.TriangleIndicesItr())
                {
                    FVector3d LocalA;
                    FVector3d LocalB;
                    FVector3d LocalC;
                    Mesh.GetTriVertices(TriangleId, LocalA, LocalB, LocalC);

                    const FVector WorldA = ActorTransform.TransformPosition((FVector)LocalA);
                    const FVector WorldB = ActorTransform.TransformPosition((FVector)LocalB);
                    const FVector WorldC = ActorTransform.TransformPosition((FVector)LocalC);
                    if (!FMath::IsNearlyEqual(WorldA.X, InterfaceWorldX, 0.1) ||
                        !FMath::IsNearlyEqual(WorldB.X, InterfaceWorldX, 0.1) ||
                        !FMath::IsNearlyEqual(WorldC.X, InterfaceWorldX, 0.1))
                    {
                        continue;
                    }

                    const double MinY = FMath::Min3((double)WorldA.Y, (double)WorldB.Y, (double)WorldC.Y);
                    const double MinZ = FMath::Min3((double)WorldA.Z, (double)WorldB.Z, (double)WorldC.Z);
                    OutCells.Add(FIntPoint(
                        FMath::RoundToInt(MinY / VoxelSize),
                        FMath::RoundToInt(MinZ / VoxelSize)));
                }
            });
    }

    int32 IOCGetGeneratedTriangleCount(const AIOCProceduralActor* Cave)
    {
        int32 TriangleCount = 0;
        if (Cave && Cave->MeshComponent && Cave->MeshComponent->GetDynamicMesh())
        {
            Cave->MeshComponent->GetDynamicMesh()->ProcessMesh(
                [&TriangleCount](const UE::Geometry::FDynamicMesh3& Mesh)
                {
                    TriangleCount = Mesh.TriangleCount();
                });
        }
        return TriangleCount;
    }

    int32 IOCGetGeneratedVertexCount(const AIOCProceduralActor* Cave)
    {
        int32 VertexCount = 0;
        if (Cave && Cave->MeshComponent && Cave->MeshComponent->GetDynamicMesh())
        {
            Cave->MeshComponent->GetDynamicMesh()->ProcessMesh(
                [&VertexCount](const UE::Geometry::FDynamicMesh3& Mesh)
                {
                    VertexCount = Mesh.VertexCount();
                });
        }
        return VertexCount;
    }
}

DEFINE_LATENT_AUTOMATION_COMMAND_FOUR_PARAMETER(
    FIOCWaitForStreamingSeamCommand,
    TWeakObjectPtr<AIOCProceduralActor>, LeftCave,
    TWeakObjectPtr<AIOCProceduralActor>, RightCave,
    FAutomationTestBase*, Test,
    double, DeadlineSeconds);

bool FIOCWaitForStreamingSeamCommand::Update()
{
    if (!LeftCave.IsValid() || !RightCave.IsValid())
    {
        Test->AddError(TEXT("A seam-test cave was destroyed before generation completed."));
        return true;
    }

    AIOCProceduralActor* Left = LeftCave.Get();
    AIOCProceduralActor* Right = RightCave.Get();
    if (Left->bIsGeneratingDisplay || Right->bIsGeneratingDisplay)
    {
        if (FPlatformTime::Seconds() < DeadlineSeconds)
        {
            return false;
        }

        Test->AddError(TEXT("Timed out waiting for streamed seam-test caves to generate."));
        if (UWorld* World = Left->GetWorld())
        {
            World->DestroyActor(Left);
            World->DestroyActor(Right);
        }
        return true;
    }

    Test->TestTrue(TEXT("Left streamed density chunk generated"), Left->bLastGenerationSucceeded);
    Test->TestTrue(TEXT("Right streamed density chunk generated"), Right->bLastGenerationSucceeded);
    Test->TestTrue(TEXT("Left chunk contains geometry"), Left->LastPrimaryTriangleCount > 0);
    Test->TestTrue(TEXT("Right chunk contains geometry"), Right->LastPrimaryTriangleCount > 0);

    const double InterfaceWorldX = (Left->GetActorLocation().X + Right->GetActorLocation().X) * 0.5;
    TSet<FIntPoint> LeftInterfaceCells;
    TSet<FIntPoint> RightInterfaceCells;
    IOCCollectInterfaceCells(Left, InterfaceWorldX, Left->VoxelSize, LeftInterfaceCells);
    IOCCollectInterfaceCells(Right, InterfaceWorldX, Right->VoxelSize, RightInterfaceCells);

    int32 DuplicateCapCells = 0;
    for (const FIntPoint& Cell : LeftInterfaceCells)
    {
        DuplicateCapCells += RightInterfaceCells.Contains(Cell) ? 1 : 0;
    }

    Test->TestTrue(TEXT("The deterministic test field crosses the chunk interface"),
        LeftInterfaceCells.Num() + RightInterfaceCells.Num() > 0);
    Test->TestEqual(TEXT("Neighbor halo prevents duplicate boundary caps"), DuplicateCapCells, 0);

    if (UWorld* World = Left->GetWorld())
    {
        World->DestroyActor(Left);
        World->DestroyActor(Right);
    }
    return true;
}

class FIOCBudgetPreservationCommand final : public IAutomationLatentCommand
{
public:
    FIOCBudgetPreservationCommand(
        AIOCProceduralActor* InCave,
        FAutomationTestBase* InTest,
        double InDeadlineSeconds)
        : Cave(InCave)
        , Test(InTest)
        , DeadlineSeconds(InDeadlineSeconds)
    {
    }

    virtual bool Update() override
    {
        if (!Cave.IsValid())
        {
            Test->AddError(TEXT("Budget-test cave was destroyed before the test completed."));
            return true;
        }

        AIOCProceduralActor* CaveActor = Cave.Get();
        if (CaveActor->bIsGeneratingDisplay)
        {
            if (FPlatformTime::Seconds() < DeadlineSeconds)
            {
                return false;
            }

            Test->AddError(TEXT("Timed out waiting for budget-test cave generation."));
            DestroyCave(CaveActor);
            return true;
        }

        if (Phase == EPhase::WaitForBaseline)
        {
            Test->TestTrue(TEXT("Budget test baseline generation succeeded"), CaveActor->bLastGenerationSucceeded);
            BaselineTriangleCount = IOCGetGeneratedTriangleCount(CaveActor);
            Test->TestTrue(TEXT("Budget test baseline mesh contains geometry"), BaselineTriangleCount > 0);
            if (!CaveActor->bLastGenerationSucceeded || BaselineTriangleCount <= 0)
            {
                DestroyCave(CaveActor);
                return true;
            }

            CaveActor->bUseWorldSpaceNoise = false;
            CaveActor->GenerationBounds = FVector(10000.0);
            CaveActor->VoxelSize = 10.0;
            CaveActor->MaxVoxelCount = 1000;
            CaveActor->MaxGeneratedTriangles = 100000;
            Test->AddExpectedError(TEXT("Cave generation failed"), EAutomationExpectedErrorFlags::Contains, 1);
            CaveActor->GenerateCave();
            Phase = EPhase::WaitForBudgetRejection;
            DeadlineSeconds = FPlatformTime::Seconds() + 30.0;
            return false;
        }

        Test->TestFalse(TEXT("Over-budget generation is rejected"), CaveActor->bLastGenerationSucceeded);
        Test->TestTrue(TEXT("Over-budget generation reports a reason"), !CaveActor->LastGenerationError.IsEmpty());
        Test->TestEqual(
            TEXT("A rejected generation preserves the last valid mesh"),
            IOCGetGeneratedTriangleCount(CaveActor),
            BaselineTriangleCount);
        DestroyCave(CaveActor);
        return true;
    }

private:
    enum class EPhase : uint8
    {
        WaitForBaseline,
        WaitForBudgetRejection
    };

    static void DestroyCave(AIOCProceduralActor* CaveActor)
    {
        if (CaveActor && CaveActor->GetWorld())
        {
            CaveActor->GetWorld()->DestroyActor(CaveActor);
        }
    }

    TWeakObjectPtr<AIOCProceduralActor> Cave;
    FAutomationTestBase* Test = nullptr;
    double DeadlineSeconds = 0.0;
    int32 BaselineTriangleCount = 0;
    EPhase Phase = EPhase::WaitForBaseline;
};
#endif

// -----------------------------------------------------------------------
// Vertex welding must not cost triangles.
//
// The generator used to emit four unshared vertices per quad specifically because
// FDynamicMesh3 rejects triangles that would create a non-manifold edge -- something
// diagonal-touching voxels produce constantly. Welding shares a vertex per grid corner and
// duplicates only the triangles that actually fail, so the invariant to prove is: identical
// triangle count, strictly fewer vertices.
//
// Unwelded is exactly 4 vertices per quad, i.e. 2 vertices per triangle, so "fewer vertices
// than triangles" is a wide margin that only a genuinely welded mesh can hit.
#if WITH_EDITOR
DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
    FIOCWaitForWeldComparisonCommand,
    TWeakObjectPtr<AIOCProceduralActor>, Cave,
    FAutomationTestBase*, Test,
    double, DeadlineSeconds);

namespace
{
    int32 GIOCUnweldedTriangles = 0;
    int32 GIOCUnweldedVertices = 0;
    bool GIOCWeldPhaseTwo = false;
    bool GIOCSavedWeldSetting = true;
}

bool FIOCWaitForWeldComparisonCommand::Update()
{
    AIOCProceduralActor* CaveActor = Cave.Get();
    if (!CaveActor)
    {
        Test->AddError(TEXT("Weld comparison cave was destroyed before generation finished."));
        GetMutableDefault<UIOCSettings>()->bWeldGeneratedVertices = GIOCSavedWeldSetting;
        return true;
    }

    const bool bTimedOut = FPlatformTime::Seconds() >= DeadlineSeconds;
    if (CaveActor->bIsGeneratingDisplay && !bTimedOut)
    {
        return false;
    }

    if (bTimedOut && CaveActor->bIsGeneratingDisplay)
    {
        Test->AddError(TEXT("Weld comparison timed out waiting for generation."));
        GetMutableDefault<UIOCSettings>()->bWeldGeneratedVertices = GIOCSavedWeldSetting;
        CaveActor->GetWorld()->DestroyActor(CaveActor);
        return true;
    }

    if (!GIOCWeldPhaseTwo)
    {
        GIOCUnweldedTriangles = IOCGetGeneratedTriangleCount(CaveActor);
        GIOCUnweldedVertices = IOCGetGeneratedVertexCount(CaveActor);

        Test->TestTrue(TEXT("Unwelded baseline produced geometry"), GIOCUnweldedTriangles > 0);
        if (GIOCUnweldedTriangles <= 0)
        {
            GetMutableDefault<UIOCSettings>()->bWeldGeneratedVertices = GIOCSavedWeldSetting;
            CaveActor->GetWorld()->DestroyActor(CaveActor);
            return true;
        }

        GetMutableDefault<UIOCSettings>()->bWeldGeneratedVertices = true;
        GIOCWeldPhaseTwo = true;
        CaveActor->GenerateCave();
        DeadlineSeconds = FPlatformTime::Seconds() + 30.0;
        return false;
    }

    const int32 WeldedTriangles = IOCGetGeneratedTriangleCount(CaveActor);
    const int32 WeldedVertices = IOCGetGeneratedVertexCount(CaveActor);

    Test->TestEqual(
        TEXT("Welding preserves every triangle"),
        WeldedTriangles, GIOCUnweldedTriangles);

    // Logged unconditionally so a CI run shows the actual saving, not just pass/fail.
    Test->AddInfo(FString::Printf(
        TEXT("Vertices: %d welded vs %d unwelded for %d triangles (%.2fx reduction)."),
        WeldedVertices, GIOCUnweldedVertices, WeldedTriangles,
        WeldedVertices > 0 ? (double)GIOCUnweldedVertices / (double)WeldedVertices : 0.0));

    Test->TestTrue(
        FString::Printf(
            TEXT("Welding shares vertices (%d welded vs %d unwelded, for %d triangles)"),
            WeldedVertices, GIOCUnweldedVertices, WeldedTriangles),
        WeldedVertices < WeldedTriangles);

    GetMutableDefault<UIOCSettings>()->bWeldGeneratedVertices = GIOCSavedWeldSetting;
    CaveActor->GetWorld()->DestroyActor(CaveActor);
    return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCVertexWeldTest, "InstantOrganicCaves.Cave.VertexWelding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCVertexWeldTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        AddError(TEXT("No editor world available."));
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AIOCProceduralActor* Cave = World->SpawnActor<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(), FVector(140000.0, 0.0, 0.0), FRotator::ZeroRotator, SpawnParams);
    if (!Cave)
    {
        AddError(TEXT("Failed to spawn weld comparison cave."));
        return false;
    }

    Cave->CavePreset = EIOCCavePreset::Custom;
    Cave->bGenerateTunnel = false;
    Cave->bUseWorldSpaceNoise = false;
    Cave->GenerationBounds = FVector(1200.0);
    Cave->VoxelSize = 100.0;
    Cave->SmoothIterations = 0;
    Cave->bEnableLOD = false;
    Cave->bShowDebugViz = false;
    Cave->MaxVoxelCount = 500000;
    Cave->MaxGeneratedTriangles = 500000;
    Cave->MaxScatterInstances = 0;

    UIOCSettings* Settings = GetMutableDefault<UIOCSettings>();
    GIOCSavedWeldSetting = Settings->bWeldGeneratedVertices;
    GIOCWeldPhaseTwo = false;
    Settings->bWeldGeneratedVertices = false;   // phase one: baseline

    Cave->GenerateCave();
    ADD_LATENT_AUTOMATION_COMMAND(FIOCWaitForWeldComparisonCommand(
        Cave, this, FPlatformTime::Seconds() + 30.0));
    return true;
#else
    return true;
#endif
}

// -----------------------------------------------------------------------
// Replaying a cached voxel field must be indistinguishable from recomputing it.
//
// The carve fast path skips the noise fill and replays a cached pre-carve field. That is only
// sound if the cache key covers every input to the fill -- a missing input would serve a field
// that no longer matches the parameters, and the failure would be silent and content-dependent.
//
// Three things are checked:
//   1. Parity        cache off vs cache on, same parameters -> identical geometry.
//   2. Hit parity    two consecutive cached generations     -> identical geometry.
//   3. Invalidation  change the seed with the cache warm    -> matches an uncached run of the
//                    new seed, proving the signature moved rather than serving the old field.
//
// Timings are logged, not asserted: wall-clock on a thread pool is too noisy to gate CI on.
#if WITH_EDITOR
DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
    FIOCWaitForCarveCacheCommand,
    TWeakObjectPtr<AIOCProceduralActor>, Cave,
    FAutomationTestBase*, Test,
    double, DeadlineSeconds);

namespace
{
    enum class EIOCCarveCachePhase : uint8
    {
        UncachedSeedA,
        CachedColdSeedA,
        CachedWarmSeedA,
        UncachedSeedB,
        CachedSeedB,
        Done
    };

    EIOCCarveCachePhase GIOCCarvePhase = EIOCCarveCachePhase::UncachedSeedA;
    int32 GIOCTrisUncachedA = 0;
    int32 GIOCTrisUncachedB = 0;
    double GIOCTimeUncachedA = 0.0;
    double GIOCTimeCachedCold = 0.0;
    double GIOCTimeCachedWarm = 0.0;
    bool GIOCSavedCacheSetting = true;

    constexpr int32 IOCCarveTestSeedA = 20260904;
    constexpr int32 IOCCarveTestSeedB = 771144;

    void IOCRestoreCacheSetting()
    {
        GetMutableDefault<UIOCSettings>()->bCacheVoxelFieldForCarving = GIOCSavedCacheSetting;
    }
}

bool FIOCWaitForCarveCacheCommand::Update()
{
    AIOCProceduralActor* CaveActor = Cave.Get();
    if (!CaveActor)
    {
        Test->AddError(TEXT("Carve cache test cave was destroyed before generation finished."));
        IOCRestoreCacheSetting();
        return true;
    }

    const bool bTimedOut = FPlatformTime::Seconds() >= DeadlineSeconds;
    if (CaveActor->bIsGeneratingDisplay && !bTimedOut)
    {
        return false;
    }

    if (bTimedOut && CaveActor->bIsGeneratingDisplay)
    {
        Test->AddError(TEXT("Carve cache test timed out waiting for generation."));
        IOCRestoreCacheSetting();
        CaveActor->GetWorld()->DestroyActor(CaveActor);
        return true;
    }

    UIOCSettings* Settings = GetMutableDefault<UIOCSettings>();
    const int32 Tris = IOCGetGeneratedTriangleCount(CaveActor);

    auto Advance = [&](EIOCCarveCachePhase Next, bool bCacheOn, int32 Seed)
    {
        GIOCCarvePhase = Next;
        Settings->bCacheVoxelFieldForCarving = bCacheOn;
        CaveActor->CaveSeed = Seed;
        CaveActor->GenerateCave();
        DeadlineSeconds = FPlatformTime::Seconds() + 60.0;
    };

    switch (GIOCCarvePhase)
    {
    case EIOCCarveCachePhase::UncachedSeedA:
        GIOCTrisUncachedA = Tris;
        GIOCTimeUncachedA = CaveActor->LastGenerationTimeSeconds;
        Test->TestTrue(TEXT("Carve cache baseline produced geometry"), GIOCTrisUncachedA > 0);
        if (GIOCTrisUncachedA <= 0)
        {
            IOCRestoreCacheSetting();
            CaveActor->GetWorld()->DestroyActor(CaveActor);
            return true;
        }
        Advance(EIOCCarveCachePhase::CachedColdSeedA, true, IOCCarveTestSeedA);
        return false;

    case EIOCCarveCachePhase::CachedColdSeedA:
        GIOCTimeCachedCold = CaveActor->LastGenerationTimeSeconds;
        Test->TestEqual(
            TEXT("Cache-enabled cold generation matches the uncached result"),
            Tris, GIOCTrisUncachedA);
        Advance(EIOCCarveCachePhase::CachedWarmSeedA, true, IOCCarveTestSeedA);
        return false;

    case EIOCCarveCachePhase::CachedWarmSeedA:
        GIOCTimeCachedWarm = CaveActor->LastGenerationTimeSeconds;
        Test->TestEqual(
            TEXT("Replayed field produces identical geometry"),
            Tris, GIOCTrisUncachedA);
        Advance(EIOCCarveCachePhase::UncachedSeedB, false, IOCCarveTestSeedB);
        return false;

    case EIOCCarveCachePhase::UncachedSeedB:
        GIOCTrisUncachedB = Tris;
        Test->TestNotEqual(
            TEXT("Changing the seed changes the geometry (otherwise the test proves nothing)"),
            GIOCTrisUncachedB, GIOCTrisUncachedA);
        // Cache is warm with seed A's field. Turning it back on with seed B must miss.
        Advance(EIOCCarveCachePhase::CachedSeedB, true, IOCCarveTestSeedB);
        return false;

    case EIOCCarveCachePhase::CachedSeedB:
    default:
        Test->TestEqual(
            TEXT("A changed seed invalidates the cached field instead of replaying it"),
            Tris, GIOCTrisUncachedB);

        Test->AddInfo(FString::Printf(
            TEXT("Generation time: %.0f ms uncached, %.0f ms cache-cold, %.0f ms cache-hit (%.2fx faster than uncached)."),
            GIOCTimeUncachedA * 1000.0,
            GIOCTimeCachedCold * 1000.0,
            GIOCTimeCachedWarm * 1000.0,
            GIOCTimeCachedWarm > 0.0 ? GIOCTimeUncachedA / GIOCTimeCachedWarm : 0.0));

        IOCRestoreCacheSetting();
        CaveActor->GetWorld()->DestroyActor(CaveActor);
        return true;
    }
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCCarveFieldCacheTest, "InstantOrganicCaves.Cave.CarveFieldCacheParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCCarveFieldCacheTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        AddError(TEXT("No editor world available."));
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AIOCProceduralActor* Cave = World->SpawnActor<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(), FVector(160000.0, 0.0, 0.0), FRotator::ZeroRotator, SpawnParams);
    if (!Cave)
    {
        AddError(TEXT("Failed to spawn carve cache test cave."));
        return false;
    }

    Cave->CavePreset = EIOCCavePreset::Custom;
    Cave->bGenerateTunnel = false;
    Cave->bUseWorldSpaceNoise = false;
    Cave->GenerationBounds = FVector(3200.0);
    Cave->VoxelSize = 40.0;
    Cave->NoiseThreshold = 0.5f;
    Cave->SmoothIterations = 1;
    Cave->bEnableLOD = false;      // one cache entry, one timing path
    Cave->bShowDebugViz = false;
    Cave->bLogPresetDebug = true;   // stage timings
    Cave->CaveSeed = IOCCarveTestSeedA;
    Cave->MaxVoxelCount = 4000000;
    Cave->MaxGeneratedTriangles = 2000000;
    Cave->MaxScatterInstances = 0;

    // A carving component is what makes the actor eligible for a cached field at all.
    UIOCCarvingComponent* Carve = NewObject<UIOCCarvingComponent>(Cave);
    Carve->RegisterComponent();
    Carve->AttachToComponent(Cave->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
    Carve->SetRelativeLocation(FVector(200.0, 0.0, 0.0));
    Carve->UpdateComponentToWorld();
    Carve->ShapeType = EIOCCarvingShape::Sphere;
    Carve->SphereRadius = 500.0f;
    Carve->FalloffRadius = 120.0f;

    UIOCSettings* Settings = GetMutableDefault<UIOCSettings>();
    GIOCSavedCacheSetting = Settings->bCacheVoxelFieldForCarving;
    Settings->bCacheVoxelFieldForCarving = false;   // phase 0: uncached baseline
    GIOCCarvePhase = EIOCCarveCachePhase::UncachedSeedA;

    Cave->GenerateCave();
    ADD_LATENT_AUTOMATION_COMMAND(FIOCWaitForCarveCacheCommand(
        Cave, this, FPlatformTime::Seconds() + 60.0));
    return true;
#else
    return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCStreamingSeamTest, "InstantOrganicCaves.Production.StreamingSeam",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCStreamingSeamTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor)
    {
        AddError(TEXT("No editor available."));
        return false;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        AddError(TEXT("No editor world available."));
        return false;
    }

    constexpr double ChunkExtent = 1600.0;
    auto SpawnChunk = [World](const FVector& Location)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AIOCProceduralActor* Cave = World->SpawnActor<AIOCProceduralActor>(
            AIOCProceduralActor::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        if (Cave)
        {
            Cave->CavePreset = EIOCCavePreset::Custom;
            Cave->bGenerateTunnel = false;
            Cave->bUseWorldSpaceNoise = true;
            Cave->GenerationBounds = FVector(ChunkExtent);
            Cave->VoxelSize = 100.0;
            Cave->NoiseThreshold = 0.5f;
            Cave->SmoothIterations = 2;
            Cave->bEnableLOD = false;
            Cave->bShowDebugViz = false;
            Cave->MaxVoxelCount = 100000;
            Cave->MaxGeneratedTriangles = 100000;
            Cave->MaxScatterInstances = 0;
        }
        return Cave;
    };

    AIOCProceduralActor* Left = SpawnChunk(FVector::ZeroVector);
    AIOCProceduralActor* Right = SpawnChunk(FVector(ChunkExtent, 0.0, 0.0));
    if (!Left || !Right)
    {
        AddError(TEXT("Failed to spawn streamed seam-test caves."));
        if (Left) World->DestroyActor(Left);
        if (Right) World->DestroyActor(Right);
        return false;
    }

    Left->GenerateCave();
    Right->GenerateCave();
    ADD_LATENT_AUTOMATION_COMMAND(FIOCWaitForStreamingSeamCommand(
        Left, Right, this, FPlatformTime::Seconds() + 30.0));
    return true;
#else
    return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCGenerationBudgetTest, "InstantOrganicCaves.Production.GenerationBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCGenerationBudgetTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor)
    {
        AddError(TEXT("No editor available."));
        return false;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        AddError(TEXT("No editor world available."));
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AIOCProceduralActor* Cave = World->SpawnActor<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(), FVector(50000.0, 0.0, 0.0), FRotator::ZeroRotator, SpawnParams);
    if (!Cave)
    {
        AddError(TEXT("Failed to spawn generation-budget cave."));
        return false;
    }

    Cave->CavePreset = EIOCCavePreset::Custom;
    Cave->bGenerateTunnel = false;
    Cave->bUseWorldSpaceNoise = true;
    Cave->GenerationBounds = FVector(1600.0);
    Cave->VoxelSize = 100.0;
    Cave->NoiseThreshold = 0.5f;
    Cave->SmoothIterations = 1;
    Cave->bEnableLOD = false;
    Cave->bShowDebugViz = false;
    Cave->MaxVoxelCount = 100000;
    Cave->MaxGeneratedTriangles = 100000;
    Cave->MaxScatterInstances = 0;
    Cave->GenerateCave();

    ADD_LATENT_AUTOMATION_COMMAND(FIOCBudgetPreservationCommand(
        Cave, this, FPlatformTime::Seconds() + 30.0));
    return true;
#else
    return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FIOCStreamingCarvePersistenceTest,
    "InstantOrganicCaves.Production.StreamingCarvePersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCStreamingCarvePersistenceTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor)
    {
        AddError(TEXT("No editor available."));
        return false;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        AddError(TEXT("No editor world available."));
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector ManagerLocation(100000.0, 100000.0, 0.0);
    AIOCStreamingManager* Manager = World->SpawnActor<AIOCStreamingManager>(
        AIOCStreamingManager::StaticClass(), ManagerLocation, FRotator::ZeroRotator, SpawnParams);
    if (!Manager)
    {
        AddError(TEXT("Failed to spawn streaming manager."));
        return false;
    }

    Manager->ChunkSize = FVector(600.0);
    Manager->VoxelSize = 100.0;
    Manager->bEnableLOD = false;
    Manager->StreamRadius = 1;
    Manager->MaxLoadedChunks = 1;
    Manager->MaxChunkLoadsPerUpdate = 4;
    Manager->MaxVoxelCountPerChunk = 100000;
    Manager->MaxTrianglesPerChunk = 100000;
    Manager->MaxScatterInstancesPerChunk = 0;
    Manager->MaxStreamedRuntimeCarves = 2;
    Manager->bGenerateTunnel = false;

    Manager->CarveStreamedCavesAtLocation(ManagerLocation, 120.0f);
    TestEqual(TEXT("Manager records an authoritative streamed carve"), Manager->StreamedRuntimeCarves.Num(), 1);
    Manager->RebuildAroundLocation(ManagerLocation);
    TestEqual(TEXT("Streaming manager loads the capped local chunk set"), Manager->GetLoadedChunkCount(), 1);

    auto FindOwnedChunk = [World, Manager]() -> AIOCProceduralActor*
    {
        for (TActorIterator<AIOCProceduralActor> It(World); It; ++It)
        {
            if (It->GetOwner() == Manager && !It->IsActorBeingDestroyed())
            {
                return *It;
            }
        }
        return nullptr;
    };

    AIOCProceduralActor* OriginalChunk = FindOwnedChunk();
    TestNotNull(TEXT("Original streamed chunk exists"), OriginalChunk);
    if (OriginalChunk)
    {
        TestEqual(TEXT("Original chunk receives manager carve history"), OriginalChunk->RuntimeCarves.Num(), 1);
    }

    const FVector FarLocation = ManagerLocation + FVector(5000.0, 0.0, 0.0);
    Manager->RebuildAroundLocation(FarLocation);
    AIOCProceduralActor* FarChunk = FindOwnedChunk();
    TestNotNull(TEXT("Far streamed chunk exists"), FarChunk);
    TestTrue(TEXT("Moving the stream replaces the original chunk"), FarChunk && FarChunk != OriginalChunk);
    if (FarChunk)
    {
        TestEqual(TEXT("Unaffected far chunk excludes the carve"), FarChunk->RuntimeCarves.Num(), 0);
    }

    Manager->RebuildAroundLocation(ManagerLocation);
    AIOCProceduralActor* ReloadedChunk = FindOwnedChunk();
    TestNotNull(TEXT("Original coordinate reloads"), ReloadedChunk);
    TestTrue(TEXT("Reload creates a new chunk actor"), ReloadedChunk && ReloadedChunk != OriginalChunk && ReloadedChunk != FarChunk);
    if (ReloadedChunk)
    {
        TestEqual(TEXT("Reloaded chunk restores persistent carve history"), ReloadedChunk->RuntimeCarves.Num(), 1);
    }

    Manager->CarveStreamedCavesAtLocation(ManagerLocation, 180.0f);
    Manager->CarveStreamedCavesAtLocation(ManagerLocation, 240.0f);
    TestEqual(TEXT("Streamed carve history obeys its FIFO cap"), Manager->StreamedRuntimeCarves.Num(), 2);

    Manager->ClearStreamedRuntimeCarves();
    TestEqual(TEXT("Clearing streamed carves clears manager history"), Manager->StreamedRuntimeCarves.Num(), 0);
    if (ReloadedChunk && !ReloadedChunk->IsActorBeingDestroyed())
    {
        TestEqual(TEXT("Clearing streamed carves clears loaded chunks"), ReloadedChunk->RuntimeCarves.Num(), 0);
    }

    World->DestroyActor(Manager);
    return !HasAnyErrors();
#else
    return true;
#endif
}

// -----------------------------------------------------------------------
// -----------------------------------------------------------------------
// Tunnel generation must follow the spline curve, not chord its control points.
// A two-point spline with large opposing tangents bulges well off the straight line
// between its endpoints; if generation only reads GetLocationAtSplinePoint the mesh
// collapses onto that line.
#if WITH_EDITOR
DEFINE_LATENT_AUTOMATION_COMMAND_FOUR_PARAMETER(
    FIOCWaitForSplineCurveCommand,
    TWeakObjectPtr<AIOCProceduralActor>, Cave,
    FAutomationTestBase*, Test,
    double, DeadlineSeconds,
    double, ExpectedMinLateralExtent);

bool FIOCWaitForSplineCurveCommand::Update()
{
    AIOCProceduralActor* CaveActor = Cave.Get();
    if (!CaveActor)
    {
        Test->AddError(TEXT("Spline curve test cave was destroyed before generation finished."));
        return true;
    }

    if (CaveActor->bIsGeneratingDisplay && FPlatformTime::Seconds() < DeadlineSeconds)
    {
        return false;
    }

    if (FPlatformTime::Seconds() >= DeadlineSeconds && CaveActor->bIsGeneratingDisplay)
    {
        Test->AddError(TEXT("Spline curve test timed out waiting for generation."));
        CaveActor->GetWorld()->DestroyActor(CaveActor);
        return true;
    }

    Test->TestTrue(TEXT("Curved spline tunnel generated a surface"), CaveActor->bLastGenerationSucceeded);

    if (CaveActor->bLastGenerationSucceeded && CaveActor->MeshComponent)
    {
        // The spline bows along +Y; a chorded (straight) tunnel would stay within roughly
        // TunnelRadius of Y=0, so a clearly larger Y extent proves the curve was followed.
        const FBox LocalBounds = CaveActor->MeshComponent->Bounds.GetBox().InverseTransformBy(
            CaveActor->GetActorTransform());
        const double LateralExtent = LocalBounds.Max.Y;

        Test->TestTrue(
            FString::Printf(
                TEXT("Tunnel follows the spline curve (lateral extent %.0f > %.0f)"),
                LateralExtent, ExpectedMinLateralExtent),
            LateralExtent > ExpectedMinLateralExtent);
    }

    CaveActor->GetWorld()->DestroyActor(CaveActor);
    return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCSplineCurveTest, "InstantOrganicCaves.Cave.SplineFollowsCurve",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCSplineCurveTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World)
    {
        AddError(TEXT("No editor world available."));
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AIOCProceduralActor* Cave = World->SpawnActor<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(), FVector(120000.0, 0.0, 0.0), FRotator::ZeroRotator, SpawnParams);
    if (!Cave)
    {
        AddError(TEXT("Failed to spawn spline curve test cave."));
        return false;
    }

    Cave->CavePreset = EIOCCavePreset::Custom;
    Cave->bGenerateTunnel = true;
    Cave->bUseSpline = true;
    Cave->bEnableLOD = false;
    Cave->bShowDebugViz = false;
    Cave->VoxelSize = 100.0;
    Cave->TunnelRadius = 250.0f;
    Cave->WallThickness = 100.0f;
    Cave->SmoothIterations = 0;
    Cave->MaxVoxelCount = 4000000;
    Cave->MaxGeneratedTriangles = 400000;
    Cave->MaxScatterInstances = 0;

    // Endpoints on the X axis, tangents pushed hard along +Y: the curve bows far off the
    // straight chord between the two points.
    USplineComponent* Spline = Cave->CaveSpline;
    if (!Spline)
    {
        AddError(TEXT("Spline curve test cave has no spline component."));
        World->DestroyActor(Cave);
        return false;
    }

    Spline->SetSplinePoints(
        { FVector(-2000.0, 0.0, 0.0), FVector(2000.0, 0.0, 0.0) },
        ESplineCoordinateSpace::Local,
        false);
    Spline->SetSplinePointType(0, ESplinePointType::Curve, false);
    Spline->SetSplinePointType(1, ESplinePointType::Curve, false);
    Spline->SetTangentAtSplinePoint(0, FVector(0.0, 6000.0, 0.0), ESplineCoordinateSpace::Local, false);
    Spline->SetTangentAtSplinePoint(1, FVector(0.0, 6000.0, 0.0), ESplineCoordinateSpace::Local, true);
    Spline->bSplineHasBeenEdited = true;

    Cave->GenerateCave();

    // A chorded tunnel hugs Y=0 and only reaches out by radius+wall (~350). The curve peaks
    // near Y=1125, so anything past 700 can only come from following the curve.
    ADD_LATENT_AUTOMATION_COMMAND(FIOCWaitForSplineCurveCommand(
        Cave, this, FPlatformTime::Seconds() + 30.0, 700.0));
    return true;
#else
    return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCCarvingVolumeTest, "InstantOrganicCaves.Carving.SmokeTest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCCarvingVolumeTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor) return false;
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) { AddError(TEXT("No Editor World")); return false; }

    // 1. Spawn a cave actor
    FActorSpawnParameters SP;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AIOCProceduralActor* Cave = World->SpawnActor<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);

    if (!Cave) { AddError(TEXT("Failed to spawn AIOCProceduralActor")); return false; }

    // 2. Add a carving component as a child
    UIOCCarvingComponent* Carve = NewObject<UIOCCarvingComponent>(Cave);
    Carve->ShapeType    = EIOCCarvingShape::Sphere;
    Carve->SphereRadius = 300.f;
    Carve->FalloffRadius = 80.f;
    Carve->RegisterComponent();
    Carve->AttachToComponent(Cave->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

    // 3. MakeCapture must return correct data
    FIOCCarvingCapture Cap = Carve->MakeCapture();
    if (Cap.ShapeType != EIOCCarvingShape::Sphere)
    {
        AddError(TEXT("MakeCapture: ShapeType mismatch"));
        World->DestroyActor(Cave);
        return false;
    }
    if (!FMath::IsNearlyEqual(Cap.SphereRadius, 300.f))
    {
        AddError(TEXT("MakeCapture: SphereRadius mismatch"));
        World->DestroyActor(Cave);
        return false;
    }
    if (!FMath::IsNearlyEqual(Cap.FalloffRadius, 80.f))
    {
        AddError(TEXT("MakeCapture: FalloffRadius mismatch"));
        World->DestroyActor(Cave);
        return false;
    }

    // 4. GenerateCave with a carving volume must not crash
    Cave->GenerateCave();

    AddInfo(TEXT("IOCCarvingComponent smoke test passed (mesh generation is async)."));
    World->DestroyActor(Cave);
    return true;
#else
    return true;
#endif
}

// -----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCLODVisibilityResetTest, "InstantOrganicCaves.Cave.LODVisibilityReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCLODVisibilityResetTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor)
    {
        AddError(TEXT("No editor available."));
        return false;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        AddError(TEXT("No editor world available."));
        return false;
    }

    FActorSpawnParameters SP;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AIOCProceduralActor* Cave = World->SpawnActor<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(), FVector(12000, 0, 0), FRotator::ZeroRotator, SP);
    if (!Cave)
    {
        AddError(TEXT("Failed to spawn cave actor for LOD visibility test."));
        return false;
    }

    Cave->MeshComponent->SetVisibility(false);
    Cave->LODMeshComponent->SetVisibility(true);
    Cave->bEnableLOD = false;
    Cave->RerunConstructionScripts();

    TestTrue(TEXT("Primary mesh becomes visible when LOD is disabled"), Cave->MeshComponent->IsVisible());
    TestFalse(TEXT("LOD mesh is hidden when LOD is disabled"), Cave->LODMeshComponent->IsVisible());

    World->DestroyActor(Cave);
    return true;
#else
    return true;
#endif
}



#endif // WITH_DEV_AUTOMATION_TESTS
