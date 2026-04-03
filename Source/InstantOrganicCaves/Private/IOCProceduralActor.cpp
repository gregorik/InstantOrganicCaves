// Copyright (c) 2026 GregOrigin. All Rights Reserved.


#include "IOCProceduralActor.h"
#include "IOCBiomeVolume.h"
#include "EngineUtils.h"

// --- CORE API ---
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "UDynamicMesh.h"
#include "Math/RandomStream.h"
#include "GenericPlatform/GenericPlatformMath.h" // For Perlin

#include "UObject/NoExportTypes.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/CollisionProfile.h"
#include "Materials/Material.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "DrawDebugHelpers.h"
#include "NavigationSystem.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/Factory.h"
#include "Engine/StaticMesh.h"
#include "Editor.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#endif

using namespace UE::Geometry;

namespace { struct FIOCQuad { int32 V[4]; }; }
namespace
{
    FORCEINLINE float IOC_Saturate(float Value)
    {
        return FMath::Clamp(Value, 0.0f, 1.0f);
    }

    FORCEINLINE float IOC_Perlin01(const FVector& P)
    {
        return (FMath::PerlinNoise3D(P) * 0.5f) + 0.5f;
    }

    float IOC_Fbm01(const FVector& P, float BaseFrequency, int32 Octaves, float Lacunarity, float Persistence)
    {
        const float SafeFrequency = FMath::Max(BaseFrequency, 0.00001f);
        const int32 SafeOctaves = FMath::Clamp(Octaves, 1, 8);
        const float SafeLacunarity = FMath::Max(Lacunarity, 1.1f);
        const float SafePersistence = FMath::Clamp(Persistence, 0.1f, 0.95f);

        float Total = 0.0f;
        float Amplitude = 1.0f;
        float AmplitudeSum = 0.0f;
        FVector Sample = P * SafeFrequency;

        for (int32 Octave = 0; Octave < SafeOctaves; ++Octave)
        {
            Total += IOC_Perlin01(Sample) * Amplitude;
            AmplitudeSum += Amplitude;
            Sample *= SafeLacunarity;
            Amplitude *= SafePersistence;
        }

        return (AmplitudeSum > SMALL_NUMBER) ? (Total / AmplitudeSum) : 0.5f;
    }

    float IOC_RidgedFbm01(const FVector& P, float BaseFrequency, int32 Octaves, float Lacunarity, float Persistence)
    {
        const float SafeFrequency = FMath::Max(BaseFrequency, 0.00001f);
        const int32 SafeOctaves = FMath::Clamp(Octaves, 1, 8);
        const float SafeLacunarity = FMath::Max(Lacunarity, 1.1f);
        const float SafePersistence = FMath::Clamp(Persistence, 0.1f, 0.95f);

        float Total = 0.0f;
        float Amplitude = 1.0f;
        float AmplitudeSum = 0.0f;
        FVector Sample = P * SafeFrequency;

        for (int32 Octave = 0; Octave < SafeOctaves; ++Octave)
        {
            float Ridged = 1.0f - FMath::Abs(FMath::PerlinNoise3D(Sample));
            Ridged *= Ridged;
            Total += Ridged * Amplitude;
            AmplitudeSum += Amplitude;
            Sample *= SafeLacunarity;
            Amplitude *= SafePersistence;
        }

        return (AmplitudeSum > SMALL_NUMBER) ? (Total / AmplitudeSum) : 0.5f;
    }

    float IOC_BoxEnvelope01(const FVector& LocalP, const FVector& Bounds)
    {
        const FVector HalfExtents = Bounds * 0.5f;
        const float NX = (HalfExtents.X > SMALL_NUMBER) ? (FMath::Abs(LocalP.X) / HalfExtents.X) : 0.0f;
        const float NY = (HalfExtents.Y > SMALL_NUMBER) ? (FMath::Abs(LocalP.Y) / HalfExtents.Y) : 0.0f;
        const float NZ = (HalfExtents.Z > SMALL_NUMBER) ? (FMath::Abs(LocalP.Z) / HalfExtents.Z) : 0.0f;
        const float Edge = 1.0f - FMath::Max(FMath::Max(NX, NY), NZ);
        return IOC_Saturate(Edge);
    }

    UMaterialInterface* IOC_GetFallbackCaveMaterial()
    {
        static TWeakObjectPtr<UMaterialInterface> CachedMaterial;
        if (!CachedMaterial.IsValid())
        {
            CachedMaterial = LoadObject<UMaterialInterface>(nullptr,
                TEXT("/InstantOrganicCaves/MI_IOC_CaveWalls.MI_IOC_CaveWalls"));

            if (!CachedMaterial.IsValid())
            {
                CachedMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
            }
        }
        return CachedMaterial.Get();
    }

    void IOC_ConfigureTunnelSpline(USplineComponent* Spline)
    {
        if (!Spline)
        {
            return;
        }

        // Feed per-instance spline edits into construction-time generation.
        Spline->bInputSplinePointsToConstructionScript = true;
    }

    bool IOC_ShouldFollowTunnelLine(const USplineComponent* Spline)
    {
        return Spline && !Spline->bSplineHasBeenEdited;
    }

    void IOC_SyncSplineToTunnelLine(USplineComponent* Spline, const FVector& TunnelStart, const FVector& TunnelEnd, float TunnelRadius)
    {
        if (!Spline)
        {
            return;
        }

        IOC_ConfigureTunnelSpline(Spline);

        FVector SafeTunnelEnd = TunnelEnd;
        if ((SafeTunnelEnd - TunnelStart).SizeSquared() < KINDA_SMALL_NUMBER)
        {
            SafeTunnelEnd = TunnelStart + FVector(FMath::Max(TunnelRadius * 8.0f, 1000.0f), 0.0f, 0.0f);
        }

        const TArray<FVector> Points = { TunnelStart, SafeTunnelEnd };
        Spline->SetSplinePoints(Points, ESplineCoordinateSpace::Local, false);
        Spline->SetSplinePointType(0, ESplinePointType::Linear, false);
        Spline->SetSplinePointType(1, ESplinePointType::Linear, true);
    }
}

AIOCProceduralActor::AIOCProceduralActor()
{
    PrimaryActorTick.bCanEverTick = true;

    CavePreset = EIOCCavePreset::LargeTunnel;
    bGenerateTunnel = true;
    ApplyPresetParams(); // Set params only — no GenerateCave() during construction

    MeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("ProceduralMesh"));
    RootComponent = MeshComponent;

    MeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
    MeshComponent->SetComplexAsSimpleCollisionEnabled(true, true);

    LODMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("LODMesh"));
    LODMeshComponent->SetupAttachment(RootComponent);
    LODMeshComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    LODMeshComponent->SetVisibility(false);

    CaveSpline = CreateDefaultSubobject<USplineComponent>(TEXT("CaveSpline"));
    CaveSpline->SetupAttachment(RootComponent);
    IOC_ConfigureTunnelSpline(CaveSpline);
    IOC_SyncSplineToTunnelLine(CaveSpline, TunnelStart, TunnelEnd, TunnelRadius);

    // Default cave material — two-sided rock shipped with the plugin
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultCaveMat(
        TEXT("/InstantOrganicCaves/MI_IOC_CaveWalls.MI_IOC_CaveWalls"));
    if (DefaultCaveMat.Succeeded())
    {
        CaveMaterial = DefaultCaveMat.Object;
    }

    ApplyCaveMaterials();
}

void AIOCProceduralActor::ApplyCaveMaterialToComponent(UDynamicMeshComponent* TargetComponent) const
{
    if (!TargetComponent)
    {
        return;
    }

    UMaterialInterface* MaterialToApply = CaveMaterial ? CaveMaterial : IOC_GetFallbackCaveMaterial();

    TargetComponent->SetColorOverrideMode(EDynamicMeshComponentColorOverrideMode::None);
    TargetComponent->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);

    if (MaterialToApply)
    {
        TArray<UMaterialInterface*> Materials;
        Materials.Add(MaterialToApply);
        TargetComponent->ConfigureMaterialSet(Materials, true);
    }
}

void AIOCProceduralActor::ApplyCaveMaterials()
{
    ApplyCaveMaterialToComponent(MeshComponent);
    ApplyCaveMaterialToComponent(LODMeshComponent);
}

void AIOCProceduralActor::BeginPlay()
{
    Super::BeginPlay();

    ApplyCaveMaterials();

    // PIE duplication can copy an in-flight generation flag from the editor world.
    // Reset so BeginPlay can force a fresh generation.
    if (bIsGenerating)
    {
        bIsGenerating = false;
        bIsGeneratingDisplay = false;
        if (bLogPresetDebug)
        {
            UE_LOG(LogTemp, Warning, TEXT("IOC DBG BeginPlay: Reset bIsGenerating copied from editor world."));
        }
    }

    if (bLogPresetDebug)
    {
        UE_LOG(LogTemp, Warning, TEXT("IOC DBG BeginPlay: Preset=%d Force=%d Tunnel=%d Spline=%d Start=%s End=%s Radius=%.1f Wall=%.1f Voxel=%.1f Bounds=%s"),
            (int32)CavePreset, bForcePreset, bGenerateTunnel, bUseSpline,
            *TunnelStart.ToString(), *TunnelEnd.ToString(),
            TunnelRadius, WallThickness, (float)VoxelSize, *GenerationBounds.ToString());
    }

    if (bForcePreset)
    {
        if (CavePreset == EIOCCavePreset::Custom)
        {
            CavePreset = EIOCCavePreset::LargeTunnel;
        }
        ApplyPreset();
        if (bLogPresetDebug)
        {
            UE_LOG(LogTemp, Warning, TEXT("IOC DBG BeginPlay: Applied preset (Force). Preset=%d Tunnel=%d Radius=%.1f Wall=%.1f Voxel=%.1f NFreq=%.6f"),
                (int32)CavePreset, bGenerateTunnel, TunnelRadius, WallThickness, (float)VoxelSize, NoiseFrequency);
        }
        return;
    }

    if (CavePreset != EIOCCavePreset::Custom)
    {
        ApplyPreset();
        if (bLogPresetDebug)
        {
            UE_LOG(LogTemp, Warning, TEXT("IOC DBG BeginPlay: Applied preset. Preset=%d Tunnel=%d Radius=%.1f Wall=%.1f Voxel=%.1f NFreq=%.6f"),
                (int32)CavePreset, bGenerateTunnel, TunnelRadius, WallThickness, (float)VoxelSize, NoiseFrequency);
        }
        return;
    }

    if (ShouldAutoPreset())
    {
        CavePreset = EIOCCavePreset::LargeTunnel;
        ApplyPreset();
        if (bLogPresetDebug)
        {
            UE_LOG(LogTemp, Warning, TEXT("IOC DBG BeginPlay: Auto preset. Preset=%d Tunnel=%d Radius=%.1f Wall=%.1f Voxel=%.1f NFreq=%.6f"),
                (int32)CavePreset, bGenerateTunnel, TunnelRadius, WallThickness, (float)VoxelSize, NoiseFrequency);
        }
        return;
    }

    GenerateCave();
}

void AIOCProceduralActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // Retry generation if a previous call was cancelled
    if (bPendingRegeneration && !bIsGenerating)
    {
        bPendingRegeneration = false;
        GenerateCave();
    }

    if (!bEnableLOD || !LODMeshComponent || !MeshComponent) return;

    APlayerCameraManager* CamMgr = nullptr;
    if (GetWorld() && GetWorld()->GetFirstPlayerController())
    {
        CamMgr = GetWorld()->GetFirstPlayerController()->PlayerCameraManager;
    }
    if (!CamMgr) return;

    float Dist = FVector::Dist(GetActorLocation(), CamMgr->GetCameraLocation());

    if (!bLODActive && Dist > LODDistance + 250.0f)
    {
        bLODActive = true;
        MeshComponent->SetVisibility(false);
        LODMeshComponent->SetVisibility(true);
    }
    else if (bLODActive && Dist < LODDistance - 250.0f)
    {
        bLODActive = false;
        MeshComponent->SetVisibility(true);
        LODMeshComponent->SetVisibility(false);
    }
}

void AIOCProceduralActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    ApplyCaveMaterials();
    IOC_ConfigureTunnelSpline(CaveSpline);

#if WITH_EDITOR
    if (bForcePreset)
    {
        if (CavePreset == EIOCCavePreset::Custom)
        {
            CavePreset = EIOCCavePreset::LargeTunnel;
        }
        ApplyPreset();
        return;
    }

    // Legacy migration: presets imply tunnel mode. If an older asset saved with tunnel off,
    // re-apply the preset to restore expected behavior in-editor.
    if (ShouldAutoPreset())
    {
        CavePreset = EIOCCavePreset::LargeTunnel;
        ApplyPreset();
        return;
    }

    if (CavePreset != EIOCCavePreset::Custom && !bGenerateTunnel)
    {
        ApplyPreset();
        return;
    }
#endif

#if WITH_EDITOR
    if (bShowDebugViz)
    {
        FlushPersistentDebugLines(GetWorld());

        FVector Center = GenerationBounds * 0.5f;
        FVector Extent = GenerationBounds * 0.5f;

        if (bGenerateTunnel)
        {
            if (bUseSpline && CaveSpline && CaveSpline->GetNumberOfSplinePoints() > 1)
            {
                int32 Num = CaveSpline->GetNumberOfSplinePoints();
                FVector Min = CaveSpline->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::Local);
                FVector Max = Min;

                for (int32 i = 0; i < Num; ++i)
                {
                    FVector Pt = CaveSpline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
                    Min = Min.ComponentMin(Pt);
                    Max = Max.ComponentMax(Pt);
                }

                float Margin = TunnelRadius + WallThickness + (VoxelSize * 2.0);
                Min -= FVector(Margin);
                Max += FVector(Margin);

                FVector SplineCenter = (Min + Max) * 0.5f;
                FVector SplineExtent = (Max - Min) * 0.5f;

                DrawDebugBox(GetWorld(), GetActorTransform().TransformPosition(SplineCenter), SplineExtent, FColor::Orange, false, -1.0f, 0, 5.0f);

                for (int32 i = 0; i < Num - 1; ++i)
                {
                    FVector P1 = CaveSpline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
                    FVector P2 = CaveSpline->GetLocationAtSplinePoint(i+1, ESplineCoordinateSpace::World);
                    DrawDebugLine(GetWorld(), P1, P2, FColor::Green, false, -1.0f, 0, 8.0f);
                    DrawDebugCircle(GetWorld(), P1, TunnelRadius, 32, FColor::Yellow, false, -1.0f, 0, 2.0f, FVector(1,0,0), FVector(0,1,0), false);
                }
            }
            else
            {
                DrawDebugBox(GetWorld(), GetActorLocation() + Center, Extent, FColor::Blue, false, -1.0f, 0, 5.0f);

                FVector P1 = GetActorTransform().TransformPosition(TunnelStart);
                FVector P2 = GetActorTransform().TransformPosition(TunnelEnd);

                DrawDebugLine(GetWorld(), P1, P2, FColor::Green, false, -1.0f, 0, 10.0f);
                DrawDebugSphere(GetWorld(), P1, 50.0f, 12, FColor::Green, false, -1.0f, 0, 2.0f);
                DrawDebugSphere(GetWorld(), P2, 50.0f, 12, FColor::Red, false, -1.0f, 0, 2.0f);
                DrawDebugCircle(GetWorld(), P1, TunnelRadius, 32, FColor::Yellow, false, -1.0f, 0, 2.0f, FVector(1,0,0), FVector(0,1,0), false);
            }
        }
        else
        {
            DrawDebugBox(GetWorld(), GetActorLocation(), Extent, FColor::Cyan, false, -1.0f, 0, 5.0f);
        }
    }
#endif
}

#if WITH_EDITOR
void AIOCProceduralActor::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);

    if (bShowDebugViz)
    {
        OnConstruction(GetActorTransform());
    }

    if (bFinished)
    {
        GenerateCave();
    }
}
#endif

bool AIOCProceduralActor::ShouldAutoPreset() const
{
    // Heuristic: if everything is still at raw defaults and preset is Custom,
    // assume this is an older asset that expects LargeTunnel defaults.
    const bool bDefaultPreset = (CavePreset == EIOCCavePreset::Custom);
    const bool bDefaultTunnel = (bGenerateTunnel == false);
    const bool bDefaultsMatch =
        FMath::IsNearlyEqual((float)VoxelSize, 50.0f) &&
        FMath::IsNearlyEqual(NoiseFrequency, 0.005f) &&
        FMath::IsNearlyEqual(TunnelRadius, 300.0f) &&
        FMath::IsNearlyEqual(WallThickness, 60.0f);

    return bDefaultPreset && bDefaultTunnel && bDefaultsMatch;
}

void AIOCProceduralActor::ApplyPresetParams()
{
    // Presets are tunnel-focused; ensure tunnel mode is on when a preset is applied.
    bGenerateTunnel = true;

    auto SetDefaultTunnelLine = [this](float MinLength)
    {
        const float Len = FMath::Max(MinLength, TunnelRadius * 8.0f);
        const float Half = Len * 0.5f;
        // Raise tunnel so its floor sits at the actor origin (Z=0 local).
        // The centerline is at Z = TunnelRadius, bottom at Z = 0.
        TunnelStart = FVector(-Half, 0.0f, TunnelRadius);
        TunnelEnd = FVector( Half, 0.0f, TunnelRadius);
    };

    switch (CavePreset)
    {
        case EIOCCavePreset::LargeTunnel:
            TunnelRadius = 450.0f;
            NoiseFrequency = 0.0025f;
            WallThickness = 60.0f;
            VoxelSize = 40.0;
            DomainWarpIntensity = 0.0f;
            TerraceSteps = 0.0f;
            break;
        case EIOCCavePreset::TightCrawl:
            TunnelRadius = 150.0f;
            NoiseFrequency = 0.01f;
            WallThickness = 30.0f;
            VoxelSize = 20.0;
            DomainWarpIntensity = 0.0f;
            TerraceSteps = 0.0f;
            break;
        case EIOCCavePreset::OpenCavern:
            TunnelRadius = 1200.0f;
            NoiseFrequency = 0.001f;
            WallThickness = 100.0f;
            VoxelSize = 60.0;
            DomainWarpIntensity = 0.0f;
            TerraceSteps = 0.0f;
            break;
        case EIOCCavePreset::AlienHive:
            TunnelRadius = 400.0f;
            NoiseFrequency = 0.008f;
            WallThickness = 40.0f;
            VoxelSize = 30.0;
            DomainWarpIntensity = 200.0f;
            TerraceSteps = 0.0f;
            break;
        case EIOCCavePreset::CanyonStrata:
            TunnelRadius = 1000.0f;
            NoiseFrequency = 0.004f;
            WallThickness = 80.0f;
            VoxelSize = 50.0;
            DomainWarpIntensity = 0.0f;
            TerraceSteps = 150.0f;
            break;
        case EIOCCavePreset::Custom:
        default:
            break;
    }

    if (CavePreset != EIOCCavePreset::Custom)
    {
        bUseSpline = false;
        SetDefaultTunnelLine(4000.0f);

        if (IOC_ShouldFollowTunnelLine(CaveSpline))
        {
            IOC_SyncSplineToTunnelLine(CaveSpline, TunnelStart, TunnelEnd, TunnelRadius);
        }
    }
}

void AIOCProceduralActor::ApplyPreset()
{
    ApplyPresetParams();
    GenerateCave();
}

void AIOCProceduralActor::ApplyPresetSettingsOnly()
{
    ApplyPresetParams();
}

#if WITH_EDITOR
void AIOCProceduralActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    if (PropertyChangedEvent.Property)
    {
        const FName PropName = PropertyChangedEvent.Property->GetFName();

        if (PropName == GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, CaveMaterial))
        {
            ApplyCaveMaterials();
#if WITH_EDITOR
            if (GEditor)
            {
                GEditor->RedrawAllViewports(false);
            }
#endif
            return;
        }

        if (PropName == GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, CavePreset))
        {
            if (CavePreset != EIOCCavePreset::Custom)
            {
                ApplyPresetParams();
            }
        }
        else if (CavePreset != EIOCCavePreset::Custom)
        {
            // If the user manually edits a preset-controlled parameter,
            // switch to Custom so BeginPlay/OnConstruction won't silently
            // override their values via ApplyPresetParams().
            static const TArray<FName> PresetControlled = {
                GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, TunnelRadius),
                GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, NoiseFrequency),
                GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, WallThickness),
                GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, VoxelSize),
                GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, DomainWarpIntensity),
                GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, TerraceSteps),
                GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, bGenerateTunnel),
                GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, bUseSpline),
            };
            if (PresetControlled.Contains(PropName))
            {
                CavePreset = EIOCCavePreset::Custom;
            }
        }

        if (PropName == GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, bUseSpline))
        {
            IOC_ConfigureTunnelSpline(CaveSpline);

            if (bUseSpline && IOC_ShouldFollowTunnelLine(CaveSpline))
            {
                IOC_SyncSplineToTunnelLine(CaveSpline, TunnelStart, TunnelEnd, TunnelRadius);
            }
        }
        else if (!bUseSpline &&
            (PropName == GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, TunnelStart) ||
             PropName == GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, TunnelEnd)) &&
            IOC_ShouldFollowTunnelLine(CaveSpline))
        {
            IOC_SyncSplineToTunnelLine(CaveSpline, TunnelStart, TunnelEnd, TunnelRadius);
        }
    }

    // Refresh debug visualization
    OnConstruction(GetActorTransform());

    // Single generation call for all property changes
    GenerateCave();
}

void AIOCProceduralActor::BakeToStaticMesh()
{
    const FDynamicMesh3* RawMesh = MeshComponent ? MeshComponent->GetMesh() : nullptr;
    if (!RawMesh || RawMesh->TriangleCount() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("IOC BakeToStaticMesh: No mesh to bake. Run generation first."));
        return;
    }

    // ---- 1. Unique asset path ----
    const FString AssetName   = FString::Printf(TEXT("SM_Cave_%s"), *FGuid::NewGuid().ToString().Left(8));
    const FString PackageName = FString::Printf(TEXT("/Game/IOC_Baked/%s"), *AssetName);

    UPackage* Package = CreatePackage(*PackageName);
    Package->FullyLoad();

    UStaticMesh* SM = NewObject<UStaticMesh>(Package, FName(*AssetName), RF_Public | RF_Standalone);
    SM->InitResources();

    // ---- 2. Build FMeshDescription from FDynamicMesh3 ----
    const FDynamicMesh3& DynMesh = *RawMesh;
    const int32 NumSourceTris    = DynMesh.TriangleCount();

    FMeshDescription MeshDesc;
    FStaticMeshAttributes Attrs(MeshDesc);
    Attrs.Register();

    auto VertPositions = Attrs.GetVertexPositions();
    auto VINormals     = Attrs.GetVertexInstanceNormals();
    auto VIUVs         = Attrs.GetVertexInstanceUVs();
    auto VIColors      = Attrs.GetVertexInstanceColors();
    auto PGSlotNames   = Attrs.GetPolygonGroupMaterialSlotNames();

    const FPolygonGroupID PGrp = MeshDesc.CreatePolygonGroup();
    PGSlotNames[PGrp] = FName(TEXT("Cave_Material"));

    // Map DynMesh vertex IDs → FVertexIDs
    TArray<FVertexID> VMap;
    VMap.SetNum(DynMesh.MaxVertexID());
    for (int32 vid : DynMesh.VertexIndicesItr())
    {
        FVertexID VID      = MeshDesc.CreateVertex();
        VertPositions[VID] = FVector3f(DynMesh.GetVertex(vid));
        VMap[vid]          = VID;
    }

    const bool bHasNormals = DynMesh.HasAttributes() && DynMesh.Attributes()->PrimaryNormals() != nullptr;
    const bool bHasUVs     = DynMesh.HasAttributes() && DynMesh.Attributes()->PrimaryUV()      != nullptr;
    const bool bHasColors  = DynMesh.HasAttributes() && DynMesh.Attributes()->PrimaryColors()  != nullptr;

    for (int32 tid : DynMesh.TriangleIndicesItr())
    {
        const FIndex3i Tri = DynMesh.GetTriangle(tid);
        FVertexInstanceID Insts[3];
        for (int32 j = 0; j < 3; ++j)
        {
            const FVertexInstanceID Inst = MeshDesc.CreateVertexInstance(VMap[Tri[j]]);

            if (bHasNormals)
            {
                const FIndex3i NTri = DynMesh.Attributes()->PrimaryNormals()->GetTriangle(tid);
                VINormals[Inst] = DynMesh.Attributes()->PrimaryNormals()->GetElement(NTri[j]);
            }
            else
            {
                VINormals[Inst] = FVector3f::UnitZ();
            }

            if (bHasUVs)
            {
                const FIndex3i UTri = DynMesh.Attributes()->PrimaryUV()->GetTriangle(tid);
                VIUVs[Inst] = DynMesh.Attributes()->PrimaryUV()->GetElement(UTri[j]);
            }

            if (bHasColors)
            {
                const FIndex3i CTri = DynMesh.Attributes()->PrimaryColors()->GetTriangle(tid);
                VIColors[Inst] = DynMesh.Attributes()->PrimaryColors()->GetElement(CTri[j]);
            }

            Insts[j] = Inst;
        }
        MeshDesc.CreatePolygon(PGrp, TArrayView<const FVertexInstanceID>(Insts, 3));
    }

    if (MeshDesc.Polygons().Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("IOC BakeToStaticMesh: Conversion produced 0 polygons."));
        SM->MarkAsGarbage();
        return;
    }

    // ---- 3. Assign to LOD 0 ----
    SM->GetStaticMaterials().Add(FStaticMaterial(CaveMaterial, FName(TEXT("Cave_Material"))));

    FStaticMeshSourceModel& LOD0 = SM->AddSourceModel();
    LOD0.BuildSettings.bRecomputeNormals    = false;
    LOD0.BuildSettings.bRecomputeTangents   = true;
    LOD0.BuildSettings.bGenerateLightmapUVs = true;
    LOD0.BuildSettings.bRemoveDegenerates   = false;

    *SM->CreateMeshDescription(0) = MoveTemp(MeshDesc);
    SM->CommitMeshDescription(0);

    // ---- 4. Build, register, notify ----
    SM->Build(false);
    SM->PostEditChange();

    FAssetRegistryModule::GetRegistry().AssetCreated(SM);
    Package->MarkPackageDirty();

    UE_LOG(LogTemp, Display, TEXT("IOC: Baked '%s' (%d tris) → /Game/IOC_Baked/"), *AssetName, NumSourceTris);
    GEditor->SyncBrowserToObjects(TArray<UObject*>{SM});
}
#endif

void AIOCProceduralActor::RegenerateInEditor()
{
    GenerateCave();
}

// Internal optimized structure for fast segment checks
struct FIOCSegment
{
    FVector A;
    FVector BA; // B - A
    float LengthSq;
};

/** Signed distance from WorldP to the surface of a carving capture.
 *  Returns: negative = inside, 0 = on surface, positive = outside. */
static float IOC_SignedDistToShape(const FVector& WorldP, const FIOCCarvingCapture& C)
{
    const FVector LocalP = C.WorldTransform.InverseTransformPosition(WorldP);

    switch (C.ShapeType)
    {
    case EIOCCarvingShape::Sphere:
        return LocalP.Size() - C.SphereRadius;

    case EIOCCarvingShape::Box:
    {
        FVector Q(
            FMath::Abs(LocalP.X) - C.BoxExtent.X,
            FMath::Abs(LocalP.Y) - C.BoxExtent.Y,
            FMath::Abs(LocalP.Z) - C.BoxExtent.Z);
        FVector QPos(FMath::Max(Q.X, 0.f), FMath::Max(Q.Y, 0.f), FMath::Max(Q.Z, 0.f));
        float OutsideDist = QPos.Size();
        float InsideDist  = FMath::Min(FMath::Max(Q.X, FMath::Max(Q.Y, Q.Z)), 0.f);
        return OutsideDist + InsideDist;
    }

    case EIOCCarvingShape::Capsule:
    {
        // Capsule axis along local Z
        float ClampedZ = FMath::Clamp(LocalP.Z, -C.CapsuleHalfHeight, C.CapsuleHalfHeight);
        FVector Closest(LocalP.X, LocalP.Y, ClampedZ);
        return (LocalP - Closest).Size() - C.CapsuleRadius;
    }

    default:
        return FLT_MAX;
    }
}

void AIOCProceduralActor::CarveAtLocation(FVector WorldLocation, float Radius)
{
    FIOCCarvingCapture Carve;
    Carve.ShapeType = EIOCCarvingShape::Sphere;
    Carve.SphereRadius = Radius;
    Carve.FalloffRadius = 50.0f; 
    Carve.WorldTransform = FTransform(WorldLocation);
    
    RuntimeCarves.Add(Carve);
    GenerateCave();
}


void AIOCProceduralActor::GenerateCave()
{
    if (bIsGenerating)
    {
        // Signal the in-flight generation to abort; we'll retry on the next tick.
        bCancelRequested = true;
        bPendingRegeneration = true;
        if (bLogPresetDebug)
        {
            UE_LOG(LogTemp, Warning, TEXT("IOC DBG GenerateCave: Cancelling in-flight generation, will retry."));
        }
        return;
    }
    bIsGenerating = true;
    bIsGeneratingDisplay = true;
    bCancelRequested = false;
    bPendingRegeneration = false;

    // Default Params
    FVector Bounds = GenerationBounds;
    FVector GridOrigin = FVector::ZeroVector;

    // --- Biome Gathering ---
    TArray<FIOCBiomeData> LocalBiomeData;
    if (GetWorld())
    {
        for (TActorIterator<AIOCBiomeVolume> It(GetWorld()); It; ++It)
        {
            AIOCBiomeVolume* Vol = *It;
            if (!Vol) continue;
            
            FIOCBiomeData Data;
            Data.Transform = Vol->GetActorTransform();
            Data.Bounds = FBoxSphereBounds(Vol->GetComponentsBoundingBox());
            Data.Priority = Vol->Priority;
            
            Data.bOverride_NoiseFrequency = Vol->bOverride_NoiseFrequency;
            Data.NoiseFrequency = Vol->NoiseFrequency;
            Data.bOverride_TunnelRadius = Vol->bOverride_TunnelRadius;
            Data.TunnelRadius = Vol->TunnelRadius;
            Data.bOverride_WallThickness = Vol->bOverride_WallThickness;
            Data.WallThickness = Vol->WallThickness;
            Data.bOverride_TerraceSteps = Vol->bOverride_TerraceSteps;
            Data.TerraceSteps = Vol->TerraceSteps;
            
            LocalBiomeData.Add(Data);
        }
        LocalBiomeData.Sort([](const FIOCBiomeData& A, const FIOCBiomeData& B) {
            return A.Priority < B.Priority;
        });
    }

    // --- 1. Multi-Spline "Segment Soup" Setup ---
    TArray<FIOCSegment> CachedSegments;
    bool bSplineMode = bGenerateTunnel && bUseSpline;

    if (bSplineMode)
    {
        TArray<USplineComponent*> AllSplines;
        GetComponents<USplineComponent>(AllSplines);

        FVector MinBounds = FVector(MAX_flt);
        FVector MaxBounds = FVector(-MAX_flt);
        bool bFoundPoints = false;
        double TotalSplineLength = 0.0;

        const FTransform ActorInvTransform = GetActorTransform().Inverse();

        for (USplineComponent* Spline : AllSplines)
        {
            if (!Spline || Spline->GetNumberOfSplinePoints() < 2) continue;

            int32 NumPoints = Spline->GetNumberOfSplinePoints();
            for (int32 i = 0; i < NumPoints - 1; ++i)
            {
                FVector P1 = ActorInvTransform.TransformPosition(
                    Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World));
                FVector P2 = ActorInvTransform.TransformPosition(
                    Spline->GetLocationAtSplinePoint(i + 1, ESplineCoordinateSpace::World));

                FIOCSegment Seg;
                Seg.A = P1;
                Seg.BA = P2 - P1;
                Seg.LengthSq = Seg.BA.SizeSquared();

                if (Seg.LengthSq > SMALL_NUMBER)
                {
                    CachedSegments.Add(Seg);
                    TotalSplineLength += FMath::Sqrt((double)Seg.LengthSq);

                    if (!bFoundPoints) { MinBounds = P1; MaxBounds = P1; bFoundPoints = true; }
                    MinBounds = MinBounds.ComponentMin(P1).ComponentMin(P2);
                    MaxBounds = MaxBounds.ComponentMax(P1).ComponentMax(P2);
                }
            }
        }

        if (CachedSegments.Num() > 0)
        {
            const double MinUsefulLen = FMath::Max((double)TunnelRadius * 2.0, (double)VoxelSize * 4.0);
            if (TotalSplineLength < MinUsefulLen)
            {
                if (bLogPresetDebug)
                {
                    UE_LOG(LogTemp, Warning, TEXT("IOC DBG GenerateCave: Spline too short (%.1f < %.1f). Falling back to line tunnel."), TotalSplineLength, MinUsefulLen);
                }
                bSplineMode = false;
                CachedSegments.Reset();
            }
            else
            {
                float Margin = TunnelRadius + WallThickness + (VoxelSize * 3.0);
                MinBounds -= FVector(Margin);
                MaxBounds += FVector(Margin);

                GridOrigin = MinBounds;
                Bounds = MaxBounds - MinBounds;

                Bounds.X = FMath::Max(Bounds.X, VoxelSize * 2.0);
                Bounds.Y = FMath::Max(Bounds.Y, VoxelSize * 2.0);
                Bounds.Z = FMath::Max(Bounds.Z, VoxelSize * 2.0);
            }
        }
        else
        {
            bSplineMode = false;
        }
    }

    // If not using spline mode, ensure the tunnel line and bounds are sane to avoid "blob" or "single wall" output.
    FVector LocalTStart = TunnelStart;
    FVector LocalTEnd = TunnelEnd;
    if (bGenerateTunnel && !bSplineMode)
    {
        if ((LocalTEnd - LocalTStart).SizeSquared() < KINDA_SMALL_NUMBER)
        {
            const float DefaultLen = FMath::Max(Bounds.X, 1000.0f);
            LocalTEnd = LocalTStart + FVector(DefaultLen, 0.0f, 0.0f);
        }

        const float Margin = TunnelRadius + WallThickness + (VoxelSize * 3.0f);

        // Center bounds around the tunnel line so the radius isn't clipped to +Y/+Z only.
        FVector MinBounds = LocalTStart.ComponentMin(LocalTEnd) - FVector(Margin);
        FVector MaxBounds = LocalTStart.ComponentMax(LocalTEnd) + FVector(Margin);
        GridOrigin = MinBounds;
        Bounds = MaxBounds - MinBounds;

        Bounds.X = FMath::Max(Bounds.X, VoxelSize * 2.0f);
        Bounds.Y = FMath::Max(Bounds.Y, VoxelSize * 2.0f);
        Bounds.Z = FMath::Max(Bounds.Z, VoxelSize * 2.0f);
    }

    if (bLogPresetDebug)
    {
        UE_LOG(LogTemp, Warning, TEXT("IOC DBG GenerateCave: Tunnel=%d SplineMode=%d Start=%s End=%s Bounds=%s Voxel=%.1f NFreq=%.6f Radius=%.1f Wall=%.1f"),
            bGenerateTunnel, bSplineMode,
            *LocalTStart.ToString(), *LocalTEnd.ToString(),
            *Bounds.ToString(), (float)VoxelSize, NoiseFrequency, TunnelRadius, WallThickness);
    }

    // Capture locals for async
    double VSize = VoxelSize;
    float NFreq = NoiseFrequency;
    float NThresh = NoiseThreshold;
    bool bTunnel = bGenerateTunnel;
    FVector TStart = LocalTStart;
    FVector TEnd = LocalTEnd;
    float TRadius = TunnelRadius;
    float TWall = WallThickness;
    int32 Smooth = SmoothIterations;
    int32 Octaves = NoiseOctaves;
    float Lacunarity = NoiseLacunarity;
    float Persistence = NoisePersistence;
    float ChamberWeight = MacroChamberWeight;
    float RidgedWeight = RidgedDetailWeight;
    float DensityBias = InteriorDensityBias;
    float Tiling = TextureTiling;
    FVector ActorLoc = GetActorLocation();
    float DWarp = DomainWarpIntensity;
    float TSteps = TerraceSteps;
    int32 Seed = CaveSeed;
    bool bSmartColors = bGenerateSmartColors;
    bool bLOD = bEnableLOD;
    float LODMult = LODVoxelSizeMultiplier;
    bool bWorldSpaceNoise = bUseWorldSpaceNoise;
    bool bNavMesh = bAutoRebuildNavMesh;

    TWeakObjectPtr<AIOCProceduralActor> WeakThis(this);
    TArray<FIOCScatterLayer> LocalDecorationLayers = DecorationLayers;

    TArray<FIOCCarvingCapture> LocalCarvingCaptures;
    {
        TArray<UIOCCarvingComponent*> CarvingComps;
        GetComponents<UIOCCarvingComponent>(CarvingComps);
        LocalCarvingCaptures.Reserve(CarvingComps.Num() + RuntimeCarves.Num());
        for (UIOCCarvingComponent* C : CarvingComps)
        {
            if (C) LocalCarvingCaptures.Add(C->MakeCapture());
        }
    }
    // Append runtime carves
    LocalCarvingCaptures.Append(RuntimeCarves);

    Async(EAsyncExecution::ThreadPool, [=, this, LocalDecorationLayers = MoveTemp(LocalDecorationLayers), Segments = MoveTemp(CachedSegments), LocalCarvingCaptures = MoveTemp(LocalCarvingCaptures), LocalBiomeData = MoveTemp(LocalBiomeData)]() mutable
    {
        // ---------------------------------------------------------------
        // Helper: build a cave mesh at the given voxel resolution.
        // Runs synchronously inside this async task.
        // ---------------------------------------------------------------
        auto BuildMesh = [&](double VoxSize, bool bColors, float WallThickOverride) -> FDynamicMesh3
        {
            double SafeVoxel = FMath::Max(10.0, VoxSize);
            int32 SizeX = FMath::Clamp((int32)(Bounds.X / SafeVoxel), 2, 400);
            int32 SizeY = FMath::Clamp((int32)(Bounds.Y / SafeVoxel), 2, 400);
            int32 SizeZ = FMath::Clamp((int32)(Bounds.Z / SafeVoxel), 2, 400);

            // Dynamic Wall Safety: Ensure wall is at least 1.5x voxel size to prevent aliasing/fragmentation
            // This is critical for LOD generation where voxel size is large but wall is thin.
            float EffectiveWall = (WallThickOverride > 0.0f) ? WallThickOverride : TWall;
            float MinSafeWall = (float)SafeVoxel * 1.5f;
            if (EffectiveWall < MinSafeWall)
            {
                EffectiveWall = MinSafeWall;
            }

            // ---- Phase 1: voxel grid ----
            TArray<bool> Voxels;
            Voxels.Init(false, (int64)SizeX * SizeY * SizeZ);
            auto GetIdx = [&](int32 x, int32 y, int32 z) { return (int64)x + (int64)y * SizeX + (int64)z * SizeX * SizeY; };

            FVector SeedOffset = FVector((float)Seed * 132.5f, (float)Seed * 37.2f, (float)Seed * 91.8f);
            FVector FinalOffset = bWorldSpaceNoise ? ActorLoc : SeedOffset;
            const FVector FillCenteringOffset = bTunnel ? FVector::ZeroVector : (Bounds * 0.5f);
            const int32 SafeOctaves = FMath::Clamp(Octaves, 1, 8);
            const float SafeLacunarity = FMath::Max(Lacunarity, 1.1f);
            const float SafePersistence = FMath::Clamp(Persistence, 0.1f, 0.95f);
            const float SafeChamberWeight = FMath::Clamp(ChamberWeight, 0.0f, 1.0f);
            const float SafeRidgedWeight = FMath::Clamp(RidgedWeight, 0.0f, 1.0f);
            const float SafeDensityBias = FMath::Clamp(DensityBias, -1.0f, 1.0f);

            auto GetDistLine = [](const FVector& P, const FVector& A, const FVector& B, float& OutTRaw, bool& bOutOfRange) -> float {
                FVector BA = B - A;
                float LensSq = BA.SizeSquared();
                if (LensSq < SMALL_NUMBER)
                {
                    OutTRaw = 0.0f;
                    bOutOfRange = true;
                    return FVector::Dist(P, A);
                }
                OutTRaw = FVector::DotProduct(P - A, BA) / LensSq;
                bOutOfRange = (OutTRaw < 0.0f || OutTRaw > 1.0f);
                float T = FMath::Clamp(OutTRaw, 0.0f, 1.0f);
                return FVector::Dist(P, A + T * BA);
            };

            auto GetDistToSoup = [&](const FVector& P, const TArray<FIOCSegment>& InSegments, float& OutTRaw, bool& bOutOfRange) -> float {
                float MinDistSq = MAX_flt;
                float BestTRaw = 0.0f;
                bool bFoundInRange = false;
                bool bBestOutOfRange = true;
                for (const FIOCSegment& Seg : InSegments)
                {
                    float TRaw = FVector::DotProduct(P - Seg.A, Seg.BA) / Seg.LengthSq;
                    float T = FMath::Clamp(TRaw, 0.0f, 1.0f);
                    float DistSq = FVector::DistSquared(P, Seg.A + T * Seg.BA);
                    const bool bThisOutOfRange = (TRaw < 0.0f || TRaw > 1.0f);

                    if (!bThisOutOfRange)
                    {
                        if (!bFoundInRange || DistSq < MinDistSq)
                        {
                            MinDistSq = DistSq;
                            BestTRaw = TRaw;
                            bFoundInRange = true;
                            bBestOutOfRange = false;
                        }
                    }
                    else if (!bFoundInRange && DistSq < MinDistSq)
                    {
                        MinDistSq = DistSq;
                        BestTRaw = TRaw;
                        bBestOutOfRange = true;
                    }
                }
                OutTRaw = BestTRaw;
                bOutOfRange = bBestOutOfRange;
                return FMath::Sqrt(MinDistSq);
            };

            // Voxel grid size warning
            const int64 TotalVoxels = (int64)SizeX * SizeY * SizeZ;
            if (TotalVoxels > 10000000) // 10M voxel warning threshold
            {
                UE_LOG(LogTemp, Warning, TEXT("IOC: Large voxel grid (%dx%dx%d = %lld voxels). Consider increasing VoxelSize or reducing GenerationBounds."),
                    SizeX, SizeY, SizeZ, TotalVoxels);
            }

            // ---- Parallelized voxel fill ----
            ParallelFor(TotalVoxels, [&](int64 Index)
            {
                const int32 z = (int32)(Index / ((int64)SizeX * SizeY));
                const int32 rem = (int32)(Index % ((int64)SizeX * SizeY));
                const int32 y = rem / SizeX;
                const int32 x = rem % SizeX;

                FVector P = GridOrigin + FVector(x, y, z) * SafeVoxel;
                FVector LocalP = P - FillCenteringOffset;
                FVector SampleP = LocalP + FinalOffset;
                FVector WorldP = ActorLoc + LocalP;

                // --- Biome Overrides ---
                float CurrentNFreq = NFreq;
                float CurrentTRadius = TRadius;
                float CurrentTWall = EffectiveWall;
                float CurrentTSteps = TSteps;

                for (const FIOCBiomeData& Biome : LocalBiomeData)
                {
                    // Point-in-OBB: transform world point into biome's local space
                    const FVector BiomeLocalP = Biome.Transform.InverseTransformPosition(WorldP);
                    const FVector HalfExt = Biome.Bounds.BoxExtent;
                    const FVector Center = Biome.Transform.InverseTransformPosition(Biome.Bounds.Origin);
                    const FVector Delta = BiomeLocalP - Center;
                    if (FMath::Abs(Delta.X) <= HalfExt.X &&
                        FMath::Abs(Delta.Y) <= HalfExt.Y &&
                        FMath::Abs(Delta.Z) <= HalfExt.Z)
                    {
                        if (Biome.bOverride_NoiseFrequency) CurrentNFreq = Biome.NoiseFrequency;
                        if (Biome.bOverride_TunnelRadius) CurrentTRadius = Biome.TunnelRadius;
                        if (Biome.bOverride_WallThickness) CurrentTWall = Biome.WallThickness;
                        if (Biome.bOverride_TerraceSteps) CurrentTSteps = Biome.TerraceSteps;
                    }
                }

                // Enforce safety on override
                if (CurrentTWall < MinSafeWall) CurrentTWall = MinSafeWall;

                // --- Critical Mesh Safety (The "Shredded Mesh" Fix) ---
                const float MaxFreq = bTunnel
                    ? (0.25f / (float)SafeVoxel)
                    : (0.45f / (float)SafeVoxel);
                if (CurrentNFreq > MaxFreq) CurrentNFreq = MaxFreq;
                if (CurrentNFreq < 0.00001f) CurrentNFreq = 0.00001f;

                float MaxAmplitude = 0.4f / (CurrentNFreq + SMALL_NUMBER);
                float DesiredAmplitude = CurrentTRadius * 0.5f;
                float SafeAmplitude = FMath::Min(DesiredAmplitude, MaxAmplitude);

                // Domain Warp application
                if (DWarp > 1.0f)
                {
                    float Wx = FMath::PerlinNoise3D(SampleP * CurrentNFreq + FVector(5.2, 1.3, 0));
                    float Wy = FMath::PerlinNoise3D(SampleP * CurrentNFreq + FVector(1.7, 9.2, 0));
                    float Wz = FMath::PerlinNoise3D(SampleP * CurrentNFreq + FVector(8.3, 2.8, 0));
                    SampleP += FVector(Wx, Wy, Wz) * DWarp;
                }

                if (CurrentTSteps > 1.0f)
                {
                    float RelZ = SampleP.Z;
                    SampleP.Z = FMath::RoundToFloat(RelZ / CurrentTSteps) * CurrentTSteps;
                }

                float Noise = FMath::PerlinNoise3D(SampleP * CurrentNFreq);

                if (bTunnel)
                {
                    float TRaw = 0.0f;
                    bool bOutOfRange = false;
                    float D = bSplineMode
                        ? GetDistToSoup(LocalP, Segments, TRaw, bOutOfRange)
                        : GetDistLine(LocalP, TStart, TEnd, TRaw, bOutOfRange);

                    if (bOutOfRange)
                    {
                        // Keep tunnel shells open by rejecting voxels beyond the path range
                        // instead of tapering them into hemispherical caps.
                        Voxels[GetIdx(x, y, z)] = false;
                        return;
                    }

                    float OrganicRadius = CurrentTRadius + (Noise * SafeAmplitude);

                    // Pad must exceed the max noise gradient between adjacent voxels
                    // to guarantee wall continuity (no disconnected fragments).
                    float NoiseGradientPad = SafeAmplitude * CurrentNFreq * (float)SafeVoxel * (float)PI;
                    const float VoxelPad = FMath::Max((float)SafeVoxel * 0.5f, NoiseGradientPad);
                    float Outer = OrganicRadius + VoxelPad;
                    float Inner = OrganicRadius - CurrentTWall - VoxelPad;
                    if (Inner < 0.0f) Inner = 0.0f;

                    bool bTunnelWall = (D <= Outer) && (D >= Inner);
                    Voxels[GetIdx(x, y, z)] = bTunnelWall;
                }
                else
                {
                    const float BaseField = IOC_Fbm01(
                        SampleP + FVector(17.2f, 3.9f, 29.7f),
                        CurrentNFreq,
                        SafeOctaves,
                        SafeLacunarity,
                        SafePersistence);
                    const float ChamberField = IOC_Fbm01(
                        SampleP + FVector(-37.4f, 19.1f, 7.6f),
                        CurrentNFreq * 0.4f,
                        FMath::Max(2, SafeOctaves - 1),
                        SafeLacunarity,
                        SafePersistence);
                    const float CorridorField = IOC_RidgedFbm01(
                        SampleP + FVector(53.8f, 27.5f, 13.2f),
                        CurrentNFreq * 1.65f,
                        FMath::Max(2, SafeOctaves - 1),
                        SafeLacunarity,
                        SafePersistence);
                    const float DetailField = IOC_Fbm01(
                        SampleP + FVector(8.6f, 61.4f, 41.1f),
                        CurrentNFreq * 2.35f,
                        FMath::Max(1, SafeOctaves - 1),
                        SafeLacunarity,
                        SafePersistence);

                    float Density = BaseField;
                    Density += DetailField * 0.12f;
                    Density += SafeDensityBias * 0.2f;
                    Density -= FMath::SmoothStep(0.55f, 0.82f, ChamberField) * SafeChamberWeight;
                    Density -= FMath::SmoothStep(0.45f, 0.85f, CorridorField) * SafeRidgedWeight;

                    bool bSolid;
                    if (bWorldSpaceNoise)
                    {
                        const float MacroField = IOC_Fbm01(
                            SampleP + FVector(101.3f, 37.4f, 73.5f),
                            CurrentNFreq * 0.18f,
                            2,
                            2.0f,
                            0.5f);
                        Density += MacroField * 0.08f;
                        Density = IOC_Saturate(Density);
                        bSolid = (Density > IOC_Saturate(NThresh));
                    }
                    else
                    {
                        float NormX = (float)x / (float)SizeX;
                        float NormY = (float)y / (float)SizeY;
                        float NormZ = (float)z / (float)SizeZ;
                        float MinDist = FMath::Min(FMath::Min(NormX, 1.0f - NormX),
                                       FMath::Min(FMath::Min(NormY, 1.0f - NormY),
                                                  FMath::Min(NormZ, 1.0f - NormZ)));
                        float EdgeMask = FMath::SmoothStep(0.0f, 0.1f, MinDist);
                        float Envelope = IOC_BoxEnvelope01(LocalP, Bounds);

                        Density += Envelope * 0.35f;
                        Density = IOC_Saturate(Density);
                        if (EdgeMask < 0.01f) bSolid = false;
                        else if (EdgeMask < 1.0f)
                        {
                            float FadedDensity = Density * EdgeMask;
                            bSolid = (FadedDensity > IOC_Saturate(NThresh));
                        }
                        else
                        {
                            bSolid = (Density > IOC_Saturate(NThresh));
                        }
                    }
                    Voxels[GetIdx(x, y, z)] = bSolid;
                }
            });

            // ---- Cancellation checkpoint ----
            if (bCancelRequested) return FDynamicMesh3{};

            // ---- Carving pass (parallelized) ----
            if (!LocalCarvingCaptures.IsEmpty())
            {
                ParallelFor(TotalVoxels, [&](int64 Index)
                {
                    if (!Voxels[Index]) return; // already empty

                    const int32 z = (int32)(Index / ((int64)SizeX * SizeY));
                    const int32 rem = (int32)(Index % ((int64)SizeX * SizeY));
                    const int32 y = rem / SizeX;
                    const int32 x = rem % SizeX;

                    FVector P = GridOrigin + FVector(x, y, z) * SafeVoxel;
                    FVector LocalP = P - FillCenteringOffset;
                    FVector WorldP = ActorLoc + LocalP;

                    float MinDist       = FLT_MAX;
                    float ActiveFalloff = 50.f;

                    for (const FIOCCarvingCapture& Cap : LocalCarvingCaptures)
                    {
                        float d = IOC_SignedDistToShape(WorldP, Cap);
                        if (d < MinDist)
                        {
                            MinDist       = d;
                            ActiveFalloff = Cap.FalloffRadius;
                        }
                    }

                    if (MinDist <= 0.f)
                    {
                        Voxels[Index] = false;
                    }
                    else if (ActiveFalloff > 0.f && MinDist < ActiveFalloff)
                    {
                        float t        = 1.f - (MinDist / ActiveFalloff);
                        float Blend    = FMath::SmoothStep(0.f, 1.f, t);
                        float HashVal  = FMath::PerlinNoise3D(FVector(x, y, z) * 0.7f + FVector((float)Seed));
                        float NormHash = (HashVal + 1.f) * 0.5f;
                        if (NormHash < Blend)
                        {
                            Voxels[Index] = false;
                        }
                    }
                });
            }

            // ---- Cancellation checkpoint ----
            if (bCancelRequested) return FDynamicMesh3{};

            // ---- Hole fill pass (tunnel shells only) ----
            if (bTunnel)
            {
                TArray<bool> VoxelsFilled = Voxels;
                for (int32 z = 1; z < SizeZ - 1; ++z)
                    for (int32 y = 1; y < SizeY - 1; ++y)
                        for (int32 x = 1; x < SizeX - 1; ++x)
                        {
                            const int64 Idx = GetIdx(x, y, z);
                            if (Voxels[Idx]) continue;

                            int32 Neigh = 0;
                            for (int32 dz = -1; dz <= 1; ++dz)
                                for (int32 dy = -1; dy <= 1; ++dy)
                                    for (int32 dx = -1; dx <= 1; ++dx)
                                    {
                                        if (dx == 0 && dy == 0 && dz == 0) continue;
                                        if (Voxels[GetIdx(x + dx, y + dy, z + dz)]) ++Neigh;
                                    }

                            // If most neighbors are wall voxels, fill this tiny hole.
                            if (Neigh >= 18)
                            {
                                VoxelsFilled[Idx] = true;
                            }
                        }

                Voxels = MoveTemp(VoxelsFilled);
            }

            // ---- Cancellation checkpoint ----
            if (bCancelRequested) return FDynamicMesh3{};

            // ---- Phase 2: collect quads with shared positions ----
            TArray<FVector3d> SharedPos;
            TMap<FIntVector, int32> PosCache;
            TArray<FIOCQuad> Quads;

            FVector3d CenteringOffset = bTunnel ? FVector3d::Zero() : FVector3d(Bounds.X, Bounds.Y, Bounds.Z) * 0.5;

            auto GetPosIdx = [&](int32 cx, int32 cy, int32 cz) -> int32 {
                FIntVector Key(cx, cy, cz);
                if (int32* Found = PosCache.Find(Key)) return *Found;
                int32 ID = SharedPos.Num();
                FVector3d Pos = FVector3d(GridOrigin) + (FVector3d(cx, cy, cz) * SafeVoxel) - CenteringOffset;
                SharedPos.Add(Pos);
                PosCache.Add(Key, ID);
                return ID;
            };

            auto AddQuad = [&](int32 a, int32 b, int32 c, int32 d) {
                FIOCQuad Q; Q.V[0] = a; Q.V[1] = b; Q.V[2] = c; Q.V[3] = d;
                Quads.Add(Q);
            };

            for (int32 z = 0; z < SizeZ; ++z)
                for (int32 y = 0; y < SizeY; ++y)
                    for (int32 x = 0; x < SizeX; ++x)
                    {
                        if (!Voxels[GetIdx(x,y,z)]) continue;

                        auto IsAir = [&](int32 nx, int32 ny, int32 nz) {
                            if (nx < 0 || nx >= SizeX || ny < 0 || ny >= SizeY || nz < 0 || nz >= SizeZ) return true;
                            return !Voxels[GetIdx(nx, ny, nz)];
                        };

                        if (IsAir(x-1,y,z)) AddQuad(GetPosIdx(x,y,z),     GetPosIdx(x,y,z+1),     GetPosIdx(x,y+1,z+1),   GetPosIdx(x,y+1,z));
                        if (IsAir(x+1,y,z)) AddQuad(GetPosIdx(x+1,y+1,z), GetPosIdx(x+1,y+1,z+1), GetPosIdx(x+1,y,z+1),   GetPosIdx(x+1,y,z));
                        if (IsAir(x,y-1,z)) AddQuad(GetPosIdx(x+1,y,z),   GetPosIdx(x+1,y,z+1),   GetPosIdx(x,y,z+1),     GetPosIdx(x,y,z));
                        if (IsAir(x,y+1,z)) AddQuad(GetPosIdx(x,y+1,z),   GetPosIdx(x,y+1,z+1),   GetPosIdx(x+1,y+1,z+1), GetPosIdx(x+1,y+1,z));
                        if (IsAir(x,y,z-1)) AddQuad(GetPosIdx(x,y+1,z),   GetPosIdx(x+1,y+1,z),   GetPosIdx(x+1,y,z),     GetPosIdx(x,y,z));
                        if (IsAir(x,y,z+1)) AddQuad(GetPosIdx(x,y,z+1),   GetPosIdx(x+1,y,z+1),   GetPosIdx(x+1,y+1,z+1), GetPosIdx(x,y+1,z+1));
                    }

            if (Quads.Num() == 0)
            {
                return FDynamicMesh3{};
            }

            // ---- Debug: validate quads for degeneracy (duplicate verts / zero area) ----
#if DO_CHECK
            int32 DegenerateCount = 0;
            for (const FIOCQuad& Q : Quads)
            {
                const int32 A = Q.V[0];
                const int32 B = Q.V[1];
                const int32 C = Q.V[2];
                const int32 D = Q.V[3];

                const bool bDup = (A == B) || (A == C) || (A == D) || (B == C) || (B == D) || (C == D);
                if (bDup)
                {
                    ++DegenerateCount;
                    continue;
                }

                const FVector3d& PA = SharedPos[A];
                const FVector3d& PB = SharedPos[B];
                const FVector3d& PC = SharedPos[C];
                const FVector3d& PD = SharedPos[D];

                const double Area1Sq = FVector3d::CrossProduct(PB - PA, PC - PA).SquaredLength();
                const double Area2Sq = FVector3d::CrossProduct(PC - PA, PD - PA).SquaredLength();
                if (Area1Sq < (double)SMALL_NUMBER || Area2Sq < (double)SMALL_NUMBER)
                {
                    ++DegenerateCount;
                }
            }

            ensureMsgf(DegenerateCount == 0, TEXT("IOC: %d degenerate quads detected. Check AddQuad winding/indices."), DegenerateCount);
#endif

            // ---- Phase 3: Laplacian smoothing on shared positions ----
            if (Smooth > 0)
            {
                TArray<TSet<int32>> AdjList;
                AdjList.SetNum(SharedPos.Num());
                for (const FIOCQuad& Q : Quads)
                    for (int32 i = 0; i < 4; ++i) {
                        int32 next = (i + 1) % 4;
                        AdjList[Q.V[i]].Add(Q.V[next]);
                        AdjList[Q.V[next]].Add(Q.V[i]);
                    }

                TArray<FVector3d> OldPos;
                OldPos.SetNum(SharedPos.Num());
                for (int32 k = 0; k < Smooth; ++k)
                {
                    for (int32 i = 0; i < SharedPos.Num(); ++i) OldPos[i] = SharedPos[i];
                    for (int32 i = 0; i < SharedPos.Num(); ++i)
                    {
                        if (AdjList[i].Num() == 0) continue;
                        FVector3d Sum = FVector3d::Zero();
                        for (int32 n : AdjList[i]) Sum += OldPos[n];
                        SharedPos[i] = FMath::Lerp(OldPos[i], Sum / (double)AdjList[i].Num(), 0.5);
                    }
                }
            }

            // ---- Phase 4: build FDynamicMesh3 with per-quad vertices ----
            // Per-quad vertices avoid non-manifold edges that FDynamicMesh3 rejects
            // when diagonal-touching voxels share grid edges. Smooth normals are
            // still achieved via NormalAccum from shared positions.

            // First pass: accumulate face normals on shared positions
            TArray<FVector3d> NormalAccum;
            NormalAccum.Init(FVector3d::Zero(), SharedPos.Num());

            for (const FIOCQuad& Q : Quads)
            {
                FVector3d E1 = SharedPos[Q.V[1]] - SharedPos[Q.V[0]];
                FVector3d E2 = SharedPos[Q.V[2]] - SharedPos[Q.V[0]];
                FVector3d FN = FVector3d::CrossProduct(E1, E2);
                double Len = FN.Length();
                if (Len > SMALL_NUMBER) FN /= Len;
                for (int32 j = 0; j < 4; ++j)
                    NormalAccum[Q.V[j]] += FN;
            }

            for (FVector3d& N : NormalAccum) {
                double L = N.Length();
                if (L > SMALL_NUMBER) N /= L; else N = FVector3d(0, 0, 1);
            }

            // Second pass: build mesh with per-quad (unshared) vertices.
            // Each quad emits front faces AND back faces (reversed winding)
            // so the mesh renders correctly from both sides regardless of
            // the material's TwoSided setting.
            FDynamicMesh3 LocalMesh;
            LocalMesh.EnableAttributes();

            TArray<int32> VertToShared;
            TArray<bool> VertIsBackface;
            VertToShared.Reserve(Quads.Num() * 8);
            VertIsBackface.Reserve(Quads.Num() * 8);

            for (const FIOCQuad& Q : Quads)
            {
                // Front faces (original winding)
                int32 mv[4];
                for (int32 j = 0; j < 4; ++j)
                {
                    mv[j] = LocalMesh.AppendVertex(SharedPos[Q.V[j]]);
                    VertToShared.Add(Q.V[j]);
                    VertIsBackface.Add(false);
                }
                LocalMesh.AppendTriangle(mv[0], mv[1], mv[2]);
                LocalMesh.AppendTriangle(mv[2], mv[3], mv[0]);

                // Back faces (reversed winding, inverted normals)
                int32 bmv[4];
                for (int32 j = 0; j < 4; ++j)
                {
                    bmv[j] = LocalMesh.AppendVertex(SharedPos[Q.V[j]]);
                    VertToShared.Add(Q.V[j]);
                    VertIsBackface.Add(true);
                }
                LocalMesh.AppendTriangle(bmv[2], bmv[1], bmv[0]);
                LocalMesh.AppendTriangle(bmv[0], bmv[3], bmv[2]);
            }

            // ---- Phase 5: normal & UV & Color overlays ----
            if (LocalMesh.TriangleCount() > 0 && LocalMesh.HasAttributes())
            {
                // --- Normals ---
                FDynamicMeshNormalOverlay* NormOvl = LocalMesh.Attributes()->PrimaryNormals();
                if (NormOvl)
                {
                    NormOvl->ClearElements();
                    TArray<int32> VertToElem;
                    VertToElem.SetNum(LocalMesh.MaxVertexID());

                    for (int32 vid : LocalMesh.VertexIndicesItr())
                    {
                        FVector3f N(0, 0, 1);
                        if (vid < VertToShared.Num())
                        {
                            int32 si = VertToShared[vid];
                            if (si >= 0 && si < NormalAccum.Num()) N = FVector3f(NormalAccum[si]);
                        }
                        // Invert normal for backface vertices
                        if (vid < VertIsBackface.Num() && VertIsBackface[vid])
                        {
                            N = -N;
                        }
                        VertToElem[vid] = NormOvl->AppendElement(N);
                    }

                    for (int32 tid : LocalMesh.TriangleIndicesItr())
                    {
                        FIndex3i Tri = LocalMesh.GetTriangle(tid);
                        NormOvl->SetTriangle(tid, FIndex3i(VertToElem[Tri[0]], VertToElem[Tri[1]], VertToElem[Tri[2]]));
                    }
                }

                // --- Vertex Colors (Smart Materials, primary mesh only) ---
                if (bColors)
                {
                    LocalMesh.Attributes()->EnablePrimaryColors();
                    FDynamicMeshColorOverlay* ColorOvl = LocalMesh.Attributes()->PrimaryColors();
                    if (ColorOvl)
                    {
                        ColorOvl->ClearElements();
                        TArray<int32> VertToColor;
                        VertToColor.SetNum(LocalMesh.MaxVertexID());

                        for (int32 vid : LocalMesh.VertexIndicesItr())
                        {
                            FVector3f N(0,0,1);
                            if (vid < VertToShared.Num())
                            {
                                int32 si = VertToShared[vid];
                                if (si >= 0 && si < NormalAccum.Num()) N = FVector3f(NormalAccum[si]);
                            }
                            if (vid < VertIsBackface.Num() && VertIsBackface[vid])
                            {
                                N = -N;
                            }

                            float Up = N.Z;
                            float FloorMask = FMath::SmoothStep(0.6f, 0.85f, Up);
                            float CeilingMask = FMath::SmoothStep(-0.6f, -0.85f, Up);
                            float WallMask = 1.0f - FMath::Max(FloorMask, CeilingMask);

                            FVector4f Color(WallMask, FloorMask, CeilingMask, 1.0f);
                            VertToColor[vid] = ColorOvl->AppendElement(Color);
                        }

                        for (int32 tid : LocalMesh.TriangleIndicesItr())
                        {
                            FIndex3i Tri = LocalMesh.GetTriangle(tid);
                            ColorOvl->SetTriangle(tid, FIndex3i(VertToColor[Tri[0]], VertToColor[Tri[1]], VertToColor[Tri[2]]));
                        }
                    }
                }

                // --- UVs ---
                FDynamicMeshUVOverlay* UVOvl = LocalMesh.Attributes()->PrimaryUV();
                if (UVOvl)
                {
                    UVOvl->ClearElements();
                    for (int32 tid : LocalMesh.TriangleIndicesItr())
                    {
                        FIndex3i Tri = LocalMesh.GetTriangle(tid);
                        FVector3d FaceN = LocalMesh.GetTriNormal(tid);
                        FVector3f AbsN(FMath::Abs(FaceN.X), FMath::Abs(FaceN.Y), FMath::Abs(FaceN.Z));
                        FIndex3i UTri;
                        for (int32 j = 0; j < 3; ++j)
                        {
                            FVector3d P = LocalMesh.GetVertex(Tri[j]);
                            FVector2f UV;
                            if (AbsN.X >= AbsN.Y && AbsN.X >= AbsN.Z) UV = FVector2f(P.Y, P.Z);
                            else if (AbsN.Y >= AbsN.Z) UV = FVector2f(P.X, P.Z);
                            else UV = FVector2f(P.X, P.Y);
                            UTri[j] = UVOvl->AppendElement(UV * Tiling);
                        }
                        UVOvl->SetTriangle(tid, UTri);
                    }
                }
            }

            return LocalMesh;
        }; // end BuildMesh

        // Build primary mesh
        FDynamicMesh3 ResultMesh = BuildMesh(VSize, bSmartColors, -1.0f);

        // Check if cancelled (BuildMesh returns empty mesh on cancel)
        if (bCancelRequested)
        {
            bIsGenerating = false;
            return;
        }

        // Build LOD mesh (coarser, preserve smart-color data so material swaps remain consistent)
        FDynamicMesh3 LODResultMesh;
        if (bLOD)
        {
            LODResultMesh = BuildMesh(VSize * (double)LODMult, bSmartColors, -1.0f);
            if (bCancelRequested)
            {
                bIsGenerating = false;
                return;
            }
        }

        // ---- Phase 6: Scattering Calculation (using primary mesh) ----
        TArray<TArray<FTransform>> ScatterResults;
        ScatterResults.SetNum(LocalDecorationLayers.Num());

        if (LocalDecorationLayers.Num() > 0 && ResultMesh.TriangleCount() > 0)
        {
            FRandomStream Rnd(Seed);

            for (int32 LayerIdx = 0; LayerIdx < LocalDecorationLayers.Num(); ++LayerIdx)
            {
                const FIOCScatterLayer& Layer = LocalDecorationLayers[LayerIdx];
                if (!Layer.Mesh || Layer.Density <= SMALL_NUMBER) continue;

                TArray<FTransform>& OutTransforms = ScatterResults[LayerIdx];
                double Density = (double)Layer.Density / 10000.0;
                double PoissonMult = (Layer.PoissonMinSeparation > SMALL_NUMBER) ? 30.0 : 1.0;

                double Accumulator = 0.0;

                for (int32 tid : ResultMesh.TriangleIndicesItr())
                {
                    FVector3d A, B, C;
                    ResultMesh.GetTriVertices(tid, A, B, C);
                    FVector3d FaceN = ResultMesh.GetTriNormal(tid);
                    double Area = VectorUtil::Area(A, B, C);

                    if (FaceN.Z < Layer.MinSlopeZ || FaceN.Z > Layer.MaxSlopeZ) continue;

                    double CountFloat = Area * Density * PoissonMult;
                    Accumulator += CountFloat;
                    int32 Count = (int32)Accumulator;
                    Accumulator -= Count;

                    for (int32 c = 0; c < Count; ++c)
                    {
                        double u = Rnd.FRand();
                        double v = Rnd.FRand();
                        if (u + v > 1.0) { u = 1.0 - u; v = 1.0 - v; }
                        double w = 1.0 - u - v;

                        FVector3d Pos = u*A + v*B + w*C;

                        FTransform T;
                        T.SetLocation((FVector)Pos);

                        if (Layer.bAlignToNormal)
                        {
                            FQuat Q = FQuat::FindBetweenNormals(FVector::UpVector, (FVector)FaceN);
                            FQuat QYaw(FVector::UpVector, Rnd.FRand() * PI * 2.0f);
                            T.SetRotation(Q * QYaw);
                        }
                        else
                        {
                            T.SetRotation(FQuat(FVector::UpVector, Rnd.FRand() * PI * 2.0f));
                        }

                        float S = Rnd.FRandRange(Layer.ScaleRange.X, Layer.ScaleRange.Y);
                        T.SetScale3D(FVector(S));

                        OutTransforms.Add(T);
                    }
                }

                // Poisson disk filter — applied only when PoissonMinSeparation > 0
                if (Layer.PoissonMinSeparation > SMALL_NUMBER)
                {
                    float R = Layer.PoissonMinSeparation;
                    auto ToCell = [R](FVector P) {
                        return FIntVector(FMath::FloorToInt(P.X / R),
                                         FMath::FloorToInt(P.Y / R),
                                         FMath::FloorToInt(P.Z / R));
                    };

                    // Shuffle candidates for unbiased dart-throwing
                    for (int32 i = OutTransforms.Num() - 1; i > 0; --i)
                        OutTransforms.Swap(i, Rnd.RandRange(0, i));

                    TMap<FIntVector, FVector> AcceptedCells;
                    TArray<FTransform> Accepted;

                    for (const FTransform& T : OutTransforms)
                    {
                        FVector Pos = T.GetLocation();
                        FIntVector Cell = ToCell(Pos);
                        bool bTooClose = false;

                        for (int32 dz = -2; dz <= 2 && !bTooClose; ++dz)
                            for (int32 dy = -2; dy <= 2 && !bTooClose; ++dy)
                                for (int32 dx = -2; dx <= 2 && !bTooClose; ++dx)
                                    if (FVector* NeighPos = AcceptedCells.Find(Cell + FIntVector(dx, dy, dz)))
                                        if (FVector::DistSquared(Pos, *NeighPos) < R * R) bTooClose = true;

                        if (!bTooClose)
                        {
                            AcceptedCells.Add(Cell, Pos);
                            Accepted.Add(T);
                        }
                    }
                    OutTransforms = MoveTemp(Accepted);
                }
            }
        }

        AsyncTask(ENamedThreads::GameThread, [WeakThis, ResultMesh = MoveTemp(ResultMesh), LODResultMesh = MoveTemp(LODResultMesh), ScatterResults = MoveTemp(ScatterResults), LocalDecorationLayers, bNavMesh, bLOD]() mutable {
            if (!WeakThis.IsValid()) return;
            AIOCProceduralActor* Actor = WeakThis.Get();

            // 1. Update primary mesh
            if (Actor->MeshComponent)
            {
                Actor->MeshComponent->GetDynamicMesh()->SetMesh(MoveTemp(ResultMesh));
            }

            // 2. Update LOD mesh
            if (bLOD && Actor->LODMeshComponent)
            {
                Actor->LODMeshComponent->GetDynamicMesh()->SetMesh(MoveTemp(LODResultMesh));
                Actor->LODMeshComponent->SetVisibility(false);
                Actor->bLODActive = false;
            }

            Actor->ApplyCaveMaterials();

            // 3. Update Decoration HISMs
            TArray<USceneComponent*> Children;
            Actor->GetRootComponent()->GetChildrenComponents(true, Children);
            for (USceneComponent* Child : Children)
            {
                if (UHierarchicalInstancedStaticMeshComponent* HISM = Cast<UHierarchicalInstancedStaticMeshComponent>(Child))
                {
                    if (HISM->ComponentTags.Contains(TEXT("IOC_Decor")))
                    {
                        HISM->DestroyComponent();
                    }
                }
            }

            if (LocalDecorationLayers.Num() == ScatterResults.Num())
            {
                for (int32 i = 0; i < ScatterResults.Num(); ++i)
                {
                    const TArray<FTransform>& Transforms = ScatterResults[i];
                    if (Transforms.Num() == 0) continue;

                    const FIOCScatterLayer& Layer = LocalDecorationLayers[i];
                    if (!Layer.Mesh) continue;

                    UHierarchicalInstancedStaticMeshComponent* HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor);
                    HISM->ComponentTags.Add(TEXT("IOC_Decor"));
                    HISM->SetStaticMesh(Layer.Mesh);
                    HISM->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
                    HISM->SetupAttachment(Actor->GetRootComponent());
                    HISM->RegisterComponent();

                    HISM->AddInstances(Transforms, false);
                }
            }

            // 4. NavMesh incremental rebuild
            if (bNavMesh && Actor->MeshComponent)
            {
                FNavigationSystem::UpdateComponentData(*Actor->MeshComponent);
            }

            // 5. Editor display state
            Actor->bIsGeneratingDisplay = false;

#if WITH_EDITOR
            if (GEditor)
            {
                GEditor->RedrawAllViewports(false);
            }
#endif

            Actor->bIsGenerating = false;
        });
    });
}
