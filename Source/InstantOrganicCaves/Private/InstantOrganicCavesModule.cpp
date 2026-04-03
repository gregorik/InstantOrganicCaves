// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "InstantOrganicCavesModule.h"
#include "Elements/IOCVoxelCore.h" // Relative path matches Public folder structure
#include "Data/PCGPointData.h"

// --- Demo Spawning ---
#include "IOCProceduralActor.h"
#include "IOCCharacter.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Materials/Material.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/PlayerController.h"

// Environment
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/StaticMesh.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "TimerManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#endif

#define LOCTEXT_NAMESPACE "FInstantOrganicCavesModule"

template<typename T>
T* EnsureActor(UWorld* World)
{
    for (TActorIterator<T> It(World); It; ++It)
    {
        return *It;
    }
    return World->SpawnActor<T>();
}

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

static void SpawnTunnelDemoCmd(const TArray<FString>& Args)
{
    UWorld* World = nullptr;

#if WITH_EDITOR
    if (GEditor)
    {
        World = GEditor->GetEditorWorldContext().World();
    }
#endif
    if (!World && GEngine)
    {
        World = GEngine->GetWorld();
    }

    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("IOC: No World found to spawn demo!"));
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // --- Environment Setup ---
    // Sun
    ADirectionalLight* Sun = EnsureActor<ADirectionalLight>(World);
    if (Sun)
    {
#if WITH_EDITOR
        Sun->SetActorLabel(TEXT("IOC_Sun"));
#endif
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
    ASkyLight* Sky = EnsureActor<ASkyLight>(World);
    if (Sky)
    {
#if WITH_EDITOR
        Sky->SetActorLabel(TEXT("IOC_SkyLight"));
#endif
        Sky->GetLightComponent()->SetRealTimeCapture(true);
    }

    // Fog
    AExponentialHeightFog* Fog = EnsureActor<AExponentialHeightFog>(World);
    if (Fog)
    {
#if WITH_EDITOR
        Fog->SetActorLabel(TEXT("IOC_Fog"));
#endif
        Fog->GetComponent()->SetFogDensity(0.02f);
        Fog->GetComponent()->SetFogHeightFalloff(0.2f);
        Fog->GetComponent()->SetStartDistance(0.0f);
    }

    // PPV
    APostProcessVolume* PPV = EnsureActor<APostProcessVolume>(World);
    if (PPV)
    {
#if WITH_EDITOR
        PPV->SetActorLabel(TEXT("IOC_PostProcess"));
#endif
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
#if WITH_EDITOR
        DemoActor->SetActorLabel(TEXT("IOC_Tunnel_Demo"));
#endif
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
        UE_LOG(LogTemp, Log, TEXT("IOC: Tunnel Mesh Generation Started (Async)!"));
    }

    // 3. Spawn Character
    // Raise start slightly to avoid stuck
    FVector CharSpawnLoc = StartLoc + FVector(0, 0, 150); 
    AIOCCharacter* PlayerChar = World->SpawnActor<AIOCCharacter>(CharSpawnLoc, FRotator::ZeroRotator, SpawnParams);
    
    if (PlayerChar)
    {
#if WITH_EDITOR
        PlayerChar->SetActorLabel(TEXT("IOC_Player"));
#endif
        // Auto Possess if PIE
        if (World->IsPlayInEditor())
        {
            APlayerController* PC = World->GetFirstPlayerController();
            if (PC)
            {
                PC->Possess(PlayerChar);
            }
        }
        
        UE_LOG(LogTemp, Log, TEXT("IOC: Player Spawned!"));
    }
}

static void SpawnSpectacularDemoCmd(const TArray<FString>& Args)
{
    UE_LOG(LogTemp, Log, TEXT("IOC: Attempting to spawn Spectacular Crystal Demo..."));

    UWorld* World = nullptr;
#if WITH_EDITOR
    if (GEditor)
    {
        World = GEditor->PlayWorld; // Prioritize PIE World if active
        if (!World) World = GEditor->GetEditorWorldContext().World();
    }
#endif
    if (!World && GEngine) World = GEngine->GetWorld();
    
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("IOC: Failed. No valid World found."));
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // --- Environment Setup ---
    ADirectionalLight* Sun = EnsureActor<ADirectionalLight>(World);
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
    
    ASkyLight* Sky = EnsureActor<ASkyLight>(World);
    if (Sky) Sky->GetLightComponent()->SetRealTimeCapture(true);
    
    AExponentialHeightFog* Fog = EnsureActor<AExponentialHeightFog>(World);
    if (Fog)
    {
        Fog->GetComponent()->SetFogDensity(0.04f);
        Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(0.1f, 0.05f, 0.2f)); // Purple/blue magical fog
    }
    
    APostProcessVolume* PPV = EnsureActor<APostProcessVolume>(World);
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
#if WITH_EDITOR
        DemoActor->SetActorLabel(TEXT("IOC_Spectacular_Demo"));
#endif
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

        // Add Fab Decorations
        auto AddDecor = [&](const TCHAR* Path, float Density, float ScaleMin, float ScaleMax, bool bAlign, float MinZ, float MaxZ) {
            if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path)) {
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
        AddDecor(TEXT("/Game/Fab/Magic_Crystal__game_ready_/magic_crystal_game_ready/StaticMeshes/magic_crystal_game_ready.magic_crystal_game_ready"), 0.4f, 1.0f, 4.0f, true, -0.5f, 1.0f);
        // Geodes clustered
        AddDecor(TEXT("/Game/Fab/Geode/geode/StaticMeshes/geode.geode"), 0.1f, 3.0f, 8.0f, true, -1.0f, 1.0f);
        // Free Magic Gemme
        AddDecor(TEXT("/Game/Fab/__FREE___Magic_Gemme/free_magic_gemme/StaticMeshes/free_magic_gemme.free_magic_gemme"), 0.3f, 1.0f, 3.0f, true, -0.2f, 1.0f);
        // Stylized Crystals
        AddDecor(TEXT("/Game/Fab/Stylized_Crystals/stylized_crystals/StaticMeshes/stylized_crystals.stylized_crystals"), 0.2f, 1.0f, 3.0f, true, -0.8f, 1.0f);
        // Mossy Rocks on floor
        AddDecor(TEXT("/Game/Fab/Mossy_Rock/mossy_rock/StaticMeshes/mossy_rock.mossy_rock"), 0.6f, 1.0f, 2.5f, false, 0.7f, 1.0f);
        // River Rocks everywhere
        AddDecor(TEXT("/Game/Fab/River_Rock/river_rock/StaticMeshes/river_rock.river_rock"), 1.2f, 0.5f, 1.5f, true, -0.2f, 1.0f);
        
        // Finish Spawning (Triggers BeginPlay -> GenerateCave)
        IOC_FinishProceduralSpawn(DemoActor, SpawnTransform, World);

        UE_LOG(LogTemp, Log, TEXT("IOC: Spectacular Crystal Cave Generation Started! VoxelSize: %f"), DemoActor->VoxelSize);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("IOC: Failed to spawn DemoActor."));
    }

    // Spawn Player
    FVector StartLoc(0, 0, 150);
    AIOCCharacter* PlayerChar = World->SpawnActor<AIOCCharacter>(StartLoc, FRotator::ZeroRotator, SpawnParams);
    if (PlayerChar)
    {
#if WITH_EDITOR
        PlayerChar->SetActorLabel(TEXT("IOC_Player"));
#endif
        // Add a cool light to player to explore the crystal cave
        UPointLightComponent* Flashlight = NewObject<UPointLightComponent>(PlayerChar);
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
        FColor HUDColor;
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

#if WITH_EDITOR
    static FVector SavedEditorViewLocation = FVector::ZeroVector;
    static FRotator SavedEditorViewRotation = FRotator::ZeroRotator;
    static bool bSavedEditorRealtime = false;
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
        EditorTickerHandle.Reset();
#if WITH_EDITOR
        SavedEditorViewLocation = FVector::ZeroVector;
        SavedEditorViewRotation = FRotator::ZeroRotator;
        bSavedEditorRealtime = false;
        bHasSavedEditorViewState = false;
#endif
    }
}

#if WITH_EDITOR
static FLevelEditorViewportClient* FindShowcaseEditorViewport()
{
    if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->IsPerspective())
    {
        return GCurrentLevelEditingViewportClient;
    }

    if (!GEditor)
    {
        return nullptr;
    }

    for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
    {
        if (ViewportClient && ViewportClient->IsPerspective())
        {
            return ViewportClient;
        }
    }

    return nullptr;
}

static void CaptureShowcaseEditorViewportState()
{
    FLevelEditorViewportClient* ViewportClient = FindShowcaseEditorViewport();
    if (!ViewportClient)
    {
        return;
    }

    ShowcaseState::SavedEditorViewLocation = ViewportClient->GetViewLocation();
    ShowcaseState::SavedEditorViewRotation = ViewportClient->GetViewRotation();
    ShowcaseState::bSavedEditorRealtime = ViewportClient->IsRealtime();
    ShowcaseState::bHasSavedEditorViewState = true;
    ViewportClient->SetRealtime(true);
}

static void RestoreShowcaseEditorViewportState()
{
    if (!ShowcaseState::bHasSavedEditorViewState)
    {
        return;
    }

    FLevelEditorViewportClient* ViewportClient = FindShowcaseEditorViewport();
    if (!ViewportClient)
    {
        return;
    }

    ViewportClient->SetViewLocation(ShowcaseState::SavedEditorViewLocation);
    ViewportClient->SetViewRotation(ShowcaseState::SavedEditorViewRotation);
    ViewportClient->SetRealtime(ShowcaseState::bSavedEditorRealtime);
    ViewportClient->Invalidate();
}

static void ApplyShowcaseEditorViewport(const FVector& Pos, const FRotator& Rot)
{
    FLevelEditorViewportClient* ViewportClient = FindShowcaseEditorViewport();
    if (!ViewportClient)
    {
        return;
    }

    ViewportClient->SetViewLocation(Pos);
    ViewportClient->SetViewRotation(Rot);
    ViewportClient->Invalidate();
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
        const float Time = FMath::Fmod(ShowcaseState::ElapsedTime, TotalDuration);

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

    // HUD
    if (GEngine)
    {
        const auto& Cur = ShowcaseState::Sections[Idx];
        GEngine->AddOnScreenDebugMessage(9000, 0.08f, Cur.HUDColor,
            FString::Printf(TEXT("=== %s ==="), *Cur.Name));
        GEngine->AddOnScreenDebugMessage(9001, 0.08f, FColor::Silver, Cur.Description);
        GEngine->AddOnScreenDebugMessage(9002, 0.08f, FColor(100, 100, 100),
            FString::Printf(TEXT("[%d / %d]"), Idx + 1, NumSections));
    }
}

static void TickShowcaseCamera()
{
    AdvanceShowcaseCamera(ShowcaseState::TimerRate);
}

#if WITH_EDITOR
static bool TickShowcaseCameraEditor(float DeltaSeconds)
{
    AdvanceShowcaseCamera(DeltaSeconds);
    return ShowcaseState::Camera.IsValid() && ShowcaseState::Sections.Num() > 0;
}
#endif

static void ClearShowcaseDemoCmd(const TArray<FString>& Args)
{
    UWorld* World = nullptr;
#if WITH_EDITOR
    if (GEditor)
    {
        World = GEditor->PlayWorld;
        if (!World) World = GEditor->GetEditorWorldContext().World();
    }
#endif
    if (!World && GEngine) World = GEngine->GetWorld();

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

    ShowcaseState::Reset();

    UE_LOG(LogTemp, Log, TEXT("IOC: Showcase Demo cleared."));
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow, TEXT("IOC Showcase Demo cleared."));
    }
}

static void SpawnShowcaseDemoCmd(const TArray<FString>& Args)
{
    UE_LOG(LogTemp, Log, TEXT("IOC: Spawning Showcase Demo..."));

    UWorld* World = nullptr;
#if WITH_EDITOR
    if (GEditor)
    {
        World = GEditor->PlayWorld;
        if (!World) World = GEditor->GetEditorWorldContext().World();
    }
#endif
    if (!World && GEngine) World = GEngine->GetWorld();

    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("IOC: No World found! Use Play-In-Editor for best results."));
        return;
    }

    // Clear any prior showcase
    ClearShowcaseDemoCmd(TArray<FString>());
    ShowcaseState::World = World;

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

    const float Spacing = 8000.0f;
    const float CamH = 400.0f;

    // --- Helper: spawn a section point light ---
    auto SpawnLight = [&](FVector Loc, FLinearColor Color, float Intensity = 8.0f, float Radius = 4000.0f)
    {
        APointLight* L = World->SpawnActor<APointLight>(Loc + FVector(0, 0, 500), FRotator::ZeroRotator, SP);
        if (L)
        {
            if (UPointLightComponent* PLC = Cast<UPointLightComponent>(L->GetLightComponent()))
            {
                PLC->SetIntensity(Intensity);
                PLC->SetAttenuationRadius(Radius);
                PLC->SetLightColor(Color);
            }
            ShowcaseState::SpawnedActors.Add(L);
        }
    };

    // --- Helper: add a decoration scatter layer ---
    auto AddDecor = [](AIOCProceduralActor* A, const TCHAR* Path, float Density,
        float ScaleMin, float ScaleMax, bool bAlign, float MinZ, float MaxZ, float Poisson = 0.0f)
    {
        if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path))
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
    ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
        FVector::ZeroVector, FRotator(-55.0f, -30.0f, 0.0f), SP);
    if (Sun)
    {
#if WITH_EDITOR
        Sun->SetActorLabel(TEXT("IOC_Showcase_Sun"));
#endif
        if (UDirectionalLightComponent* DLC = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
        {
            DLC->SetIntensity(4.0f);
            DLC->CastShadows = true;
        }
        ShowcaseState::SpawnedActors.Add(Sun);
    }

    // Sky
    ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
    if (Sky)
    {
#if WITH_EDITOR
        Sky->SetActorLabel(TEXT("IOC_Showcase_Sky"));
#endif
        Sky->GetLightComponent()->SetRealTimeCapture(true);
        Sky->GetLightComponent()->SetIntensity(0.5f);
        ShowcaseState::SpawnedActors.Add(Sky);
    }

    // Fog
    AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(
        FVector::ZeroVector, FRotator::ZeroRotator, SP);
    if (Fog)
    {
#if WITH_EDITOR
        Fog->SetActorLabel(TEXT("IOC_Showcase_Fog"));
#endif
        Fog->GetComponent()->SetFogDensity(0.012f);
        Fog->GetComponent()->SetFogHeightFalloff(0.15f);
        Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(0.08f, 0.1f, 0.18f));
        ShowcaseState::SpawnedActors.Add(Fog);
    }

    // Post-Process
    APostProcessVolume* PPV = World->SpawnActor<APostProcessVolume>(
        FVector::ZeroVector, FRotator::ZeroRotator, SP);
    if (PPV)
    {
#if WITH_EDITOR
        PPV->SetActorLabel(TEXT("IOC_Showcase_PostProcess"));
#endif
        PPV->bUnbound = true;
        PPV->Settings.bOverride_AutoExposureMinBrightness = true;
        PPV->Settings.AutoExposureMinBrightness = 0.02f;
        PPV->Settings.bOverride_AutoExposureMaxBrightness = true;
        PPV->Settings.AutoExposureMaxBrightness = 1.5f;
        PPV->Settings.bOverride_BloomIntensity = true;
        PPV->Settings.BloomIntensity = 1.5f;
        PPV->Settings.bOverride_ColorSaturation = true;
        PPV->Settings.ColorSaturation = FVector4(1.15f, 1.1f, 1.2f, 1.0f);
        ShowcaseState::SpawnedActors.Add(PPV);
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
        }
        SpawnLight(Pos, FLinearColor(1.0f, 0.7f, 0.3f));
        ShowcaseState::Sections.Add({
            Pos + FVector(2000, 2000, CamH + 800),
            TEXT("Organic Cavern"),
            TEXT("Cellular Automata — random fill + smoothing iterations carve natural cave shapes"),
            FColor(255, 180, 80)
        });
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
        }
        SpawnLight(Pos + FVector(1500, 750, 0), FLinearColor(0.3f, 0.5f, 1.0f));
        ShowcaseState::Sections.Add({
            Pos + FVector(1500, 750, CamH),
            TEXT("Sculpted Tunnel"),
            TEXT("Perlin Tunnel — noise-deformed passages along a path with thick organic walls"),
            FColor(80, 130, 255)
        });
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
        }
        SpawnLight(Pos + FVector(1500, 1000, 0), FLinearColor(0.2f, 1.0f, 0.3f));
        ShowcaseState::Sections.Add({
            Pos + FVector(1500, 1000, CamH),
            TEXT("Alien Hive"),
            TEXT("Domain Warp — procedural space distortion creates biomechanical geometry"),
            FColor(60, 255, 80)
        });
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
        }
        SpawnLight(Pos + FVector(1500, 750, 0), FLinearColor(1.0f, 0.5f, 0.15f));
        ShowcaseState::Sections.Add({
            Pos + FVector(1500, 750, CamH),
            TEXT("Canyon Strata"),
            TEXT("Terrace Steps — dramatic layered geology with stepped, stratified surfaces"),
            FColor(255, 130, 40)
        });
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

            // 6 decoration layers
            AddDecor(A, TEXT("/Game/Fab/Magic_Crystal__game_ready_/magic_crystal_game_ready/StaticMeshes/magic_crystal_game_ready.magic_crystal_game_ready"),
                0.4f, 1.0f, 4.0f, true, -0.5f, 1.0f);
            AddDecor(A, TEXT("/Game/Fab/Geode/geode/StaticMeshes/geode.geode"),
                0.1f, 3.0f, 8.0f, true, -1.0f, 1.0f);
            AddDecor(A, TEXT("/Game/Fab/__FREE___Magic_Gemme/free_magic_gemme/StaticMeshes/free_magic_gemme.free_magic_gemme"),
                0.3f, 1.0f, 3.0f, true, -0.2f, 1.0f);
            AddDecor(A, TEXT("/Game/Fab/Stylized_Crystals/stylized_crystals/StaticMeshes/stylized_crystals.stylized_crystals"),
                0.2f, 1.0f, 3.0f, true, -0.8f, 1.0f);
            AddDecor(A, TEXT("/Game/Fab/Mossy_Rock/mossy_rock/StaticMeshes/mossy_rock.mossy_rock"),
                0.6f, 1.0f, 2.5f, false, 0.7f, 1.0f);
            AddDecor(A, TEXT("/Game/Fab/River_Rock/river_rock/StaticMeshes/river_rock.river_rock"),
                1.2f, 0.5f, 1.5f, true, -0.2f, 1.0f);

            IOC_FinishProceduralSpawn(A, T, World);
            ShowcaseState::SpawnedActors.Add(A);
        }
        SpawnLight(Pos + FVector(1500, 750, 0), FLinearColor(0.6f, 0.2f, 1.0f), 10.0f);
        SpawnLight(Pos + FVector(3000, 1000, 0), FLinearColor(0.2f, 0.4f, 1.0f), 6.0f);
        ShowcaseState::Sections.Add({
            Pos + FVector(2000, 750, CamH),
            TEXT("Crystal Gallery"),
            TEXT("Scatter Layers — 6 decoration types with slope filtering, Poisson separation & async placement"),
            FColor(160, 80, 255)
        });
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

            // Sphere carve
            auto* Sphere = NewObject<UIOCCarvingComponent>(A);
            if (Sphere)
            {
                Sphere->RegisterComponent();
                Sphere->AttachToComponent(A->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
                Sphere->SetRelativeLocation(FVector(1000, 500, 300));
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
                Capsule->ShapeType = EIOCCarvingShape::Capsule;
                Capsule->CapsuleRadius = 200.0f;
                Capsule->CapsuleHalfHeight = 350.0f;
                Capsule->FalloffRadius = 80.0f;
            }

            IOC_FinishProceduralSpawn(A, T, World);
            ShowcaseState::SpawnedActors.Add(A);
        }
        SpawnLight(Pos + FVector(1500, 1000, 0), FLinearColor(1.0f, 0.25f, 0.15f));
        ShowcaseState::Sections.Add({
            Pos + FVector(1500, 1000, CamH),
            TEXT("Carved Chamber"),
            TEXT("Carving Volumes — sphere, box & capsule shapes with smoothstep falloff for artist-directed openings"),
            FColor(255, 70, 40)
        });
    }

    // =====================================================================
    // Section 7 — Infinite Depths  (Streaming — simulated with tiled chunks)
    // =====================================================================
    {
        FVector Pos(Spacing * 6, 0, 0);
        const float ChunkSz = 2000.0f;
        for (int32 dy = -1; dy <= 1; ++dy)
        {
            for (int32 dx = -1; dx <= 1; ++dx)
            {
                FVector ChunkPos = Pos + FVector(dx * ChunkSz, dy * ChunkSz, 0);
                FTransform T(FRotator::ZeroRotator, ChunkPos);
                auto* C = World->SpawnActorDeferred<AIOCProceduralActor>(
                    AIOCProceduralActor::StaticClass(), T, nullptr, nullptr,
                    ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
                if (C)
                {
#if WITH_EDITOR
                    C->SetActorLabel(*FString::Printf(TEXT("IOC_Showcase_7_Chunk_%d_%d"), dx + 1, dy + 1));
#endif
                    C->CavePreset = EIOCCavePreset::Custom;
                    C->bUseWorldSpaceNoise = true;
                    C->GenerationBounds = FVector(ChunkSz, ChunkSz, 1500);
                    C->VoxelSize = 80.0;
                    C->NoiseFrequency = 0.005f;
                    C->NoiseThreshold = 0.48f;
                    C->SmoothIterations = 3;
                    C->CaveSeed = 1337 ^ (dx * 73856093) ^ (dy * 19349663);
                    C->bGenerateTunnel = true;
                    C->TunnelRadius = 400.0f;
                    C->WallThickness = 100.0f;
                    C->TunnelStart = FVector(0, 0, 0);
                    C->TunnelEnd = FVector(ChunkSz, 0, 0);
                    C->TextureTiling = 0.005f;
                    C->bGenerateSmartColors = true;
                    if (CaveMat) C->CaveMaterial = CaveMat;
                    IOC_FinishProceduralSpawn(C, T, World);
                    ShowcaseState::SpawnedActors.Add(C);
                }
            }
        }
        SpawnLight(Pos, FLinearColor(0.15f, 0.25f, 1.0f), 10.0f, 5000.0f);
        ShowcaseState::Sections.Add({
            Pos + FVector(0, 0, CamH),
            TEXT("Infinite Depths"),
            TEXT("Streaming System — seamless world-space noise across chunk boundaries for infinite caves"),
            FColor(40, 70, 255)
        });
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

        ShowcaseState::ElapsedTime = 0.0f;
        ShowcaseState::StartDelayRemaining = ShowcaseState::InitialDelay;

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
                ShowcaseState::TimerRate, true, ShowcaseState::InitialDelay);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("IOC: Showcase Demo spawned — %d sections, flythrough starts in %.1f seconds."),
        ShowcaseState::Sections.Num(), ShowcaseState::InitialDelay);
    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green,
            TEXT("IOC Showcase Demo — Generating caves, flythrough starting..."));
        GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Yellow,
            TEXT("Use 'IOC.ClearShowcase' to clean up.  Best in Play-In-Editor mode."));
    }
}

void FInstantOrganicCavesModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; 
    // the exact timing is specified in the .uplugin file per-module

    IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("IOC.SpawnTunnelDemo"),
        TEXT("Spawns an Instant Organic Caves Tunnel Demo actor."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&SpawnTunnelDemoCmd),
        ECVF_Default
    );

    IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("IOC.SpawnSpectacular"),
        TEXT("Spawns a Spectacular Alien Hive Demo."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&SpawnSpectacularDemoCmd),
        ECVF_Default
    );

    IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("IOC.SpawnShowcase"),
        TEXT("Spawns an automated showcase demo: 7 cave sections with cinematic camera flythrough."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&SpawnShowcaseDemoCmd),
        ECVF_Default
    );

    IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("IOC.ClearShowcase"),
        TEXT("Clears the IOC Showcase Demo and restores camera."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&ClearShowcaseDemoCmd),
        ECVF_Default
    );
}

void FInstantOrganicCavesModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.
    // For modules that support dynamic reloading, we call this function before unloading the module.

    // Clear showcase timer before module unload to prevent dangling callback
    ClearShowcaseDemoCmd(TArray<FString>());

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

    if (IConsoleObject* Cmd = IConsoleManager::Get().FindConsoleObject(TEXT("IOC.ClearShowcase")))
    {
        IConsoleManager::Get().UnregisterConsoleObject(Cmd);
    }
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FInstantOrganicCavesModule, InstantOrganicCaves)
