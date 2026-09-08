// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "InstantOrganicCavesModule.h"
#include "Elements/IOCVoxelCore.h" // Relative path matches Public folder structure
#include "Data/PCGPointData.h"

// --- Demo Spawning ---
#include "IOCProceduralActor.h"
#include "IOCCharacter.h"
#include "IOCStreamingManager.h"
#include "IOCShowcaseLauncher.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

// Environment
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "Interfaces/IPluginManager.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "TimerManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

DEFINE_LOG_CATEGORY(LogIOC);

FIOCShowcaseViewportHooks& IOCGetShowcaseViewportHooks()
{
    static FIOCShowcaseViewportHooks Hooks;
    return Hooks;
}

#define LOCTEXT_NAMESPACE "FInstantOrganicCavesModule"

/** Marks every environment actor the demo/showcase commands create, so they can be reused
 *  across repeated runs and destroyed on cleanup. */
static const FName IOCDemoEnvironmentTag(TEXT("IOC_DemoEnvironment"));

/** Environment actors this plugin spawned. Cleared by ClearShowcase. */
static TArray<TWeakObjectPtr<AActor>> GIOCDemoEnvironmentActors;

/** Scatter meshes the demos use, plugin-owned first with an engine primitive as the
 *  last-resort fallback -- the same order IOC_LoadFirstStaticMesh uses in the wizard.
 *  The plugin ships these (Resources/GenerateScatterMeshes.py), so the fallback only
 *  matters if a build was packaged without content. */
namespace IOCDemoMeshes
{
    static const TCHAR* Geo = TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/");

    static UStaticMesh* LoadFirst(const TCHAR* PluginMeshName, const TCHAR* EngineFallback)
    {
        if (PluginMeshName)
        {
            const FString Path = FString::Printf(TEXT("%s%s.%s"), Geo, PluginMeshName, PluginMeshName);
            if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path))
            {
                return Mesh;
            }
        }
        return EngineFallback ? LoadObject<UStaticMesh>(nullptr, EngineFallback) : nullptr;
    }
}

/** Marks the headline actors the demo commands create -- the demo caves and the demo
 *  character. Separate from the environment tag because these are the things the user came
 *  to look at, and ClearShowcase deliberately leaves them alone. */
static const FName IOCDemoActorTag(TEXT("IOC_DemoActor"));

/** Demo caves and characters this plugin spawned. Cleared by ClearAllDemos. */
static TArray<TWeakObjectPtr<AActor>> GIOCDemoActors;

/** Returns the demo-tagged actor of type T already in the world, if any. */
template<typename T>
static T* IOC_FindDemoActor(UWorld* World)
{
    if (!World)
    {
        return nullptr;
    }

    for (TActorIterator<T> It(World); It; ++It)
    {
        if (IsValid(*It) && It->Tags.Contains(IOCDemoActorTag))
        {
            return *It;
        }
    }
    return nullptr;
}

/** Tags, labels and tracks a demo actor so repeated runs can reuse it and cleanup can find it. */
static void IOC_RegisterDemoActor(AActor* Actor, const TCHAR* Label)
{
    if (!Actor)
    {
        return;
    }

    Actor->Tags.AddUnique(IOCDemoActorTag);
#if WITH_EDITOR
    if (Label)
    {
        Actor->SetActorLabel(Label);
    }
#else
    (void)Label;
#endif
    GIOCDemoActors.AddUnique(Actor);
}

/**
 * Returns the demo character, moving the existing one rather than spawning a second.
 *
 * SpawnTunnelDemo and SpawnSpectacular both used to spawn their own AIOCCharacter labelled
 * "IOC_Player". Running both left two pawns in the level, which makes possession on PIE
 * ambiguous and litters the outliner with duplicates.
 */
static AIOCCharacter* IOC_EnsureDemoCharacter(UWorld* World, const FVector& Location,
    const FActorSpawnParameters& SpawnParams)
{
    if (!World)
    {
        return nullptr;
    }

    if (AIOCCharacter* Existing = IOC_FindDemoActor<AIOCCharacter>(World))
    {
        Existing->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
        return Existing;
    }

    AIOCCharacter* Spawned = World->SpawnActor<AIOCCharacter>(
        Location, FRotator::ZeroRotator, SpawnParams);
    IOC_RegisterDemoActor(Spawned, TEXT("IOC_Player"));
    return Spawned;
}

/**
 * Returns the plugin-owned environment actor of type T, creating it if needed.
 *
 * This deliberately does NOT reuse an arbitrary existing actor. The previous version took
 * the first DirectionalLight / SkyLight / Fog / PostProcessVolume it found in the level,
 * overwrote its rotation, intensity and colour, and renamed it -- so running a demo in a
 * user's own level silently vandalised their lighting, with no undo and no cleanup.
 */
template<typename T>
static T* IOC_EnsureDemoActor(UWorld* World, const TCHAR* Label)
{
    if (!World)
    {
        return nullptr;
    }

    for (TActorIterator<T> It(World); It; ++It)
    {
        if (It->Tags.Contains(IOCDemoEnvironmentTag))
        {
            return *It;
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (!World->IsGameWorld())
    {
        // Editor-world demo dressing is throwaway; do not persist it into the user's level.
        SpawnParams.ObjectFlags |= RF_Transient;
    }

    T* Spawned = World->SpawnActor<T>(SpawnParams);
    if (Spawned)
    {
        Spawned->Tags.Add(IOCDemoEnvironmentTag);
#if WITH_EDITOR
        if (Label)
        {
            Spawned->SetActorLabel(Label);
        }
#else
        (void)Label;
#endif
        GIOCDemoEnvironmentActors.Add(Spawned);
    }
    return Spawned;
}

static UWorld* IOC_ResolveTargetWorld(UWorld* PreferredWorld = nullptr)
{
    if (PreferredWorld)
    {
        return PreferredWorld;
    }

#if WITH_EDITOR
    if (GEditor)
    {
        if (UWorld* PlayWorld = GEditor->PlayWorld)
        {
            return PlayWorld;
        }

        if (UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
        {
            return EditorWorld;
        }
    }
#endif

    return GEngine ? GEngine->GetWorld() : nullptr;
}

#if WITH_EDITOR
static void IOC_ShowNotification(const FText& Message,
    SNotificationItem::ECompletionState State = SNotificationItem::CS_None,
    float ExpireDuration = 4.0f)
{
    FNotificationInfo Notification(Message);
    Notification.ExpireDuration = ExpireDuration;
    Notification.bFireAndForget = true;
    Notification.bUseLargeFont = false;

    if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Notification))
    {
        Item->SetCompletionState(State);
    }
}
#endif

static bool IOC_ShouldGenerateImmediately(UWorld* World)
{
    return World && !World->IsPlayInEditor() && !World->IsGameWorld();
}

static void IOC_FinishProceduralSpawn(AIOCProceduralActor* Actor, const FTransform& SpawnTransform, UWorld* World)
{
    if (!Actor)
    {
        return;
    }

    Actor->FinishSpawning(SpawnTransform);

    // Actors spawned into the editor world do not get BeginPlay, so generate explicitly there.
    if (IOC_ShouldGenerateImmediately(World))
    {
        Actor->GenerateCave();
    }
}

static bool SpawnTunnelDemoInternal(UWorld* World)
{
    if (!World)
    {
        UE_LOG(LogIOC, Warning, TEXT("No World found to spawn demo!"));
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // --- Environment Setup ---
    // Sun
    ADirectionalLight* Sun = IOC_EnsureDemoActor<ADirectionalLight>(World, TEXT("IOC_Sun"));
    if (Sun)
    {
        Sun->SetActorRotation(FRotator(-50.0f, -30.0f, 0.0f));
        
        UDirectionalLightComponent* DLC = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
        if (DLC)
        {
            DLC->SetIntensity(6.0f); 
            DLC->CastShadows = true;
            DLC->SetEnableLightShaftOcclusion(true);
        }
    }

    // Sky
    ASkyLight* Sky = IOC_EnsureDemoActor<ASkyLight>(World, TEXT("IOC_SkyLight"));
    if (Sky)
    {
        Sky->GetLightComponent()->SetRealTimeCapture(true);
    }

    // Fog
    AExponentialHeightFog* Fog = IOC_EnsureDemoActor<AExponentialHeightFog>(World, TEXT("IOC_Fog"));
    if (Fog)
    {
        Fog->GetComponent()->SetFogDensity(0.02f);
        Fog->GetComponent()->SetFogHeightFalloff(0.2f);
        Fog->GetComponent()->SetStartDistance(0.0f);
    }

    // PPV
    APostProcessVolume* PPV = IOC_EnsureDemoActor<APostProcessVolume>(World, TEXT("IOC_PostProcess"));
    if (PPV)
    {
        PPV->bUnbound = true;
        PPV->Settings.bOverride_AutoExposureMinBrightness = true;
        PPV->Settings.bOverride_AutoExposureMaxBrightness = true;
        PPV->Settings.AutoExposureMinBrightness = 0.03f; // Allow dark caves
        PPV->Settings.AutoExposureMaxBrightness = 2.0f;
    }

    // 2. Spawn Tunnel
    AIOCProceduralActor* DemoActor = World->SpawnActor<AIOCProceduralActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
    FVector StartLoc(0, 0, 0);
    
    if (DemoActor)
    {
        IOC_RegisterDemoActor(DemoActor, TEXT("IOC_Tunnel_Demo"));
        DemoActor->bGenerateTunnel = true;
        // Updated API for Non-Cubic Bounds
        DemoActor->GenerationBounds = FVector(5000, 3000, 1500); 
        DemoActor->VoxelSize = 60.0; 
        DemoActor->TunnelStart = StartLoc;
        DemoActor->TunnelEnd = FVector(3000, 1500, 800);
        DemoActor->TunnelRadius = 450.0f;
        DemoActor->WallThickness = 120.0f;
        DemoActor->NoiseFrequency = 0.004f; 
        DemoActor->TextureTiling = 0.005f;

        // Try load plugin material, fallback to engine default
        UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/InstantOrganicCaves/MI_IOC_CaveWalls.MI_IOC_CaveWalls"));
        if (!Mat)
        {
            Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
        }
        
        if (Mat) DemoActor->CaveMaterial = Mat;

        DemoActor->GenerateCave();
        UE_LOG(LogIOC, Log, TEXT("Tunnel Mesh Generation Started (Async)!"));
    }

    // 3. Spawn Character
    // Raise start slightly to avoid stuck
    FVector CharSpawnLoc = StartLoc + FVector(0, 0, 150);
    AIOCCharacter* PlayerChar = IOC_EnsureDemoCharacter(World, CharSpawnLoc, SpawnParams);

    if (PlayerChar)
    {
        // Auto Possess if PIE
        if (World->IsPlayInEditor())
        {
            APlayerController* PC = World->GetFirstPlayerController();
            if (PC)
            {
                PC->Possess(PlayerChar);
            }
        }
        
        UE_LOG(LogIOC, Log, TEXT("Player Spawned!"));
    }

    return DemoActor != nullptr;
}

static bool SpawnSpectacularDemoInternal(UWorld* World)
{
    UE_LOG(LogIOC, Log, TEXT("Attempting to spawn Spectacular Crystal Demo..."));
    
    if (!World)
    {
        UE_LOG(LogIOC, Warning, TEXT("Failed. No valid World found."));
        return false;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // --- Environment Setup ---
    ADirectionalLight* Sun = IOC_EnsureDemoActor<ADirectionalLight>(World, TEXT("IOC_Sun"));
    if (Sun)
    {
        Sun->SetActorRotation(FRotator(-80.0f, 45.0f, 0.0f));
        if (UDirectionalLightComponent* DLC = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
        {
            DLC->SetIntensity(2.0f); // Darker, moody lighting for glowing crystals to pop
            DLC->LightColor = FColor(180, 200, 255); // Cool moonlight
            DLC->bUseTemperature = true;
            DLC->Temperature = 8500.0f;
        }
    }
    
    ASkyLight* Sky = IOC_EnsureDemoActor<ASkyLight>(World, TEXT("IOC_SkyLight"));
    if (Sky) Sky->GetLightComponent()->SetRealTimeCapture(true);
    
    AExponentialHeightFog* Fog = IOC_EnsureDemoActor<AExponentialHeightFog>(World, TEXT("IOC_Fog"));
    if (Fog)
    {
        Fog->GetComponent()->SetFogDensity(0.04f);
        Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(0.1f, 0.05f, 0.2f)); // Purple/blue magical fog
    }
    
    APostProcessVolume* PPV = IOC_EnsureDemoActor<APostProcessVolume>(World, TEXT("IOC_PostProcess"));
    if (PPV)
    {
        PPV->bUnbound = true;
        PPV->Settings.bOverride_AutoExposureMinBrightness = true;
        PPV->Settings.AutoExposureMinBrightness = 0.05f;
        PPV->Settings.bOverride_AutoExposureMaxBrightness = true;
        PPV->Settings.AutoExposureMaxBrightness = 1.0f;
        
        // Bloom for crystals
        PPV->Settings.bOverride_BloomIntensity = true;
        PPV->Settings.BloomIntensity = 2.5f;
        PPV->Settings.bOverride_BloomThreshold = true;
        PPV->Settings.BloomThreshold = -1.0f; 
        
        // Color grading to make it cinematic
        PPV->Settings.bOverride_ColorSaturation = true;
        PPV->Settings.ColorSaturation = FVector4(1.2f, 1.1f, 1.4f, 1.0f);
    }

    // Spawn Actor (Deferred to allow parameter setup before BeginPlay/Generation)
    FTransform SpawnTransform = FTransform::Identity;
    AIOCProceduralActor* DemoActor = World->SpawnActorDeferred<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(), 
        SpawnTransform, 
        nullptr, 
        nullptr, 
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn
    );

    if (DemoActor)
    {
        IOC_RegisterDemoActor(DemoActor, TEXT("IOC_Spectacular_Demo"));
        // Important: Set VoxelSize to reasonable value before generation
        DemoActor->VoxelSize = 40.0; // Higher detail for crystals
        DemoActor->GenerationBounds = FVector(6000, 6000, 3000); 

        // Apply Spectacular Settings
        DemoActor->CavePreset = EIOCCavePreset::CanyonStrata;
        DemoActor->ApplyPresetSettingsOnly();
        DemoActor->CavePreset = EIOCCavePreset::Custom;
        
        DemoActor->bGenerateTunnel = true;
        DemoActor->TunnelRadius = 600.0f; // Giant cavern
        DemoActor->TunnelStart = FVector(0, 0, 0);
        DemoActor->TunnelEnd = FVector(4000, 1500, 1000);
        
        // Try to load Smart Cave material
        UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/InstantOrganicCaves/MI_IOC_SmartCave_Inst.MI_IOC_SmartCave_Inst"));
        if (!Mat) Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/InstantOrganicCaves/MI_IOC_CaveWalls.MI_IOC_CaveWalls"));
        if (Mat) DemoActor->CaveMaterial = Mat;

        // Add shipped/engine decorations so the demo does not depend on external marketplace content.
        auto AddDecor = [&](const TCHAR* PluginMesh, const TCHAR* Fallback, float Density, float ScaleMin, float ScaleMax, bool bAlign, float MinZ, float MaxZ) {
            if (UStaticMesh* Mesh = IOCDemoMeshes::LoadFirst(PluginMesh, Fallback)) {
                FIOCScatterLayer Layer;
                Layer.Mesh = Mesh;
                Layer.Density = Density;
                Layer.ScaleRange = FVector2D(ScaleMin, ScaleMax);
                Layer.bAlignToNormal = bAlign;
                Layer.MinSlopeZ = MinZ; 
                Layer.MaxSlopeZ = MaxZ;
                DemoActor->DecorationLayers.Add(Layer);
            }
        };

        // Crystals on walls/floor
        AddDecor(TEXT("SM_IOC_Crystal_A"), TEXT("/Engine/BasicShapes/Cone.Cone"), 0.4f, 1.0f, 2.0f, true, -0.5f, 1.0f);
        // Geodes clustered
        AddDecor(TEXT("SM_IOC_Geode"), TEXT("/Engine/BasicShapes/Sphere.Sphere"), 0.1f, 1.0f, 2.4f, true, -1.0f, 1.0f);
        // Free Magic Gemme
        AddDecor(TEXT("SM_IOC_Crystal_B"), TEXT("/Engine/BasicShapes/Cone.Cone"), 0.3f, 1.0f, 2.0f, true, -0.2f, 1.0f);
        // Stylized Crystals
        AddDecor(TEXT("SM_IOC_Stalactite"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), 0.2f, 0.8f, 1.6f, true, -1.0f, -0.5f);
        // Mossy Rocks on floor
        AddDecor(TEXT("SM_IOC_Rock_A"), TEXT("/Engine/BasicShapes/Cube.Cube"), 0.6f, 0.8f, 1.6f, false, 0.7f, 1.0f);
        // River Rocks everywhere
        AddDecor(TEXT("SM_IOC_Rock_C"), TEXT("/Engine/BasicShapes/Sphere.Sphere"), 1.2f, 0.5f, 1.5f, true, -0.2f, 1.0f);
        
        // Finish Spawning (Triggers BeginPlay -> GenerateCave)
        IOC_FinishProceduralSpawn(DemoActor, SpawnTransform, World);

        UE_LOG(LogIOC, Log, TEXT("Spectacular Crystal Cave Generation Started! VoxelSize: %f"), DemoActor->VoxelSize);
    }
    else
    {
        UE_LOG(LogIOC, Warning, TEXT("Failed to spawn DemoActor."));
        return false;
    }

    // Spawn Player
    FVector StartLoc(0, 0, 150);
    AIOCCharacter* PlayerChar = IOC_EnsureDemoCharacter(World, StartLoc, SpawnParams);
    if (PlayerChar)
    {
        // Reusing an existing demo character must not stack a second flashlight on it.
        const bool bHasFlashlight = PlayerChar->FindComponentByClass<UPointLightComponent>() != nullptr;
        UPointLightComponent* Flashlight = bHasFlashlight
            ? nullptr : NewObject<UPointLightComponent>(PlayerChar);
        if (Flashlight)
        {
            Flashlight->RegisterComponent();
            Flashlight->AttachToComponent(PlayerChar->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
            Flashlight->SetIntensity(3000.0f);
            Flashlight->SetAttenuationRadius(2000.0f);
            Flashlight->SetLightColor(FLinearColor(0.8f, 0.9f, 1.0f));
            Flashlight->CastShadows = true;
        }

        if (World->IsPlayInEditor())
        {
            if (APlayerController* PC = World->GetFirstPlayerController()) PC->Possess(PlayerChar);
        }
    }

    return true;
}

// ============================================================================
// Showcase Demo — Automated cinematic flythrough of all IOC features
// ============================================================================

namespace ShowcaseState
{
    struct FSection
    {
        FVector CameraPos;
        FString Name;
        FString Description;
        FString Proof;
        FColor HUDColor;
        TWeakObjectPtr<AIOCProceduralActor> MetricsActor;
    };

    static TArray<TWeakObjectPtr<AActor>> SpawnedActors;
    static TArray<FSection> Sections;
    static TWeakObjectPtr<ACameraActor> Camera;
    static TWeakObjectPtr<UWorld> World;
    static FTimerHandle CameraTimerHandle;
    static FTSTicker::FDelegateHandle EditorTickerHandle;
    static float ElapsedTime = 0.0f;
    static constexpr float TimerRate = 0.033f;
    static constexpr float SecondsPerSection = 10.0f;
    static constexpr float DwellTime = 6.5f;
    static constexpr float TransitTime = 3.5f;
    static constexpr float InitialDelay = 3.0f;
    static float StartDelayRemaining = 0.0f;
    static bool bCaptureMode = false;
    static bool bShowCaptions = true;
    static int32 LastCaptionIndex = INDEX_NONE;
    static TSharedPtr<SWidget> CaptionWidget;
    static TSharedPtr<STextBlock> CaptionTitle;
    static TSharedPtr<STextBlock> CaptionBody;
    static TSharedPtr<STextBlock> CaptionProof;
    static TSharedPtr<STextBlock> CaptionCounter;
    static double LastCameraTickTime = 0.0;
    static bool bIsActive = false;
    static bool bLoop = true;
    static bool bCameraStarted = false;
    static TArray<TWeakObjectPtr<AIOCProceduralActor>> PendingActors;
    static FTSTicker::FDelegateHandle ReadinessHandle;
    static float ReadinessElapsed = 0.0f;
    static constexpr float ReadinessTimeout = 30.0f;

#if WITH_EDITOR
    // Only whether a viewport state was saved. The saved values themselves live in the
    // editor module, which is the only thing that knows what a level viewport is.
    static bool bHasSavedEditorViewState = false;
#endif

    static void Reset()
    {
        SpawnedActors.Empty();
        Sections.Empty();
        Camera = nullptr;
        World = nullptr;
        ElapsedTime = 0.0f;
        StartDelayRemaining = 0.0f;
        bCaptureMode = false;
        bShowCaptions = true;
        LastCaptionIndex = INDEX_NONE;
        CaptionWidget.Reset();
        CaptionTitle.Reset();
        CaptionBody.Reset();
        CaptionProof.Reset();
        CaptionCounter.Reset();
        LastCameraTickTime = 0.0;
        CameraTimerHandle.Invalidate();
        EditorTickerHandle.Reset();
        bIsActive = false;
        bLoop = true;
        bCameraStarted = false;
        PendingActors.Empty();
        ReadinessHandle.Reset();
        ReadinessElapsed = 0.0f;
#if WITH_EDITOR
        bHasSavedEditorViewState = false;
#endif
    }
}

static void RemoveShowcaseCaptionOverlay()
{
    if (ShowcaseState::CaptionWidget.IsValid() && GEngine && GEngine->GameViewport)
    {
        GEngine->GameViewport->RemoveViewportWidgetContent(ShowcaseState::CaptionWidget.ToSharedRef());
    }

    ShowcaseState::CaptionWidget.Reset();
    ShowcaseState::CaptionTitle.Reset();
    ShowcaseState::CaptionBody.Reset();
    ShowcaseState::CaptionProof.Reset();
    ShowcaseState::CaptionCounter.Reset();
    ShowcaseState::LastCaptionIndex = INDEX_NONE;
}

static void ShowcaseWorldCleanupHandler(UWorld*, bool, bool)
{
    RemoveShowcaseCaptionOverlay();
}

static FDelegateHandle GIOCWorldCleanupHandle;

static void CreateShowcaseCaptionOverlay()
{
    if (!ShowcaseState::bShowCaptions || !GEngine || !GEngine->GameViewport)
    {
        return;
    }

    RemoveShowcaseCaptionOverlay();

    SAssignNew(ShowcaseState::CaptionWidget, SOverlay)
    + SOverlay::Slot()
    .HAlign(HAlign_Left)
    .VAlign(VAlign_Bottom)
    .Padding(FMargin(48.0f, 48.0f, 48.0f, 44.0f))
    [
        SNew(SBox)
        .WidthOverride(760.0f)
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.015f, 0.018f, 0.022f, 0.82f))
            .Padding(FMargin(22.0f, 18.0f, 22.0f, 18.0f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot()
                .AutoHeight()
                [
                    SAssignNew(ShowcaseState::CaptionCounter, STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.67f, 0.72f, 1.0f)))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
                [
                    SAssignNew(ShowcaseState::CaptionTitle, STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 30))
                    .ColorAndOpacity(FSlateColor(FLinearColor::White))
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
                [
                    SAssignNew(ShowcaseState::CaptionBody, STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 17))
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.90f, 0.92f, 0.95f, 1.0f)))
                    .WrapTextAt(700.0f)
                ]
                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
                [
                    SAssignNew(ShowcaseState::CaptionProof, STextBlock)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                    .ColorAndOpacity(FSlateColor(FLinearColor(0.64f, 0.78f, 0.92f, 1.0f)))
                    .WrapTextAt(700.0f)
                ]
            ]
        ]
    ];

    GEngine->GameViewport->AddViewportWidgetContent(ShowcaseState::CaptionWidget.ToSharedRef(), 100);
}

static void UpdateShowcaseCaption(int32 SectionIndex)
{
    if (!ShowcaseState::bShowCaptions ||
        !ShowcaseState::CaptionTitle.IsValid() ||
        !ShowcaseState::Sections.IsValidIndex(SectionIndex))
    {
        return;
    }

    const ShowcaseState::FSection& Section = ShowcaseState::Sections[SectionIndex];
    FString ProofText = Section.Proof;
    if (Section.MetricsActor.IsValid() && Section.MetricsActor->LastGenerationTimeSeconds > 0.0)
    {
        ProofText += TEXT("\nLive metrics: ");
        ProofText += Section.MetricsActor->GetPerformanceSummary();
    }

    ShowcaseState::CaptionTitle->SetText(FText::FromString(Section.Name));
    ShowcaseState::CaptionTitle->SetColorAndOpacity(FSlateColor(FLinearColor(Section.HUDColor)));
    ShowcaseState::CaptionBody->SetText(FText::FromString(Section.Description));
    ShowcaseState::CaptionProof->SetText(FText::FromString(ProofText));
    ShowcaseState::CaptionCounter->SetText(FText::FromString(FString::Printf(
        TEXT("Instant Organic Caves %s  |  %02d / %02d"),
        ShowcaseState::bCaptureMode ? TEXT("capture") : TEXT("showcase"),
        SectionIndex + 1,
        ShowcaseState::Sections.Num())));
    ShowcaseState::LastCaptionIndex = SectionIndex;
}

#if WITH_EDITOR
// These forward to FIOCShowcaseViewportHooks, which the editor module owns. When the editor
// module is absent (packaged game, or an editor build where it failed to load) the hooks are
// unbound and the showcase simply drives its own camera actor without touching any viewport.
static void CaptureShowcaseEditorViewportState()
{
    const FIOCShowcaseViewportHooks& Hooks = IOCGetShowcaseViewportHooks();
    if (Hooks.CaptureState)
    {
        Hooks.CaptureState();
        ShowcaseState::bHasSavedEditorViewState = true;
    }
}

static void RestoreShowcaseEditorViewportState()
{
    if (!ShowcaseState::bHasSavedEditorViewState)
    {
        return;
    }

    const FIOCShowcaseViewportHooks& Hooks = IOCGetShowcaseViewportHooks();
    if (Hooks.RestoreState)
    {
        Hooks.RestoreState();
    }
    ShowcaseState::bHasSavedEditorViewState = false;
}

static void ApplyShowcaseEditorViewport(const FVector& Pos, const FRotator& Rot)
{
    const FIOCShowcaseViewportHooks& Hooks = IOCGetShowcaseViewportHooks();
    if (Hooks.ApplyView)
    {
        Hooks.ApplyView(Pos, Rot);
    }
}
#endif

static void AdvanceShowcaseCamera(float DeltaSeconds)
{
    if (!ShowcaseState::Camera.IsValid()) return;

    ACameraActor* Cam = ShowcaseState::Camera.Get();

    const int32 NumSections = ShowcaseState::Sections.Num();
    if (NumSections == 0) return;

    FVector Pos;
    FRotator Rot;
    int32 Idx = 0;

    if (ShowcaseState::StartDelayRemaining > 0.0f)
    {
        ShowcaseState::StartDelayRemaining = FMath::Max(0.0f, ShowcaseState::StartDelayRemaining - DeltaSeconds);
        const auto& Cur = ShowcaseState::Sections[0];
        Pos = Cur.CameraPos;
        Rot = FRotator(-10.0f, 0.0f, 0.0f);
    }
    else
    {
        ShowcaseState::ElapsedTime += DeltaSeconds;

        const float TotalDuration = ShowcaseState::SecondsPerSection * NumSections;
        float Time;
        if (ShowcaseState::bLoop)
        {
            Time = FMath::Fmod(ShowcaseState::ElapsedTime, TotalDuration);
        }
        else
        {
            Time = FMath::Min(ShowcaseState::ElapsedTime, TotalDuration - 0.001f);
        }

        Idx = FMath::Clamp(FMath::FloorToInt(Time / ShowcaseState::SecondsPerSection), 0, NumSections - 1);
        const float SectionTime = FMath::Fmod(Time, ShowcaseState::SecondsPerSection);

        const auto& Cur = ShowcaseState::Sections[Idx];
        const bool bInTransit = (SectionTime > ShowcaseState::DwellTime);
        if (!bInTransit)
        {
            Pos = Cur.CameraPos;
            const float DwellAlpha = FMath::Clamp(SectionTime / ShowcaseState::DwellTime, 0.0f, 1.0f);
            Rot = FRotator(-10.0f, -20.0f + 40.0f * DwellAlpha, 0.0f);
        }
        else
        {
            const auto& Next = ShowcaseState::Sections[(Idx + 1) % NumSections];
            float Alpha = (SectionTime - ShowcaseState::DwellTime) / ShowcaseState::TransitTime;
            Alpha = FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);
            Pos = FMath::Lerp(Cur.CameraPos, Next.CameraPos, Alpha);

            FRotator TravelRot = (Next.CameraPos - Cur.CameraPos).GetSafeNormal().Rotation();
            TravelRot.Pitch = FMath::Clamp(TravelRot.Pitch, -25.0f, 5.0f);
            Rot = TravelRot;
        }
    }

    Cam->SetActorLocationAndRotation(Pos, Rot);

#if WITH_EDITOR
    if (ShowcaseState::World.IsValid() &&
        !ShowcaseState::World->IsPlayInEditor() &&
        !ShowcaseState::World->IsGameWorld())
    {
        ApplyShowcaseEditorViewport(Pos, Rot);
    }
#endif

    UpdateShowcaseCaption(Idx);
}

static void TickShowcaseCamera()
{
    const double Now = FPlatformTime::Seconds();
    const float Delta = (ShowcaseState::LastCameraTickTime > 0.0)
        ? static_cast<float>(Now - ShowcaseState::LastCameraTickTime)
        : ShowcaseState::TimerRate;
    ShowcaseState::LastCameraTickTime = Now;
    AdvanceShowcaseCamera(Delta);
}

#if WITH_EDITOR
static bool TickShowcaseCameraEditor(float DeltaSeconds)
{
    AdvanceShowcaseCamera(DeltaSeconds);
    return ShowcaseState::Camera.IsValid() && ShowcaseState::Sections.Num() > 0;
}
#endif

static bool ClearShowcaseDemoInternal(UWorld* World)
{
    if (World)
    {
        World->GetTimerManager().ClearTimer(ShowcaseState::CameraTimerHandle);

        if (World->IsPlayInEditor() || World->IsGameWorld())
        {
            APlayerController* PC = World->GetFirstPlayerController();
            if (PC && PC->GetPawn())
            {
                PC->SetViewTarget(PC->GetPawn());
            }
        }
    }

    if (ShowcaseState::ReadinessHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(ShowcaseState::ReadinessHandle);
    }

#if WITH_EDITOR
    if (ShowcaseState::EditorTickerHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(ShowcaseState::EditorTickerHandle);
    }

    if (ShowcaseState::bHasSavedEditorViewState)
    {
        RestoreShowcaseEditorViewportState();
    }
#endif

    for (auto& WeakActor : ShowcaseState::SpawnedActors)
    {
        if (WeakActor.IsValid())
        {
            WeakActor->Destroy();
        }
    }

    // Environment dressing is plugin-owned (see IOC_EnsureDemoActor), so cleanup can remove
    // it without touching anything the user placed themselves.
    for (auto& WeakActor : GIOCDemoEnvironmentActors)
    {
        if (WeakActor.IsValid())
        {
            WeakActor->Destroy();
        }
    }
    GIOCDemoEnvironmentActors.Reset();

    RemoveShowcaseCaptionOverlay();
    ShowcaseState::Reset();

    UE_LOG(LogIOC, Log, TEXT("Showcase Demo cleared."));
    return true;
}

// ---------------------------------------------------------------------------
// Readiness guard — defers camera start until all showcase caves are generated
// ---------------------------------------------------------------------------

static void StartShowcaseCamera()
{
    ShowcaseState::bCameraStarted = true;
    ShowcaseState::ElapsedTime = 0.0f;
    ShowcaseState::StartDelayRemaining = ShowcaseState::InitialDelay;

    UpdateShowcaseCaption(0);

    UWorld* World = ShowcaseState::World.Get();
    if (!World)
    {
        return;
    }

#if WITH_EDITOR
    if (!World->IsPlayInEditor() && !World->IsGameWorld())
    {
        CaptureShowcaseEditorViewportState();
        AdvanceShowcaseCamera(0.0f);
        ShowcaseState::EditorTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateStatic(&TickShowcaseCameraEditor));
    }
    else
#endif
    {
        FTimerDelegate TimerDel = FTimerDelegate::CreateStatic(&TickShowcaseCamera);
        World->GetTimerManager().SetTimer(
            ShowcaseState::CameraTimerHandle, TimerDel,
            ShowcaseState::TimerRate, true, 0.0f);
    }

    UE_LOG(LogIOC, Log, TEXT("Showcase camera flythrough started."));
}

static bool TickReadinessGuard(float DeltaSeconds)
{
    ShowcaseState::ReadinessElapsed += DeltaSeconds;

    bool bAllReady = true;
    for (const auto& WeakActor : ShowcaseState::PendingActors)
    {
        if (WeakActor.IsValid() && WeakActor->LastGenerationTimeSeconds <= 0.0)
        {
            bAllReady = false;
            break;
        }
    }

    if (bAllReady)
    {
        UE_LOG(LogIOC, Log, TEXT("All showcase caves generated in %.1fs. Starting camera flythrough."),
            ShowcaseState::ReadinessElapsed);
        ShowcaseState::ReadinessHandle.Reset();
        StartShowcaseCamera();
        return false;
    }

    if (ShowcaseState::ReadinessElapsed > ShowcaseState::ReadinessTimeout)
    {
        UE_LOG(LogIOC, Warning, TEXT("Showcase readiness timed out after %.0fs. Starting camera anyway."),
            ShowcaseState::ReadinessTimeout);
        ShowcaseState::ReadinessHandle.Reset();
        StartShowcaseCamera();
        return false;
    }

    return true;
}

static bool SpawnShowcaseDemoInternal(UWorld* World, const FIOCShowcaseOptions& Options)
{
    UE_LOG(LogIOC, Log, TEXT("Spawning Showcase Demo..."));

    if (ShowcaseState::bIsActive)
    {
        UE_LOG(LogIOC, Warning, TEXT("Showcase is already active. Call ClearShowcase first."));
        return false;
    }

    if (!World)
    {
        UE_LOG(LogIOC, Warning, TEXT("No World found! Use Play-In-Editor for best results."));
        return false;
    }

    // Clear any prior showcase
    ClearShowcaseDemoInternal(World);
    ShowcaseState::bIsActive = true;
    ShowcaseState::World = World;
    ShowcaseState::bCaptureMode = Options.bCaptureMode;
    ShowcaseState::bShowCaptions = Options.bShowCaptions;
    ShowcaseState::bLoop = Options.bLoop;

    FActorSpawnParameters SP;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // Load materials
    UMaterialInterface* SmartMat = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/InstantOrganicCaves/MI_IOC_SmartCave_Inst.MI_IOC_SmartCave_Inst"));
    UMaterialInterface* FallbackMat = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/InstantOrganicCaves/MI_IOC_CaveWalls.MI_IOC_CaveWalls"));
    if (!FallbackMat) FallbackMat = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
    UMaterialInterface* CaveMat = SmartMat ? SmartMat : FallbackMat;

    const float Spacing = 6000.0f;
    const float CamH = 400.0f;

    // --- Helper: spawn a section point light ---
    auto SpawnLight = [&](FVector Loc, FLinearColor Color, float Intensity = 8.0f, float Radius = 4000.0f)
    {
        APointLight* L = World->SpawnActor<APointLight>(Loc + FVector(0, 0, 500), FRotator::ZeroRotator, SP);
        if (L)
        {
            // Movable for the same reason as the sun: Lumen GI ignores anything else, and
            // these fill lights are what make the cave interiors readable.
            if (USceneComponent* Root = L->GetRootComponent())
            {
                Root->SetMobility(EComponentMobility::Movable);
            }
            if (UPointLightComponent* PLC = Cast<UPointLightComponent>(L->GetLightComponent()))
            {
                PLC->SetMobility(EComponentMobility::Movable);
                PLC->SetIntensity(Intensity);
                PLC->SetAttenuationRadius(Radius);
                PLC->SetLightColor(Color);
            }
            ShowcaseState::SpawnedActors.Add(L);
        }
    };

    // --- Helper: add a decoration scatter layer ---
    auto AddDecor = [](AIOCProceduralActor* A, const TCHAR* PluginMesh, const TCHAR* Fallback,
        float Density, float ScaleMin, float ScaleMax, bool bAlign, float MinZ, float MaxZ,
        float Poisson = 0.0f)
    {
        if (UStaticMesh* Mesh = IOCDemoMeshes::LoadFirst(PluginMesh, Fallback))
        {
            FIOCScatterLayer Layer;
            Layer.Mesh = Mesh;
            Layer.Density = Density;
            Layer.ScaleRange = FVector2D(ScaleMin, ScaleMax);
            Layer.bAlignToNormal = bAlign;
            Layer.MinSlopeZ = MinZ;
            Layer.MaxSlopeZ = MaxZ;
            Layer.PoissonMinSeparation = Poisson;
            A->DecorationLayers.Add(Layer);
        }
    };

    // =====================================================================
    // Shared Environment
    // =====================================================================

    // Sun
    //
    // Mobility must be set before the light is moved or lit. A default DirectionalLight is
    // Stationary, so rotating it logs "has to be 'Movable' if you'd like to move" -- and,
    // more importantly, Lumen global illumination only considers Movable lights, so a
    // Stationary key light contributes nothing to GI and the caves render near-black.
    ADirectionalLight* Sun = IOC_EnsureDemoActor<ADirectionalLight>(World, TEXT("IOC_Showcase_Sun"));
    if (Sun)
    {
        if (USceneComponent* Root = Sun->GetRootComponent())
        {
            Root->SetMobility(EComponentMobility::Movable);
        }
        Sun->SetActorRotation(FRotator(-55.0f, -30.0f, 0.0f));
        if (UDirectionalLightComponent* DLC = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
        {
            DLC->SetMobility(EComponentMobility::Movable);
            DLC->SetIntensity(4.0f);
            DLC->CastShadows = true;
        }
    }

    // Sky atmosphere.
    //
    // The sky light below runs real-time capture, which UE refuses to do without something
    // to capture -- it draws a red error over the viewport reading "A sky light with
    // real-time capture enable is in the scene. It requires at least a SkyAtmosphere
    // component...". That message is rendered, not logged, so it never showed up in headless
    // runs while being the first thing a customer saw. Give the capture a real sky.
    ASkyAtmosphere* SkyAtmos = IOC_EnsureDemoActor<ASkyAtmosphere>(World, TEXT("IOC_Showcase_SkyAtmosphere"));
    if (SkyAtmos)
    {
        if (USceneComponent* Root = SkyAtmos->GetRootComponent())
        {
            Root->SetMobility(EComponentMobility::Movable);
        }
    }

    // Sky
    ASkyLight* Sky = IOC_EnsureDemoActor<ASkyLight>(World, TEXT("IOC_Showcase_Sky"));
    if (Sky)
    {
        if (USceneComponent* Root = Sky->GetRootComponent())
        {
            Root->SetMobility(EComponentMobility::Movable);
        }
        if (USkyLightComponent* SLC = Sky->GetLightComponent())
        {
            SLC->SetMobility(EComponentMobility::Movable);
            SLC->SetRealTimeCapture(true);
            SLC->SetIntensity(0.5f);
        }
    }

    // Fog
    AExponentialHeightFog* Fog = IOC_EnsureDemoActor<AExponentialHeightFog>(World, TEXT("IOC_Showcase_Fog"));
    if (Fog)
    {
        Fog->GetComponent()->SetFogDensity(0.012f);
        Fog->GetComponent()->SetFogHeightFalloff(0.15f);
        Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(0.08f, 0.1f, 0.18f));
    }

    // Post-Process
    APostProcessVolume* PPV = IOC_EnsureDemoActor<APostProcessVolume>(World, TEXT("IOC_Showcase_PostProcess"));
    if (PPV)
    {
        PPV->bUnbound = true;
        PPV->Settings.bOverride_AutoExposureMinBrightness = true;
        PPV->Settings.AutoExposureMinBrightness = 0.02f;
        PPV->Settings.bOverride_AutoExposureMaxBrightness = true;
        PPV->Settings.AutoExposureMaxBrightness = 1.5f;
        PPV->Settings.bOverride_BloomIntensity = true;
        PPV->Settings.BloomIntensity = 1.5f;
        PPV->Settings.bOverride_ColorSaturation = true;
        PPV->Settings.ColorSaturation = FVector4(1.15f, 1.1f, 1.2f, 1.0f);
    }

    // =====================================================================
    // Section 1 — Organic Cavern  (Cellular Automata)
    // =====================================================================
    {
        FVector Pos(0, 0, 0);
        FTransform T(FRotator::ZeroRotator, Pos);
        auto* A = World->SpawnActorDeferred<AIOCProceduralActor>(
            AIOCProceduralActor::StaticClass(), T, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (A)
        {
#if WITH_EDITOR
            A->SetActorLabel(TEXT("IOC_Showcase_1_OrganicCavern"));
#endif
            A->CavePreset = EIOCCavePreset::Custom;
            A->bGenerateTunnel = false;
            A->GenerationBounds = FVector(4000, 4000, 2500);
            A->VoxelSize = 80.0;
            A->NoiseFrequency = 0.006f;
            A->NoiseThreshold = 0.48f;
            A->SmoothIterations = 5;
            A->CaveSeed = 42;
            A->TextureTiling = 0.005f;
            A->bGenerateSmartColors = true;
            if (CaveMat) A->CaveMaterial = CaveMat;
            IOC_FinishProceduralSpawn(A, T, World);
            ShowcaseState::SpawnedActors.Add(A);
            ShowcaseState::PendingActors.Add(A);
            SpawnLight(Pos, FLinearColor(1.0f, 0.7f, 0.3f));
            ShowcaseState::Sections.Add({
                Pos + FVector(2000, 2000, CamH),
                TEXT("Drop-In Cave Actor"),
                TEXT("Place BP_IOC_Cave, choose a preset or custom fill, then press Generate for a playable cavern."),
                TEXT("Proof: 80 cm voxels, 5 smoothing passes, async mesh generation, smart floor/wall/ceiling colors."),
                FColor(255, 180, 80)
            });
        }
    }

    // =====================================================================
    // Section 2 — Sculpted Tunnel  (Perlin Tunnel Mode)
    // =====================================================================
    {
        FVector Pos(Spacing, 0, 0);
        FTransform T(FRotator::ZeroRotator, Pos);
        auto* A = World->SpawnActorDeferred<AIOCProceduralActor>(
            AIOCProceduralActor::StaticClass(), T, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (A)
        {
#if WITH_EDITOR
            A->SetActorLabel(TEXT("IOC_Showcase_2_SculptedTunnel"));
#endif
            A->CavePreset = EIOCCavePreset::Custom;
            A->bGenerateTunnel = true;
            A->GenerationBounds = FVector(5000, 3000, 2000);
            A->VoxelSize = 50.0;
            A->TunnelStart = FVector(0, 0, 0);
            A->TunnelEnd = FVector(3500, 1500, 500);
            A->TunnelRadius = 500.0f;
            A->WallThickness = 200.0f;
            A->NoiseFrequency = 0.003f;
            A->TextureTiling = 0.005f;
            A->bGenerateSmartColors = true;
            if (CaveMat) A->CaveMaterial = CaveMat;
            IOC_FinishProceduralSpawn(A, T, World);
            ShowcaseState::SpawnedActors.Add(A);
            ShowcaseState::PendingActors.Add(A);
            SpawnLight(Pos + FVector(1500, 750, 0), FLinearColor(0.3f, 0.5f, 1.0f));
            ShowcaseState::Sections.Add({
                Pos + FVector(1500, 750, CamH),
                TEXT("Path-Driven Tunnel"),
                TEXT("Set TunnelStart/TunnelEnd or edit a spline; IOC turns the path into a continuous organic passage."),
                TEXT("Proof: 50 cm voxels, 500 cm radius, thick shell safety, generated collision."),
                FColor(80, 130, 255)
            });
        }
    }

    // =====================================================================
    // Section 3 — Alien Hive  (Domain Warp)
    // =====================================================================
    {
        FVector Pos(Spacing * 2, 0, 0);
        FTransform T(FRotator::ZeroRotator, Pos);
        auto* A = World->SpawnActorDeferred<AIOCProceduralActor>(
            AIOCProceduralActor::StaticClass(), T, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (A)
        {
#if WITH_EDITOR
            A->SetActorLabel(TEXT("IOC_Showcase_3_AlienHive"));
#endif
            A->CavePreset = EIOCCavePreset::AlienHive;
            A->ApplyPresetSettingsOnly();
            A->CavePreset = EIOCCavePreset::Custom; // Prevent BeginPlay re-apply
            A->GenerationBounds = FVector(4000, 4000, 2500);
            A->VoxelSize = 60.0;
            A->bGenerateTunnel = true;
            A->TunnelRadius = 450.0f;
            A->TunnelStart = FVector(0, 0, 0);
            A->TunnelEnd = FVector(3000, 2000, 800);
            A->TextureTiling = 0.005f;
            A->bGenerateSmartColors = true;
            if (CaveMat) A->CaveMaterial = CaveMat;
            IOC_FinishProceduralSpawn(A, T, World);
            ShowcaseState::SpawnedActors.Add(A);
            ShowcaseState::PendingActors.Add(A);
            SpawnLight(Pos + FVector(1500, 1000, 0), FLinearColor(0.2f, 1.0f, 0.3f));
            ShowcaseState::Sections.Add({
                Pos + FVector(1500, 1000, CamH),
                TEXT("Preset: Alien Hive"),
                TEXT("Presets give art-directed outcomes without tuning every noise parameter by hand."),
                TEXT("Proof: domain warp preset, tunnel mode, smart material, async preview."),
                FColor(60, 255, 80)
            });
        }
    }

    // =====================================================================
    // Section 4 — Canyon Strata  (Terrace Steps)
    // =====================================================================
    {
        FVector Pos(Spacing * 3, 0, 0);
        FTransform T(FRotator::ZeroRotator, Pos);
        auto* A = World->SpawnActorDeferred<AIOCProceduralActor>(
            AIOCProceduralActor::StaticClass(), T, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (A)
        {
#if WITH_EDITOR
            A->SetActorLabel(TEXT("IOC_Showcase_4_CanyonStrata"));
#endif
            A->CavePreset = EIOCCavePreset::CanyonStrata;
            A->ApplyPresetSettingsOnly();
            A->CavePreset = EIOCCavePreset::Custom; // Prevent BeginPlay re-apply
            A->GenerationBounds = FVector(4500, 4500, 2500);
            A->VoxelSize = 50.0;
            A->bGenerateTunnel = true;
            A->TunnelRadius = 550.0f;
            A->TunnelStart = FVector(0, 0, 0);
            A->TunnelEnd = FVector(3000, 1500, 600);
            A->TextureTiling = 0.005f;
            A->bGenerateSmartColors = true;
            if (CaveMat) A->CaveMaterial = CaveMat;
            IOC_FinishProceduralSpawn(A, T, World);
            ShowcaseState::SpawnedActors.Add(A);
            ShowcaseState::PendingActors.Add(A);
            SpawnLight(Pos + FVector(1500, 750, 0), FLinearColor(1.0f, 0.5f, 0.15f));
            ShowcaseState::Sections.Add({
                Pos + FVector(1500, 750, CamH),
                TEXT("Preset: Canyon Strata"),
                TEXT("Terrace steps produce layered geology for mines, ravines, and exposed cave shelves."),
                TEXT("Proof: 50 cm voxels, terraced density field, LOD-ready generated mesh."),
                FColor(255, 130, 40)
            });
        }
    }

    // =====================================================================
    // Section 5 — Crystal Gallery  (Decoration Scattering)
    // =====================================================================
    {
        FVector Pos(Spacing * 4, 0, 0);
        FTransform T(FRotator::ZeroRotator, Pos);
        auto* A = World->SpawnActorDeferred<AIOCProceduralActor>(
            AIOCProceduralActor::StaticClass(), T, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (A)
        {
#if WITH_EDITOR
            A->SetActorLabel(TEXT("IOC_Showcase_5_CrystalGallery"));
#endif
            A->CavePreset = EIOCCavePreset::LargeTunnel;
            A->ApplyPresetSettingsOnly();
            A->CavePreset = EIOCCavePreset::Custom; // Prevent BeginPlay re-apply
            A->GenerationBounds = FVector(6000, 4000, 2500);
            A->VoxelSize = 45.0;
            A->bGenerateTunnel = true;
            A->TunnelRadius = 600.0f;
            A->WallThickness = 180.0f;
            A->TunnelStart = FVector(0, 0, 0);
            A->TunnelEnd = FVector(4000, 1500, 800);
            A->TextureTiling = 0.005f;
            A->bGenerateSmartColors = true;
            if (CaveMat) A->CaveMaterial = CaveMat;

            // Six decoration layers using engine meshes so the showcase is self-contained.
            AddDecor(A, TEXT("SM_IOC_Crystal_A"), TEXT("/Engine/BasicShapes/Cone.Cone"),
                0.4f, 0.9f, 1.8f, true, -0.5f, 1.0f);
            AddDecor(A, TEXT("SM_IOC_Geode"), TEXT("/Engine/BasicShapes/Sphere.Sphere"),
                0.1f, 1.0f, 2.2f, true, -1.0f, 1.0f);
            AddDecor(A, TEXT("SM_IOC_Crystal_B"), TEXT("/Engine/BasicShapes/Cone.Cone"),
                0.3f, 0.9f, 1.9f, true, -0.2f, 1.0f);
            AddDecor(A, TEXT("SM_IOC_Stalactite"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
                0.2f, 0.8f, 1.5f, true, -1.0f, -0.5f);
            AddDecor(A, TEXT("SM_IOC_Rock_A"), TEXT("/Engine/BasicShapes/Cube.Cube"),
                0.6f, 0.8f, 1.5f, false, 0.7f, 1.0f);
            AddDecor(A, TEXT("SM_IOC_Rock_C"), TEXT("/Engine/BasicShapes/Sphere.Sphere"),
                1.2f, 0.6f, 1.4f, true, -0.2f, 1.0f);

            IOC_FinishProceduralSpawn(A, T, World);
            ShowcaseState::SpawnedActors.Add(A);
            ShowcaseState::PendingActors.Add(A);
            SpawnLight(Pos + FVector(1500, 750, 0), FLinearColor(0.6f, 0.2f, 1.0f), 10.0f);
            SpawnLight(Pos + FVector(3000, 1000, 0), FLinearColor(0.2f, 0.4f, 1.0f), 6.0f);
            ShowcaseState::Sections.Add({
                Pos + FVector(2000, 750, CamH),
                TEXT("Scatter Props"),
                TEXT("Add scatter layers, slope ranges, scale ranges, and Poisson spacing; props populate during generation."),
                TEXT("Proof: six layers of the plugin's own generated meshes, slope filters, async instance placement, no external content."),
                FColor(160, 80, 255)
            });
        }
    }

    // =====================================================================
    // Section 6 — Carved Chamber  (Carving Components)
    // =====================================================================
    {
        FVector Pos(Spacing * 5, 0, 0);
        FTransform T(FRotator::ZeroRotator, Pos);
        auto* A = World->SpawnActorDeferred<AIOCProceduralActor>(
            AIOCProceduralActor::StaticClass(), T, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (A)
        {
#if WITH_EDITOR
            A->SetActorLabel(TEXT("IOC_Showcase_6_CarvedChamber"));
#endif
            A->CavePreset = EIOCCavePreset::OpenCavern;
            A->ApplyPresetSettingsOnly();
            A->CavePreset = EIOCCavePreset::Custom; // Prevent BeginPlay re-apply
            A->GenerationBounds = FVector(4000, 4000, 2500);
            A->VoxelSize = 50.0;
            A->bGenerateTunnel = true;
            A->TunnelRadius = 500.0f;
            A->TunnelStart = FVector(0, 0, 0);
            A->TunnelEnd = FVector(3000, 2000, 500);
            A->TextureTiling = 0.005f;
            A->bGenerateSmartColors = true;
            if (CaveMat) A->CaveMaterial = CaveMat;

            auto* Sphere = NewObject<UIOCCarvingComponent>(A);
            if (Sphere)
            {
                Sphere->RegisterComponent();
                Sphere->AttachToComponent(A->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
                Sphere->SetRelativeLocation(FVector(1000, 500, 300));
                Sphere->UpdateComponentToWorld();
                Sphere->ShapeType = EIOCCarvingShape::Sphere;
                Sphere->SphereRadius = 400.0f;
                Sphere->FalloffRadius = 100.0f;
            }
            // Box carve
            auto* Box = NewObject<UIOCCarvingComponent>(A);
            if (Box)
            {
                Box->RegisterComponent();
                Box->AttachToComponent(A->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
                Box->SetRelativeLocation(FVector(2000, 1200, 200));
                Box->UpdateComponentToWorld();
                Box->ShapeType = EIOCCarvingShape::Box;
                Box->BoxExtent = FVector(350, 250, 300);
                Box->FalloffRadius = 80.0f;
            }
            // Capsule carve
            auto* Capsule = NewObject<UIOCCarvingComponent>(A);
            if (Capsule)
            {
                Capsule->RegisterComponent();
                Capsule->AttachToComponent(A->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
                Capsule->SetRelativeLocation(FVector(2500, 800, 400));
                Capsule->UpdateComponentToWorld();
                Capsule->ShapeType = EIOCCarvingShape::Capsule;
                Capsule->CapsuleRadius = 200.0f;
                Capsule->CapsuleHalfHeight = 350.0f;
                Capsule->FalloffRadius = 80.0f;
            }

            IOC_FinishProceduralSpawn(A, T, World);
            ShowcaseState::SpawnedActors.Add(A);
            ShowcaseState::PendingActors.Add(A);
            SpawnLight(Pos + FVector(1500, 1000, 0), FLinearColor(1.0f, 0.25f, 0.15f));
            ShowcaseState::Sections.Add({
                Pos + FVector(1500, 1000, CamH),
                TEXT("Directed Carving"),
                TEXT("Place sphere, box, and capsule carving components to guarantee rooms, doors, and gameplay beats."),
                TEXT("Proof: smoothstep falloff blends carve volumes into the generated mesh without hand modeling."),
                FColor(255, 70, 40)
            });
        }
    }

    // =====================================================================
    // Section 7 - Infinite Depths  (actual streaming manager)
    // =====================================================================
    {
        FVector Pos(Spacing * 6, 0, 0);
        FTransform T(FRotator::ZeroRotator, Pos);
        auto* Manager = World->SpawnActorDeferred<AIOCStreamingManager>(
            AIOCStreamingManager::StaticClass(), T, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (Manager)
        {
#if WITH_EDITOR
            Manager->SetActorLabel(TEXT("IOC_Showcase_7_StreamingManager"));
#endif
            Manager->ChunkSize = FVector(2000, 2000, 1500);
            Manager->StreamRadius = 2;
            Manager->UnloadDistanceBias = 1.2f;
            Manager->bAutoCouplePlayerAtStart = false;
            Manager->CavePreset = EIOCCavePreset::Custom;
            Manager->BaseSeed = 1337;
            Manager->VoxelSize = 80.0;
            Manager->NoiseFrequency = 0.005f;
            Manager->NoiseThreshold = 0.48f;
            Manager->SmoothIterations = 3;
            Manager->bGenerateTunnel = true;
            Manager->TunnelRadius = 400.0f;
            Manager->WallThickness = 120.0f;
            Manager->bEnableLOD = true;
            Manager->LODDistance = 5000.0f;
            Manager->LODVoxelSizeMultiplier = 3.0f;
            Manager->TextureTiling = 0.005f;
            if (CaveMat) Manager->SharedMaterial = CaveMat;

            Manager->FinishSpawning(T);
            Manager->SetActorTickEnabled(false);
            Manager->RebuildAroundLocation(Pos);
            ShowcaseState::SpawnedActors.Add(Manager);
            SpawnLight(Pos, FLinearColor(0.15f, 0.25f, 1.0f), 10.0f, 5000.0f);
            ShowcaseState::Sections.Add({
                Pos + FVector(0, 0, CamH),
                TEXT("Streaming Manager"),
                TEXT("AIOCStreamingManager loads and unloads cave chunks around a location using seamless world-space noise."),
                TEXT("Proof: 5 x 5 streamed preview grid, 80 cm voxels, per-chunk seed hashing, LOD enabled."),
                FColor(40, 70, 255)
            });
        }
    }

    // =====================================================================
    // Section 8 - Performance and bake-ready result
    // =====================================================================
    {
        FVector Pos(Spacing * 7, 0, 0);
        FTransform T(FRotator::ZeroRotator, Pos);
        AIOCProceduralActor* MetricsActor = World->SpawnActorDeferred<AIOCProceduralActor>(
            AIOCProceduralActor::StaticClass(), T, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (MetricsActor)
        {
#if WITH_EDITOR
            MetricsActor->SetActorLabel(TEXT("IOC_Showcase_8_BakeReadyMesh"));
#endif
            MetricsActor->CavePreset = EIOCCavePreset::LargeTunnel;
            MetricsActor->ApplyPresetSettingsOnly();
            MetricsActor->CavePreset = EIOCCavePreset::Custom;
            MetricsActor->GenerationBounds = FVector(3600, 2800, 1800);
            MetricsActor->VoxelSize = 60.0;
            MetricsActor->bGenerateTunnel = true;
            MetricsActor->TunnelRadius = 450.0f;
            MetricsActor->WallThickness = 160.0f;
            MetricsActor->TunnelStart = FVector(0, 0, 0);
            MetricsActor->TunnelEnd = FVector(2800, 1000, 500);
            MetricsActor->bEnableLOD = true;
            MetricsActor->LODDistance = 3500.0f;
            MetricsActor->LODVoxelSizeMultiplier = 3.0f;
            MetricsActor->TextureTiling = 0.005f;
            MetricsActor->bGenerateSmartColors = true;
            if (CaveMat) MetricsActor->CaveMaterial = CaveMat;
            IOC_FinishProceduralSpawn(MetricsActor, T, World);
            ShowcaseState::SpawnedActors.Add(MetricsActor);
            ShowcaseState::PendingActors.Add(MetricsActor);
            SpawnLight(Pos + FVector(1400, 500, 0), FLinearColor(0.9f, 0.95f, 1.0f), 7.0f);
            ShowcaseState::Sections.Add({
                Pos + FVector(1400, 500, CamH),
                TEXT("Performance & Bake"),
                TEXT("Generated caves expose live timing, voxel, triangle, LOD, and scatter counts; final meshes bake to Static Mesh assets."),
                TEXT("Proof: live metrics update here after async completion. Editor bake writes /Game/IOC_Baked from this actor."),
                FColor(120, 220, 255)
            });
            ShowcaseState::Sections.Last().MetricsActor = MetricsActor;
        }
    }

    // =====================================================================
    // Camera — automated cinematic flythrough
    // =====================================================================
    if (ShowcaseState::Sections.Num() > 0)
    {
        ACameraActor* Cam = World->SpawnActor<ACameraActor>(
            ShowcaseState::Sections[0].CameraPos, FRotator(-10.0f, 0.0f, 0.0f), SP);
        if (Cam)
        {
#if WITH_EDITOR
            Cam->SetActorLabel(TEXT("IOC_Showcase_Camera"));
#endif
            Cam->GetCameraComponent()->SetFieldOfView(90.0f);
            ShowcaseState::Camera = Cam;
            ShowcaseState::SpawnedActors.Add(Cam);

            // Possess camera in PIE / game
            if (World->IsPlayInEditor() || World->IsGameWorld())
            {
                if (APlayerController* PC = World->GetFirstPlayerController())
                {
                    PC->SetViewTargetWithBlend(Cam, 1.0f);
                }
            }
        }

        CreateShowcaseCaptionOverlay();
        UpdateShowcaseCaption(0);

        // Readiness guard — defer camera movement until all caves finish generating
        ShowcaseState::ReadinessElapsed = 0.0f;
        ShowcaseState::bCameraStarted = false;
        if (ShowcaseState::PendingActors.Num() == 0)
        {
            StartShowcaseCamera();
        }
        else
        {
            ShowcaseState::ReadinessHandle = FTSTicker::GetCoreTicker().AddTicker(
                FTickerDelegate::CreateStatic(&TickReadinessGuard));
        }
    }

    UE_LOG(LogIOC, Log, TEXT("Showcase Demo spawned - %d sections, %d caves pending generation."),
        ShowcaseState::Sections.Num(), ShowcaseState::PendingActors.Num());
    return ShowcaseState::Sections.Num() > 0;
}

static void SpawnTunnelDemoCmd(const TArray<FString>& Args)
{
    FInstantOrganicCavesModule::SpawnTunnelDemo();
}

static void SpawnSpectacularDemoCmd(const TArray<FString>& Args)
{
    FInstantOrganicCavesModule::SpawnSpectacularDemo();
}

static void SpawnShowcaseDemoCmd(const TArray<FString>& Args)
{
    FIOCShowcaseOptions Options;
    Options.bCaptureMode = false;
    Options.bShowCaptions = !Args.Contains(TEXT("NoCaptions"));
    FInstantOrganicCavesModule::SpawnShowcase(Options);
}

static void SpawnShowcaseCaptureCmd(const TArray<FString>& Args)
{
    FIOCShowcaseOptions Options;
    Options.bCaptureMode = true;
    Options.bShowCaptions = !Args.Contains(TEXT("NoCaptions"));
    Options.bLoop = false;
    FInstantOrganicCavesModule::SpawnShowcase(Options);
}

static void ClearShowcaseDemoCmd(const TArray<FString>& Args)
{
    FInstantOrganicCavesModule::ClearShowcase();
}

static void ClearAllDemosCmd(const TArray<FString>& Args)
{
    FInstantOrganicCavesModule::ClearAllDemos();
}

bool FInstantOrganicCavesModule::SpawnTunnelDemo(UWorld* World)
{
    UWorld* TargetWorld = IOC_ResolveTargetWorld(World);
    const bool bSpawned = SpawnTunnelDemoInternal(TargetWorld);

#if WITH_EDITOR
    if (!bSpawned && !TargetWorld)
    {
        IOC_ShowNotification(
            INVTEXT("Open a level or start Play-In-Editor before spawning an IOC tunnel demo."),
            SNotificationItem::CS_Fail,
            5.0f);
    }
#endif

    return bSpawned;
}

bool FInstantOrganicCavesModule::SpawnSpectacularDemo(UWorld* World)
{
    UWorld* TargetWorld = IOC_ResolveTargetWorld(World);
    const bool bSpawned = SpawnSpectacularDemoInternal(TargetWorld);

#if WITH_EDITOR
    if (!bSpawned && !TargetWorld)
    {
        IOC_ShowNotification(
            INVTEXT("Open a level or start Play-In-Editor before spawning the IOC spectacular demo."),
            SNotificationItem::CS_Fail,
            5.0f);
    }
#endif

    return bSpawned;
}

bool FInstantOrganicCavesModule::SpawnShowcase(const FIOCShowcaseOptions& Options, UWorld* World)
{
    UWorld* TargetWorld = IOC_ResolveTargetWorld(World);
    const bool bSpawned = SpawnShowcaseDemoInternal(TargetWorld, Options);

#if WITH_EDITOR
    if (!bSpawned && !TargetWorld)
    {
        IOC_ShowNotification(
            INVTEXT("Open a level or start Play-In-Editor before launching the IOC showcase."),
            SNotificationItem::CS_Fail,
            5.0f);
    }
#endif

    return bSpawned;
}

bool FInstantOrganicCavesModule::ClearShowcase(UWorld* World)
{
    return ClearShowcaseDemoInternal(IOC_ResolveTargetWorld(World));
}

int32 FInstantOrganicCavesModule::ClearAllDemos(UWorld* World)
{
    UWorld* TargetWorld = IOC_ResolveTargetWorld(World);

    // The showcase owns its own actor list and camera/viewport restore, so run it first.
    ClearShowcaseDemoInternal(TargetWorld);

    int32 Removed = 0;
    for (TWeakObjectPtr<AActor>& WeakActor : GIOCDemoActors)
    {
        if (AActor* Actor = WeakActor.Get())
        {
            Actor->Destroy();
            ++Removed;
        }
    }
    GIOCDemoActors.Reset();

    // Also sweep by tag: a demo spawned before an editor restart is no longer in the array,
    // but the tag survives on the actor, and leaving it behind is the bug being fixed.
    if (TargetWorld)
    {
        TArray<AActor*> Stragglers;
        for (TActorIterator<AActor> It(TargetWorld); It; ++It)
        {
            if (IsValid(*It) && It->Tags.Contains(IOCDemoActorTag))
            {
                Stragglers.Add(*It);
            }
        }
        for (AActor* Actor : Stragglers)
        {
            Actor->Destroy();
            ++Removed;
        }
    }

    UE_LOG(LogIOC, Log, TEXT("ClearAllDemos removed %d demo actor(s)."), Removed);
    return Removed;
}

void FInstantOrganicCavesModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; 
    // the exact timing is specified in the .uplugin file per-module

#if !UE_BUILD_SHIPPING
    IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("IOC.SpawnTunnelDemo"),
        TEXT("Spawns an Instant Organic Caves Tunnel Demo actor."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&SpawnTunnelDemoCmd),
        ECVF_Cheat
    );

    GIOCWorldCleanupHandle = FWorldDelegates::OnPostWorldCleanup.AddStatic(&ShowcaseWorldCleanupHandler);

    IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("IOC.SpawnSpectacular"),
        TEXT("Spawns a Spectacular Alien Hive Demo."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&SpawnSpectacularDemoCmd),
        ECVF_Cheat
    );

    IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("IOC.SpawnShowcase"),
        TEXT("Spawns an automated showcase demo: 8 cave sections with cinematic camera flythrough."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&SpawnShowcaseDemoCmd),
        ECVF_Cheat
    );

    IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("IOC.SpawnShowcaseCapture"),
        TEXT("Spawns the capture-ready IOC showcase: 8 sections, polished captions, no debug HUD."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&SpawnShowcaseCaptureCmd),
        ECVF_Cheat
    );

    IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("IOC.ClearShowcase"),
        TEXT("Clears the IOC showcase flythrough and restores the camera. Does NOT remove the tunnel or spectacular demos -- use IOC.ClearAllDemos for that."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&ClearShowcaseDemoCmd),
        ECVF_Cheat
    );

    IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("IOC.ClearAllDemos"),
        TEXT("Removes every actor the IOC demo commands spawned: showcase, tunnel demo, spectacular demo and the demo character."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&ClearAllDemosCmd),
        ECVF_Cheat
    );
#endif

}

void FInstantOrganicCavesModule::ShutdownModule()
{
    // This function may be called during shutdown to clean up your module.
    // For modules that support dynamic reloading, we call this function before unloading the module.

    // Clear development showcase state and remove static delegates before unload.
#if !UE_BUILD_SHIPPING
    ClearShowcase();
    if (GIOCWorldCleanupHandle.IsValid())
    {
        FWorldDelegates::OnPostWorldCleanup.Remove(GIOCWorldCleanupHandle);
        GIOCWorldCleanupHandle.Reset();
    }
#endif

#if !UE_BUILD_SHIPPING
    if (IConsoleObject* Cmd = IConsoleManager::Get().FindConsoleObject(TEXT("IOC.SpawnTunnelDemo")))
    {
        IConsoleManager::Get().UnregisterConsoleObject(Cmd);
    }

    if (IConsoleObject* Cmd = IConsoleManager::Get().FindConsoleObject(TEXT("IOC.SpawnSpectacular")))
    {
        IConsoleManager::Get().UnregisterConsoleObject(Cmd);
    }

    if (IConsoleObject* Cmd = IConsoleManager::Get().FindConsoleObject(TEXT("IOC.SpawnShowcase")))
    {
        IConsoleManager::Get().UnregisterConsoleObject(Cmd);
    }

    if (IConsoleObject* Cmd = IConsoleManager::Get().FindConsoleObject(TEXT("IOC.SpawnShowcaseCapture")))
    {
        IConsoleManager::Get().UnregisterConsoleObject(Cmd);
    }

    if (IConsoleObject* Cmd = IConsoleManager::Get().FindConsoleObject(TEXT("IOC.ClearShowcase")))
    {
        IConsoleManager::Get().UnregisterConsoleObject(Cmd);
    }

    if (IConsoleObject* Cmd = IConsoleManager::Get().FindConsoleObject(TEXT("IOC.ClearAllDemos")))
    {
        IConsoleManager::Get().UnregisterConsoleObject(Cmd);
    }
#endif

}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FInstantOrganicCavesModule, InstantOrganicCaves)
