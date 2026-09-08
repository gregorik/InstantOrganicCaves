// Copyright (c) 2026 GregOrigin. All Rights Reserved.


#include "IOCProceduralActor.h"
#include "IOCBiomeVolume.h"
#include "IOCSettings.h"
#include "InstantOrganicCavesModule.h"
#include "EngineUtils.h"

// --- CORE API ---
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "UDynamicMesh.h"
#include "Math/RandomStream.h"
#include "Misc/EngineVersionComparison.h"
#include "GenericPlatform/GenericPlatformMath.h" // For Perlin

#include "UObject/NoExportTypes.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/CollisionProfile.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Algo/Sort.h"
#include "HAL/PlatformTime.h"
#include "Components/BrushComponent.h"
#include "DrawDebugHelpers.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/Factory.h"
#include "Engine/StaticMesh.h"
#include "Editor.h"
#include "MeshDescription.h"
#include "ObjectTools.h"
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

    FORCEINLINE uint32 IOC_HashCell(int32 X, int32 Y, int32 Z)
    {
        uint32 H = 0x9E3779B9u;
        H ^= static_cast<uint32>(X) * 0x9E3779B1u;
        H = (H ^ (H >> 15)) * 0x85EBCA6Bu;
        H ^= static_cast<uint32>(Y) * 0x85EBCA77u;
        H = (H ^ (H >> 13)) * 0xC2B2AE35u;
        H ^= static_cast<uint32>(Z) * 0xC2B2AE3Du;
        return H ^ (H >> 16);
    }

    /**
     * Gradient noise with a hashed lattice instead of a permutation table.
     *
     * FMath::PerlinNoise3D masks its lattice coordinates with & 255, so the field repeats
     * every 256 units of noise space -- roughly every 512 m at the default 0.005 frequency,
     * which is plainly visible in streamed "infinite" caves, and large seed offsets fold
     * back onto the same lattice instead of producing an independent field. Hashing the
     * integer lattice directly removes the period entirely.
     *
     * Output range and distribution match FMath::PerlinNoise3D (same 12 edge gradients, same
     * 0.97 normalisation), so existing thresholds and amplitudes stay meaningful.
     */
    float IOC_GradientNoise3D(const FVector& P)
    {
        static const FVector3f Gradients[16] = {
            FVector3f( 1,  1,  0), FVector3f(-1,  1,  0), FVector3f( 1, -1,  0), FVector3f(-1, -1,  0),
            FVector3f( 1,  0,  1), FVector3f(-1,  0,  1), FVector3f( 1,  0, -1), FVector3f(-1,  0, -1),
            FVector3f( 0,  1,  1), FVector3f( 0, -1,  1), FVector3f( 0,  1, -1), FVector3f( 0, -1, -1),
            // Duplicates padding to 16 keep the index a mask instead of a modulo, which is
            // the standard trick and does not measurably bias the field.
            FVector3f( 1,  1,  0), FVector3f(-1,  1,  0), FVector3f( 0, -1,  1), FVector3f( 0, -1, -1)
        };

        const double Xfl = FMath::Floor(P.X);
        const double Yfl = FMath::Floor(P.Y);
        const double Zfl = FMath::Floor(P.Z);

        const int32 X0 = static_cast<int32>(Xfl);
        const int32 Y0 = static_cast<int32>(Yfl);
        const int32 Z0 = static_cast<int32>(Zfl);

        const float X = static_cast<float>(P.X - Xfl);
        const float Y = static_cast<float>(P.Y - Yfl);
        const float Z = static_cast<float>(P.Z - Zfl);

        auto Fade = [](float T) { return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f); };
        auto Corner = [&](int32 Ix, int32 Iy, int32 Iz, float Dx, float Dy, float Dz)
        {
            const FVector3f& G = Gradients[IOC_HashCell(Ix, Iy, Iz) & 15u];
            return G.X * Dx + G.Y * Dy + G.Z * Dz;
        };

        const float Xm1 = X - 1.0f;
        const float Ym1 = Y - 1.0f;
        const float Zm1 = Z - 1.0f;

        const float U = Fade(X);
        const float V = Fade(Y);
        const float W = Fade(Z);

        const float Result = FMath::Lerp(
            FMath::Lerp(
                FMath::Lerp(Corner(X0,     Y0,     Z0,     X, Y, Z),
                            Corner(X0 + 1, Y0,     Z0,     Xm1, Y, Z), U),
                FMath::Lerp(Corner(X0,     Y0 + 1, Z0,     X, Ym1, Z),
                            Corner(X0 + 1, Y0 + 1, Z0,     Xm1, Ym1, Z), U), V),
            FMath::Lerp(
                FMath::Lerp(Corner(X0,     Y0,     Z0 + 1, X, Y, Zm1),
                            Corner(X0 + 1, Y0,     Z0 + 1, Xm1, Y, Zm1), U),
                FMath::Lerp(Corner(X0,     Y0 + 1, Z0 + 1, X, Ym1, Zm1),
                            Corner(X0 + 1, Y0 + 1, Z0 + 1, Xm1, Ym1, Zm1), U), V), W);

        return FMath::Clamp(Result * 0.97f, -1.0f, 1.0f);
    }

    FORCEINLINE float IOC_Perlin01(const FVector& P)
    {
        return (IOC_GradientNoise3D(P) * 0.5f) + 0.5f;
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
            float Ridged = 1.0f - FMath::Abs(IOC_GradientNoise3D(Sample));
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
            // Project-configurable rather than a hardcoded path into the plugin's content,
            // so a project that forks the shipped materials can retarget it.
            const FSoftObjectPath& ConfiguredPath = UIOCSettings::Get().FallbackCaveMaterial;
            if (ConfiguredPath.IsValid())
            {
                CachedMaterial = Cast<UMaterialInterface>(ConfiguredPath.TryLoad());
            }

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

    FVector2f IOC_ProjectStablePlanarUV(const FVector3d& Position, const FVector3d& Normal, float Tiling)
    {
        FVector3d SafeNormal = Normal;
        if (SafeNormal.SquaredLength() <= SMALL_NUMBER)
        {
            SafeNormal = FVector3d(0.0, 0.0, 1.0);
        }
        else
        {
            SafeNormal.Normalize();
        }
        const FVector3d AbsNormal(FMath::Abs(SafeNormal.X), FMath::Abs(SafeNormal.Y), FMath::Abs(SafeNormal.Z));

        FVector2f UV;
        if (AbsNormal.X >= AbsNormal.Y && AbsNormal.X >= AbsNormal.Z)
        {
            UV = FVector2f(Position.Y, Position.Z);
        }
        else if (AbsNormal.Y >= AbsNormal.Z)
        {
            UV = FVector2f(Position.X, Position.Z);
        }
        else
        {
            UV = FVector2f(Position.X, Position.Y);
        }

        return UV * Tiling;
    }

    FQuat IOC_BuildScatterRotation(const FIOCScatterLayer& Layer, const FVector3d& FaceNormal, FRandomStream& Rnd)
    {
        FVector3d SafeFaceNormal = FaceNormal;
        if (SafeFaceNormal.SquaredLength() <= SMALL_NUMBER)
        {
            SafeFaceNormal = FVector3d(0.0, 0.0, 1.0);
        }
        else
        {
            SafeFaceNormal.Normalize();
        }

        const FVector SurfaceNormal = FVector(SafeFaceNormal);
        FQuat Rotation = Layer.bAlignToNormal
            ? FQuat::FindBetweenNormals(FVector::UpVector, SurfaceNormal)
            : FQuat::Identity;

        const FVector YawAxis = Layer.bAlignToNormal ? SurfaceNormal : FVector::UpVector;
        Rotation = FQuat(YawAxis, Rnd.FRand() * PI * 2.0f) * Rotation;

        if (Layer.RandomPitch > SMALL_NUMBER)
        {
            const float PitchRadians = FMath::DegreesToRadians(
                Rnd.FRandRange(-Layer.RandomPitch, Layer.RandomPitch));
            const FVector PitchAxis = Rotation.RotateVector(FVector::RightVector);
            Rotation = FQuat(PitchAxis, PitchRadians) * Rotation;
        }

        return Rotation;
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
    PrimaryActorTick.TickInterval = 0.1f;

    bReplicates = true;
    SetReplicateMovement(true);
    SetNetUpdateFrequency(2.0f);
    SetMinNetUpdateFrequency(0.5f);

    CavePreset = EIOCCavePreset::LargeTunnel;
    bGenerateTunnel = true;
    ApplyPresetParams(); // Set params only — no GenerateCave() during construction

    MeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("ProceduralMesh"));
    RootComponent = MeshComponent;

    MeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
    MeshComponent->SetComplexAsSimpleCollisionEnabled(true, false);

    // Without this, every mesh edit re-cooks complex-as-simple collision synchronously on
    // the game thread. Generation touches the mesh several times per run, so collision is
    // cooked once, explicitly, when a generation completes.
    MeshComponent->SetDeferredCollisionUpdatesEnabled(true, false);

    LODMeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("LODMesh"));
    LODMeshComponent->SetupAttachment(RootComponent);
    LODMeshComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
    LODMeshComponent->SetVisibility(false);

    CaveSpline = CreateDefaultSubobject<USplineComponent>(TEXT("CaveSpline"));
    CaveSpline->SetupAttachment(RootComponent);
    IOC_ConfigureTunnelSpline(CaveSpline);
    IOC_SyncSplineToTunnelLine(CaveSpline, TunnelStart, TunnelEnd, TunnelRadius);

    // Default cave material - two-sided rock shipped with the plugin. This stays a
    // ConstructorHelpers lookup because it has to resolve during CDO construction, before
    // config-driven settings are meaningful; UIOCSettings::FallbackCaveMaterial covers the
    // runtime path for actors whose material is cleared.
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultCaveMat(
        TEXT("/InstantOrganicCaves/MI_IOC_CaveWalls.MI_IOC_CaveWalls"));
    if (DefaultCaveMat.Succeeded())
    {
        CaveMaterial = DefaultCaveMat.Object;
    }

    ApplyCaveMaterials();
    SyncLODVisibility();
}

void AIOCProceduralActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AIOCProceduralActor, CavePreset);
    DOREPLIFETIME(AIOCProceduralActor, bGenerateTunnel);
    DOREPLIFETIME(AIOCProceduralActor, bUseSpline);
    DOREPLIFETIME(AIOCProceduralActor, TunnelStart);
    DOREPLIFETIME(AIOCProceduralActor, TunnelEnd);
    DOREPLIFETIME(AIOCProceduralActor, TunnelRadius);
    DOREPLIFETIME(AIOCProceduralActor, WallThickness);
    DOREPLIFETIME(AIOCProceduralActor, CaveSeed);
    DOREPLIFETIME(AIOCProceduralActor, GenerationBounds);
    DOREPLIFETIME(AIOCProceduralActor, VoxelSize);
    DOREPLIFETIME(AIOCProceduralActor, NoiseFrequency);
    DOREPLIFETIME(AIOCProceduralActor, NoiseThreshold);
    DOREPLIFETIME(AIOCProceduralActor, SmoothIterations);
    DOREPLIFETIME(AIOCProceduralActor, NoiseOctaves);
    DOREPLIFETIME(AIOCProceduralActor, NoiseLacunarity);
    DOREPLIFETIME(AIOCProceduralActor, NoisePersistence);
    DOREPLIFETIME(AIOCProceduralActor, MacroChamberWeight);
    DOREPLIFETIME(AIOCProceduralActor, RidgedDetailWeight);
    DOREPLIFETIME(AIOCProceduralActor, InteriorDensityBias);
    DOREPLIFETIME(AIOCProceduralActor, DomainWarpIntensity);
    DOREPLIFETIME(AIOCProceduralActor, TerraceSteps);
    DOREPLIFETIME(AIOCProceduralActor, MaxVoxelCount);
    DOREPLIFETIME(AIOCProceduralActor, MaxGeneratedTriangles);
    DOREPLIFETIME(AIOCProceduralActor, MaxScatterInstances);
    DOREPLIFETIME(AIOCProceduralActor, CaveMaterial);
    DOREPLIFETIME(AIOCProceduralActor, TextureTiling);
    DOREPLIFETIME(AIOCProceduralActor, bGenerateSmartColors);
    DOREPLIFETIME(AIOCProceduralActor, DecorationLayers);
    DOREPLIFETIME(AIOCProceduralActor, bEnableLOD);
    DOREPLIFETIME(AIOCProceduralActor, LODDistance);
    DOREPLIFETIME(AIOCProceduralActor, LODVoxelSizeMultiplier);
    DOREPLIFETIME(AIOCProceduralActor, bUseWorldSpaceNoise);
    DOREPLIFETIME(AIOCProceduralActor, bUseFixedBoundsForTunnel);
    DOREPLIFETIME(AIOCProceduralActor, RuntimeCarves);
}

void AIOCProceduralActor::ApplyCaveMaterialToComponent(UDynamicMeshComponent* TargetComponent) const
{
    if (!TargetComponent)
    {
        return;
    }

    UMaterialInterface* MaterialToApply = CaveMaterial ? ToRawPtr(CaveMaterial) : IOC_GetFallbackCaveMaterial();

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

void AIOCProceduralActor::SyncLODVisibility()
{
    const bool bCanShowLOD = bEnableLOD && bHasValidLOD && bLODActive && MeshComponent && LODMeshComponent;

    if (!bEnableLOD || !MeshComponent || !LODMeshComponent)
    {
        bLODActive = false;
    }

    if (MeshComponent)
    {
        MeshComponent->SetVisibility(!bCanShowLOD);
    }

    if (LODMeshComponent)
    {
        LODMeshComponent->SetVisibility(bCanShowLOD);
    }
}

void AIOCProceduralActor::BeginPlay()
{
    Super::BeginPlay();

    // The replicated history calls back into this actor from PostReplicatedReceive, so the
    // link must exist on clients too, before any carve can arrive.
    RuntimeCarves.Owner = this;

    ApplyCaveMaterials();

    // PIE duplication can copy an in-flight generation flag from the editor world.
    // Reset so BeginPlay can force a fresh generation.
    if (bIsGenerating)
    {
        bIsGenerating = false;
        bIsGeneratingDisplay = false;
        if (bLogPresetDebug)
        {
            UE_LOG(LogIOC, Warning, TEXT("BeginPlay: Reset bIsGenerating copied from editor world."));
        }
    }

    bLODActive = false;
    SyncLODVisibility();

    if (bLogPresetDebug)
    {
        UE_LOG(LogIOC, Warning, TEXT("BeginPlay: Preset=%d Force=%d Tunnel=%d Spline=%d Start=%s End=%s Radius=%.1f Wall=%.1f Voxel=%.1f Bounds=%s"),
            (int32)CavePreset, bForcePreset, bGenerateTunnel, bUseSpline,
            *TunnelStart.ToString(), *TunnelEnd.ToString(),
            TunnelRadius, WallThickness, (float)VoxelSize, *GenerationBounds.ToString());
    }

    // Presets expand into concrete recipe values (tunnel endpoints, radius, wall, voxel
    // size) and every one of those fields is replicated. A client that re-expanded the
    // preset here would overwrite the values it just received and build geometry -- and
    // collision -- the server does not have. Authority resolves presets; peers generate
    // from the replicated recipe verbatim.
    if (HasAuthority())
    {
        if (bForcePreset && CavePreset == EIOCCavePreset::Custom)
        {
            CavePreset = EIOCCavePreset::LargeTunnel;
        }

        if (bForcePreset || CavePreset != EIOCCavePreset::Custom)
        {
            ApplyPresetParams();
            if (bLogPresetDebug)
            {
                UE_LOG(LogIOC, Warning, TEXT("BeginPlay: applied preset%s. Preset=%d Tunnel=%d Radius=%.1f Wall=%.1f Voxel=%.1f NFreq=%.6f"),
                    bForcePreset ? TEXT(" (forced)") : TEXT(""),
                    (int32)CavePreset, bGenerateTunnel, TunnelRadius, WallThickness, (float)VoxelSize, NoiseFrequency);
            }
        }
    }

    // The legacy ShouldAutoPreset() migration deliberately does not run here. It is a
    // value-match heuristic over user-editable fields, so at runtime it would silently
    // convert a legitimately configured actor -- a streamed chunk whose voxel size and
    // frequency happen to match the defaults, for instance -- into a tunnel. Migration is
    // editor-only; see OnConstruction.

    GenerateCave();
}

void AIOCProceduralActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    CancelActiveGeneration();
    Super::EndPlay(EndPlayReason);
}

void AIOCProceduralActor::Destroyed()
{
    CancelActiveGeneration();
    Super::Destroyed();
}

void AIOCProceduralActor::CancelActiveGeneration()
{
    const bool bWasGenerating = bIsGenerating || ActiveGeneration.IsValid();
    if (ActiveGeneration.IsValid())
    {
        ActiveGeneration->bCancelRequested.store(true);
        ActiveGeneration.Reset();
    }

    bIsGenerating = false;
    bIsGeneratingDisplay = false;
    bPendingRegeneration = false;

    if (bWasGenerating)
    {
        BroadcastGenerationFinished(true, false);
    }
}

void AIOCProceduralActor::RequestRegeneration()
{
    if (IsActorBeingDestroyed() || !HasActorBegunPlay())
    {
        return;
    }

    if (ActiveGeneration.IsValid())
    {
        ActiveGeneration->bCancelRequested.store(true);
    }

    bPendingRegeneration = true;
    SetActorTickEnabled(true);
}

void AIOCProceduralActor::OnRep_GenerationSettings()
{
    ApplyCaveMaterials();
    RequestRegeneration();
}

void AIOCProceduralActor::BroadcastGenerationFinished(bool bCancelled, bool bWillRegenerate)
{
    OnGenerationFinished.Broadcast(this, bCancelled, bWillRegenerate);
    OnGenerationFinishedEvent.Broadcast(this, bCancelled, bWillRegenerate);
}

void AIOCProceduralActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

#if WITH_EDITOR
    const UWorld* TickWorld = GetWorld();
    if (TickWorld && !TickWorld->IsGameWorld())
    {
        // Editor viewport tick: refresh the configuration gizmos and do nothing else.
        DrawConfigurationDebug();
        return;
    }
#endif

    // Retry generation if a previous call was cancelled
    if (bPendingRegeneration && !bIsGenerating)
    {
        bPendingRegeneration = false;
        GenerateCave();
    }

    if (!bEnableLOD || !LODMeshComponent || !MeshComponent)
    {
        if (bLODActive)
        {
            SyncLODVisibility();
        }
        return;
    }

    APlayerCameraManager* CamMgr = nullptr;
    if (GetWorld() && GetWorld()->GetFirstPlayerController())
    {
        CamMgr = GetWorld()->GetFirstPlayerController()->PlayerCameraManager;
    }
    if (!CamMgr) return;

    const FVector CameraLocation = CamMgr->GetCameraLocation();
    float Dist = FVector::Dist(GetActorLocation(), CameraLocation);
    if (MeshComponent->Bounds.SphereRadius > KINDA_SMALL_NUMBER)
    {
        Dist = FMath::Sqrt(MeshComponent->Bounds.GetBox().ComputeSquaredDistanceToPoint(CameraLocation));
    }

    if (!bLODActive && Dist > LODDistance + 250.0f)
    {
        bLODActive = true;
        SyncLODVisibility();
    }
    else if (bLODActive && Dist < LODDistance - 250.0f)
    {
        bLODActive = false;
        SyncLODVisibility();
    }
}

void AIOCProceduralActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    RuntimeCarves.Owner = this;

    ApplyCaveMaterials();
    SyncLODVisibility();
    IOC_ConfigureTunnelSpline(CaveSpline);

#if WITH_EDITOR
    // Content migration rewrites the actor's saved recipe, so it is confined to a genuine
    // editor world. OnConstruction also runs in PIE and on runtime-spawned actors, where
    // rewriting the recipe would fight replication and the streaming manager.
    const UWorld* ConstructionWorld = GetWorld();
    const bool bIsEditorPreviewWorld = ConstructionWorld && !ConstructionWorld->IsGameWorld();

    if (bIsEditorPreviewWorld)
    {
        if (bForcePreset)
        {
            if (CavePreset == EIOCCavePreset::Custom)
            {
                CavePreset = EIOCCavePreset::LargeTunnel;
            }
            ApplyPreset();
            return;
        }

        // Legacy migration: presets imply tunnel mode. If an older asset saved with tunnel
        // off, re-apply the preset to restore expected behavior in-editor.
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
    }

    DrawConfigurationDebug();
#endif
}

#if WITH_EDITOR
void AIOCProceduralActor::DrawConfigurationDebug() const
{
    if (bShowDebugViz)
    {
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
}

bool AIOCProceduralActor::ShouldTickIfViewportsOnly() const
{
    // Debug lines drawn with LifeTime < 0 go to the transient line batcher and expire after
    // ~1 second, so issuing them once from OnConstruction made the gizmos flash and vanish.
    // Ticking in the editor viewport lets them be re-issued while the box is ticked.
    return bShowDebugViz;
}
#endif

#if WITH_EDITOR
void AIOCProceduralActor::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);

    if (bShowDebugViz)
    {
        DrawConfigurationDebug();
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
    DrawConfigurationDebug();

    // Regenerating is seconds of work on a large cave, so properties that cannot change the
    // generated geometry must not trigger it. Bake options, debug toggles, the LOD switch
    // distance and the runtime-carve limits are all consumed later, not during generation.
    if (PropertyChangedEvent.Property)
    {
        static const TSet<FName> NonGeometryProperties = {
            GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, bShowDebugViz),
            GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, bLogPresetDebug),
            GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, bForcePreset),
            GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, bAutoRebuildNavMesh),
            GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, LODDistance),
            GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, MaxRuntimeCarves),
            GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, MinRuntimeCarveRadius),
            GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, MaxRuntimeCarveRadius),
            GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, BakedAssetBaseName),
            GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, BakeMaterialOverride),
            GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, bBakeGeneratedLOD),
            GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, bBakeEnableNanite),
            GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, BakeCollisionMode),
            GET_MEMBER_NAME_CHECKED(AIOCProceduralActor, BakedLODScreenSize),
        };

        if (NonGeometryProperties.Contains(PropertyChangedEvent.Property->GetFName()))
        {
            return;
        }
    }

    // Single generation call for all remaining property changes
    GenerateCave();
}

void AIOCProceduralActor::BakeToStaticMesh()
{
    const FDynamicMesh3* RawMesh = MeshComponent ? MeshComponent->GetMesh() : nullptr;
    if (!RawMesh || RawMesh->TriangleCount() == 0)
    {
        UE_LOG(LogIOC, Warning, TEXT("BakeToStaticMesh: No mesh to bake. Run generation first."));
        return;
    }

    const FDynamicMesh3* RawLODMesh = (bBakeGeneratedLOD && bEnableLOD && LODMeshComponent)
        ? LODMeshComponent->GetMesh()
        : nullptr;

    // ---- 1. Stable base name + unique asset path ----
    FString SafeBaseName = ObjectTools::SanitizeObjectName(BakedAssetBaseName.TrimStartAndEnd());
    if (SafeBaseName.IsEmpty())
    {
        SafeBaseName = TEXT("SM_IOC_Cave");
    }

    FString PackageName;
    FString AssetName;
    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    AssetToolsModule.Get().CreateUniqueAssetName(
        FString::Printf(TEXT("/Game/IOC_Baked/%s"), *SafeBaseName),
        FString(),
        PackageName,
        AssetName);

    UPackage* Package = CreatePackage(*PackageName);
    Package->FullyLoad();

    UStaticMesh* SM = NewObject<UStaticMesh>(Package, FName(*AssetName), RF_Public | RF_Standalone);
    SM->InitResources();

    // ---- 2. Convert one dynamic mesh into a production static-mesh LOD ----
    auto AddDynamicMeshLOD = [SM](const FDynamicMesh3& DynMesh, int32 LODIndex, float ScreenSize) -> bool
    {
        if (DynMesh.TriangleCount() == 0)
        {
            return false;
        }

        FMeshDescription MeshDesc;
        FStaticMeshAttributes Attrs(MeshDesc);
        Attrs.Register();

        auto VertPositions = Attrs.GetVertexPositions();
        auto VINormals     = Attrs.GetVertexInstanceNormals();
        auto VIUVs         = Attrs.GetVertexInstanceUVs();
        auto VIColors      = Attrs.GetVertexInstanceColors();
        auto PGSlotNames   = Attrs.GetPolygonGroupMaterialSlotNames();
        VIUVs.SetNumChannels(1);

        const FPolygonGroupID PGrp = MeshDesc.CreatePolygonGroup();
        PGSlotNames[PGrp] = FName(TEXT("Cave_Material"));

        TArray<FVertexID> VMap;
        VMap.SetNum(DynMesh.MaxVertexID());
        for (int32 VertexId : DynMesh.VertexIndicesItr())
        {
            const FVertexID MeshVertexId = MeshDesc.CreateVertex();
            VertPositions[MeshVertexId] = FVector3f(DynMesh.GetVertex(VertexId));
            VMap[VertexId] = MeshVertexId;
        }

        const bool bHasNormals = DynMesh.HasAttributes() && DynMesh.Attributes()->PrimaryNormals() != nullptr;
        const bool bHasUVs = DynMesh.HasAttributes() && DynMesh.Attributes()->PrimaryUV() != nullptr;
        const bool bHasColors = DynMesh.HasAttributes() && DynMesh.Attributes()->PrimaryColors() != nullptr;

        for (int32 TriangleId : DynMesh.TriangleIndicesItr())
        {
            const FIndex3i Tri = DynMesh.GetTriangle(TriangleId);
            FVertexInstanceID Instances[3];

            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const FVertexInstanceID Instance = MeshDesc.CreateVertexInstance(VMap[Tri[Corner]]);
                VINormals[Instance] = FVector3f::UnitZ();
                VIUVs.Set(Instance, 0, FVector2f::ZeroVector);
                VIColors[Instance] = FVector4f(1, 1, 1, 1);

                if (bHasNormals)
                {
                    const FIndex3i NormalTri = DynMesh.Attributes()->PrimaryNormals()->GetTriangle(TriangleId);
                    VINormals[Instance] = DynMesh.Attributes()->PrimaryNormals()->GetElement(NormalTri[Corner]);
                }
                if (bHasUVs)
                {
                    const FIndex3i UVTri = DynMesh.Attributes()->PrimaryUV()->GetTriangle(TriangleId);
                    VIUVs.Set(Instance, 0, DynMesh.Attributes()->PrimaryUV()->GetElement(UVTri[Corner]));
                }
                if (bHasColors)
                {
                    const FIndex3i ColorTri = DynMesh.Attributes()->PrimaryColors()->GetTriangle(TriangleId);
                    VIColors[Instance] = DynMesh.Attributes()->PrimaryColors()->GetElement(ColorTri[Corner]);
                }

                Instances[Corner] = Instance;
            }

            MeshDesc.CreatePolygon(PGrp, TArrayView<const FVertexInstanceID>(Instances, 3));
        }

        if (MeshDesc.Polygons().Num() == 0)
        {
            return false;
        }

        FStaticMeshSourceModel& SourceModel = SM->AddSourceModel();
        SourceModel.ScreenSize.Default = ScreenSize;
        SourceModel.BuildSettings.bRecomputeNormals = false;
        SourceModel.BuildSettings.bRecomputeTangents = true;
        SourceModel.BuildSettings.bGenerateLightmapUVs = true;
        SourceModel.BuildSettings.SrcLightmapIndex = 0;
        SourceModel.BuildSettings.DstLightmapIndex = 1;
        SourceModel.BuildSettings.bRemoveDegenerates = true;

        *SM->CreateMeshDescription(LODIndex) = MoveTemp(MeshDesc);
        SM->CommitMeshDescription(LODIndex);
        return true;
    };

    UMaterialInterface* BakedMaterial = BakeMaterialOverride
        ? ToRawPtr(BakeMaterialOverride)
        : (CaveMaterial ? ToRawPtr(CaveMaterial) : IOC_GetFallbackCaveMaterial());
    SM->GetStaticMaterials().Add(FStaticMaterial(BakedMaterial, FName(TEXT("Cave_Material"))));

    const int32 NumSourceTris = RawMesh->TriangleCount();
    if (!AddDynamicMeshLOD(*RawMesh, 0, 1.0f))
    {
        UE_LOG(LogIOC, Warning, TEXT("BakeToStaticMesh: LOD0 conversion produced no polygons."));
        SM->MarkAsGarbage();
        return;
    }

    bool bAddedLOD1 = false;
    if (RawLODMesh && RawLODMesh->TriangleCount() > 0 && RawLODMesh->TriangleCount() < RawMesh->TriangleCount())
    {
        bAddedLOD1 = AddDynamicMeshLOD(*RawLODMesh, 1, FMath::Clamp(BakedLODScreenSize, 0.01f, 0.99f));
    }

    // UStaticMesh gained Get/SetNaniteSettings in 5.7; 5.5 and 5.6 only expose the field.
    // ENGINE_MINOR_VERSION alone is not a version test -- it ignores the major version, so it
    // would also be "true" for 4.7 through 4.27.
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
    FMeshNaniteSettings NaniteSettings = SM->GetNaniteSettings();
    NaniteSettings.bEnabled = bBakeEnableNanite;
    SM->SetNaniteSettings(NaniteSettings);
#else
    SM->NaniteSettings.bEnabled = bBakeEnableNanite;
#endif
    SM->SetLightMapCoordinateIndex(1);
    SM->SetLightMapResolution(64);

    SM->CreateBodySetup();
    if (UBodySetup* BodySetup = SM->GetBodySetup())
    {
        BodySetup->CollisionTraceFlag = BakeCollisionMode == EIOCBakeCollisionMode::ComplexAsSimple
            ? CTF_UseComplexAsSimple
            : CTF_UseDefault;
        BodySetup->InvalidatePhysicsData();
    }

    // ---- 3. Build, register, notify ----
    SM->Build(false);
    SM->PostEditChange();

    FAssetRegistryModule::GetRegistry().AssetCreated(SM);
    Package->MarkPackageDirty();

    UE_LOG(LogIOC, Display,
        TEXT("Baked '%s' (%d tris, %d LODs, collision=%s, Nanite=%s) -> %s"),
        *AssetName,
        NumSourceTris,
        bAddedLOD1 ? 2 : 1,
        BakeCollisionMode == EIOCBakeCollisionMode::ComplexAsSimple ? TEXT("ComplexAsSimple") : TEXT("ProjectDefault"),
        bBakeEnableNanite ? TEXT("enabled") : TEXT("disabled"),
        *PackageName);
    GEditor->SyncBrowserToObjects(TArray<UObject*>{SM});
}
#endif

void AIOCProceduralActor::RegenerateInEditor()
{
    GenerateCave();
}

/**
 * Per-stage wall-clock accumulation for one generation, logged when bLogPresetDebug is set.
 *
 * Worth having permanently: the cost of a cave is very unevenly distributed between the
 * parallel voxel stages and the single-threaded meshing stages, and which one dominates
 * depends heavily on voxel size and bounds. Guessing wrong sends optimisation effort at the
 * minor term.
 */
struct FIOCStageTimings
{
    double Fill = 0.0;
    double Carve = 0.0;
    double HoleFill = 0.0;
    double Extract = 0.0;
    double Smooth = 0.0;
    double MeshBuild = 0.0;
    double Overlays = 0.0;
    double Scatter = 0.0;

    double SumBuild() const { return Fill + Carve + HoleFill + Extract + Smooth + MeshBuild + Overlays; }
};

// Internal optimized structure for fast segment checks
struct FIOCSegment
{
    FVector A;
    FVector BA; // B - A
    float LengthSq;
};

/**
 * Uniform grid binning tunnel segments by position.
 *
 * Sampling a spline at voxel resolution (which is what following the actual curve rather
 * than chording its control points requires) produces hundreds of segments, and the naive
 * nearest-segment scan is O(voxels * segments). Cells are at least as large as the maximum
 * distance at which a segment can still influence a voxel, and each segment is inserted into
 * every cell its padded bounds touch, so a point only ever needs to consult its own cell.
 */
struct FIOCSegmentGrid
{
    FVector Origin = FVector::ZeroVector;
    double CellSize = 1.0;
    FIntVector Dims = FIntVector(1, 1, 1);
    TArray<TArray<int32>> Cells;
    bool bValid = false;

    FORCEINLINE int32 CellIndex(int32 X, int32 Y, int32 Z) const
    {
        return X + Y * Dims.X + Z * Dims.X * Dims.Y;
    }

    FORCEINLINE FIntVector ToCellClamped(const FVector& P) const
    {
        return FIntVector(
            FMath::Clamp(FMath::FloorToInt((P.X - Origin.X) / CellSize), 0, Dims.X - 1),
            FMath::Clamp(FMath::FloorToInt((P.Y - Origin.Y) / CellSize), 0, Dims.Y - 1),
            FMath::Clamp(FMath::FloorToInt((P.Z - Origin.Z) / CellSize), 0, Dims.Z - 1));
    }

    void Build(const TArray<FIOCSegment>& Segments, double InfluenceRadius)
    {
        Cells.Reset();
        bValid = false;
        if (Segments.IsEmpty())
        {
            return;
        }

        const double Pad = FMath::Max(InfluenceRadius, 1.0);

        FVector Min(TNumericLimits<double>::Max());
        FVector Max(-TNumericLimits<double>::Max());
        for (const FIOCSegment& Seg : Segments)
        {
            const FVector B = Seg.A + Seg.BA;
            Min = Min.ComponentMin(Seg.A).ComponentMin(B);
            Max = Max.ComponentMax(Seg.A).ComponentMax(B);
        }
        Min -= FVector(Pad);
        Max += FVector(Pad);

        Origin = Min;
        CellSize = Pad;

        const FVector Span = Max - Min;
        auto AxisDim = [this](double Length)
        {
            return FMath::Clamp(FMath::CeilToInt(Length / CellSize), 1, 128);
        };
        Dims = FIntVector(AxisDim(Span.X), AxisDim(Span.Y), AxisDim(Span.Z));

        // A clamped dimension means the cells have to grow to still span the bounds.
        CellSize = FMath::Max(CellSize, Span.X / (double)Dims.X);
        CellSize = FMath::Max(CellSize, Span.Y / (double)Dims.Y);
        CellSize = FMath::Max(CellSize, Span.Z / (double)Dims.Z);

        Cells.SetNum(Dims.X * Dims.Y * Dims.Z);

        for (int32 SegIndex = 0; SegIndex < Segments.Num(); ++SegIndex)
        {
            const FIOCSegment& Seg = Segments[SegIndex];
            const FVector B = Seg.A + Seg.BA;
            const FIntVector C0 = ToCellClamped(Seg.A.ComponentMin(B) - FVector(Pad));
            const FIntVector C1 = ToCellClamped(Seg.A.ComponentMax(B) + FVector(Pad));

            for (int32 z = C0.Z; z <= C1.Z; ++z)
            {
                for (int32 y = C0.Y; y <= C1.Y; ++y)
                {
                    for (int32 x = C0.X; x <= C1.X; ++x)
                    {
                        Cells[CellIndex(x, y, z)].Add(SegIndex);
                    }
                }
            }
        }

        bValid = true;
    }

    /** Segments that can influence P, or nullptr when the grid is unbuilt. */
    FORCEINLINE const TArray<int32>* GetCandidates(const FVector& P) const
    {
        if (!bValid)
        {
            return nullptr;
        }
        const FIntVector C = ToCellClamped(P);
        return &Cells[CellIndex(C.X, C.Y, C.Z)];
    }
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
    if (!HasAuthority())
    {
        UE_LOG(LogIOC, Warning, TEXT("CarveAtLocation rejected on non-authority actor '%s'. Route the request through authoritative gameplay code."), *GetName());
        return;
    }

    if (WorldLocation.ContainsNaN() || !FMath::IsFinite(Radius) || MaxRuntimeCarves <= 0)
    {
        UE_LOG(LogIOC, Warning, TEXT("Invalid or disabled runtime carve request on '%s'."), *GetName());
        return;
    }

    FIOCCarvingCapture Carve;
    Carve.ShapeType = EIOCCarvingShape::Sphere;
    Carve.SphereRadius = FMath::Clamp(Radius, MinRuntimeCarveRadius, FMath::Max(MinRuntimeCarveRadius, MaxRuntimeCarveRadius));
    Carve.FalloffRadius = 50.0f; 
    Carve.WorldTransform = FTransform(WorldLocation);

    const int32 SafeMaxCarves = FMath::Clamp(MaxRuntimeCarves, 1, 4096);
    const bool bEvictedOldest = RuntimeCarves.Items.Num() >= SafeMaxCarves;
    if (bEvictedOldest)
    {
        RuntimeCarves.Items.RemoveAt(
            0, RuntimeCarves.Items.Num() - SafeMaxCarves + 1, EAllowShrinking::No);
    }

    FIOCCarveHistoryItem& NewItem = RuntimeCarves.Items.AddDefaulted_GetRef();
    NewItem.Carve = Carve;

    if (bEvictedOldest)
    {
        // A removal invalidates the replication keys of everything after it, so the delta has
        // to degrade to a full array update. Only the FIFO-eviction case pays that.
        RuntimeCarves.MarkArrayDirty();
    }
    else
    {
        // The common case: one new element, one item marked, one small delta on the wire.
        RuntimeCarves.MarkItemDirty(NewItem);
    }

    ForceNetUpdate();
    RequestCarveRebuild();
}

void AIOCProceduralActor::RequestCarveRebuild()
{
    // A carve currently costs a full revoxelisation, so a burst of them in one frame used to
    // mean one full rebuild each. Routing through the pending-regeneration pump collapses a
    // burst into a single rebuild on the next tick. Outside of play (editor tooling) there is
    // no tick to pump it, so generate directly.
    if (HasActorBegunPlay() && !IsActorBeingDestroyed())
    {
        RequestRegeneration();
    }
    else
    {
        GenerateCave();
    }
}

void AIOCProceduralActor::ClearRuntimeCarves()
{
    if (!HasAuthority())
    {
        UE_LOG(LogIOC, Warning, TEXT("ClearRuntimeCarves rejected on non-authority actor '%s'."), *GetName());
        return;
    }

    if (RuntimeCarves.IsEmpty())
    {
        return;
    }

    RuntimeCarves.Items.Reset();
    RuntimeCarves.MarkArrayDirty();
    ForceNetUpdate();
    RequestCarveRebuild();
}

TArray<FIOCCarvingCapture> AIOCProceduralActor::GetRuntimeCarves() const
{
    TArray<FIOCCarvingCapture> Result;
    RuntimeCarves.AppendCapturesTo(Result);
    return Result;
}

FString AIOCProceduralActor::GetPerformanceSummary() const
{
    return FString::Printf(
        TEXT("%.2fs, %lld voxels, %d tris, %d LOD tris, %d scattered instances"),
        LastGenerationTimeSeconds,
        LastEstimatedVoxelCount,
        LastPrimaryTriangleCount,
        LastLODTriangleCount,
        LastScatterInstanceCount);
}


void AIOCProceduralActor::GenerateCave()
{
    if (bIsGenerating)
    {
        if (!ActiveGeneration.IsValid())
        {
            bIsGenerating = false;
            bIsGeneratingDisplay = false;
        }
        else
        {
            // Signal the in-flight generation to abort; completion will retry on the game thread.
            ActiveGeneration->bCancelRequested.store(true);
            bPendingRegeneration = true;
            if (bLogPresetDebug)
            {
                UE_LOG(LogIOC, Warning, TEXT("GenerateCave: Cancelling in-flight generation, will retry."));
            }
            return;
        }
    }
    bIsGenerating = true;
    bIsGeneratingDisplay = true;
    bPendingRegeneration = false;
    bLastGenerationSucceeded = false;
    LastGenerationError.Reset();
    OnGenerationStarted.Broadcast(this);
    OnGenerationStartedEvent.Broadcast(this);
    ActiveGeneration = MakeShared<FIOCGenerationState, ESPMode::ThreadSafe>();
    TSharedPtr<FIOCGenerationState, ESPMode::ThreadSafe> GenerationState = ActiveGeneration;
    const double GenerationStartSeconds = FPlatformTime::Seconds();

    // Project settings are read once here, on the game thread, and captured by value into
    // the async task below. Reading a UObject CDO from a worker thread would be a race.
    const UIOCSettings& Settings = UIOCSettings::Get();
    const int32 MaxGridAxis = FMath::Clamp(Settings.MaxGridAxis, 16, 8192);
    const int32 MaxSplineSamples = FMath::Clamp(Settings.MaxSplineSamples, 16, 16384);
    const bool bWeldVertices = Settings.bWeldGeneratedVertices;
    const bool bLogStageTimings = bLogPresetDebug;

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
            Data.Priority = Vol->Priority;

            UPrimitiveComponent* BoundsComponent = Cast<UPrimitiveComponent>(Vol->GetBrushComponent());
            if (!BoundsComponent)
            {
                BoundsComponent = Cast<UPrimitiveComponent>(Vol->GetRootComponent());
            }

            if (BoundsComponent)
            {
                const FBoxSphereBounds LocalBounds = BoundsComponent->CalcBounds(FTransform::Identity);
                Data.LocalCenter = LocalBounds.Origin;
                Data.LocalExtents = LocalBounds.BoxExtent;
            }
            
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

        // Follow the actual curve. Reading only GetLocationAtSplinePoint() chords the
        // control polygon, so a two-point spline with tangents -- the shape the editor hands
        // you by default -- generated a dead-straight tunnel, and any curve that bulged past
        // its chords was clipped by bounds derived from those same chords. Sampling by arc
        // length fixes both; FIOCSegmentGrid keeps the extra segments cheap to query.
        const double SampleStep = FMath::Max3((double)VoxelSize * 2.0, (double)TunnelRadius * 0.5, 10.0);

        for (USplineComponent* Spline : AllSplines)
        {
            if (!Spline || Spline->GetNumberOfSplinePoints() < 2) continue;

            const double SplineLength = (double)Spline->GetSplineLength();
            if (SplineLength <= (double)KINDA_SMALL_NUMBER) continue;

            const int32 NumSamples = FMath::Clamp(
                FMath::CeilToInt(SplineLength / SampleStep), 1, MaxSplineSamples);

            FVector Prev = ActorInvTransform.TransformPosition(
                Spline->GetLocationAtDistanceAlongSpline(0.0f, ESplineCoordinateSpace::World));

            if (!bFoundPoints) { MinBounds = Prev; MaxBounds = Prev; bFoundPoints = true; }
            MinBounds = MinBounds.ComponentMin(Prev);
            MaxBounds = MaxBounds.ComponentMax(Prev);

            for (int32 SampleIndex = 1; SampleIndex <= NumSamples; ++SampleIndex)
            {
                const float Distance = (float)(SplineLength * ((double)SampleIndex / (double)NumSamples));
                const FVector Curr = ActorInvTransform.TransformPosition(
                    Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World));

                FIOCSegment Seg;
                Seg.A = Prev;
                Seg.BA = Curr - Prev;
                Seg.LengthSq = Seg.BA.SizeSquared();

                if (Seg.LengthSq > SMALL_NUMBER)
                {
                    CachedSegments.Add(Seg);
                    TotalSplineLength += FMath::Sqrt((double)Seg.LengthSq);
                    MinBounds = MinBounds.ComponentMin(Curr);
                    MaxBounds = MaxBounds.ComponentMax(Curr);
                }

                Prev = Curr;
            }
        }

        if (CachedSegments.Num() > 0)
        {
            const double MinUsefulLen = FMath::Max((double)TunnelRadius * 2.0, (double)VoxelSize * 4.0);
            if (TotalSplineLength < MinUsefulLen)
            {
                if (bLogPresetDebug)
                {
                    UE_LOG(LogIOC, Warning, TEXT("GenerateCave: Spline too short (%.1f < %.1f). Falling back to line tunnel."), TotalSplineLength, MinUsefulLen);
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

        if (bUseFixedBoundsForTunnel)
        {
            Bounds.X = FMath::Max(Bounds.X, VoxelSize * 2.0f);
            Bounds.Y = FMath::Max(Bounds.Y, VoxelSize * 2.0f);
            Bounds.Z = FMath::Max(Bounds.Z, VoxelSize * 2.0f);
            GridOrigin = Bounds * -0.5f;
        }
        else
        {
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
    }

    if (bLogPresetDebug)
    {
        UE_LOG(LogIOC, Warning, TEXT("GenerateCave: Tunnel=%d SplineMode=%d Start=%s End=%s Bounds=%s Voxel=%.1f NFreq=%.6f Radius=%.1f Wall=%.1f"),
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
    const FTransform ActorTransform = GetActorTransform();
    float DWarp = DomainWarpIntensity;
    float TSteps = TerraceSteps;
    int32 Seed = CaveSeed;
    bool bSmartColors = bGenerateSmartColors;
    bool bLOD = bEnableLOD;
    float LODMult = LODVoxelSizeMultiplier;
    bool bWorldSpaceNoise = bUseWorldSpaceNoise;
    bool bNavMesh = bAutoRebuildNavMesh;
    const int32 VoxelBudget = FMath::Clamp(MaxVoxelCount, 1000, 15000000);
    const int32 TriangleBudget = FMath::Max(1000, MaxGeneratedTriangles);
    const int32 ScatterBudget = FMath::Max(0, MaxScatterInstances);
    const double SafePrimaryVoxel = FMath::Max(10.0, VSize);
    const FVector ActorWorldLocation = ActorTransform.GetLocation();
    const FIntVector ScatterGridCoord(
        FMath::RoundToInt(ActorWorldLocation.X / SafePrimaryVoxel),
        FMath::RoundToInt(ActorWorldLocation.Y / SafePrimaryVoxel),
        FMath::RoundToInt(ActorWorldLocation.Z / SafePrimaryVoxel));
    const int32 EffectiveScatterSeed = bWorldSpaceNoise
        ? (Seed ^ (int32)GetTypeHash(ScatterGridCoord))
        : Seed;
    const int32 EstimatedSizeX = FMath::Clamp((int32)(Bounds.X / SafePrimaryVoxel), 2, MaxGridAxis);
    const int32 EstimatedSizeY = FMath::Clamp((int32)(Bounds.Y / SafePrimaryVoxel), 2, MaxGridAxis);
    const int32 EstimatedSizeZ = FMath::Clamp((int32)(Bounds.Z / SafePrimaryVoxel), 2, MaxGridAxis);
    const int32 EstimatedHalo = (bWorldSpaceNoise && !bTunnel) ? 1 : 0;
    const int64 EstimatedVoxelCount =
        (int64)(EstimatedSizeX + EstimatedHalo * 2) *
        (int64)(EstimatedSizeY + EstimatedHalo * 2) *
        (int64)(EstimatedSizeZ + EstimatedHalo * 2);

    // Conservative upper bound on how far a tunnel segment can still influence a voxel:
    // the largest organic radius any biome can request, plus its wall, plus the voxel pad at
    // the coarsest voxel size in play (the LOD pass multiplies it). Used to size the segment
    // acceleration grid, so it must never under-estimate.
    float MaxInfluenceRadius = TunnelRadius;
    float MaxInfluenceWall = WallThickness;
    for (const FIOCBiomeData& Biome : LocalBiomeData)
    {
        if (Biome.bOverride_TunnelRadius)
        {
            MaxInfluenceRadius = FMath::Max(MaxInfluenceRadius, Biome.TunnelRadius);
        }
        if (Biome.bOverride_WallThickness)
        {
            MaxInfluenceWall = FMath::Max(MaxInfluenceWall, Biome.WallThickness);
        }
    }
    const double CoarsestVoxel = FMath::Max(10.0, VoxelSize)
        * (double)FMath::Max(1.0f, bEnableLOD ? LODVoxelSizeMultiplier : 1.0f);
    const double SegmentInfluenceRadius =
        (double)MaxInfluenceRadius * 1.5 + (double)MaxInfluenceWall + CoarsestVoxel * 8.0;

    // Signature over every input to the voxel fill. Anything that changes the density field
    // must be folded in here; a missing value would let the cache serve a field that no longer
    // matches the parameters, which is the one way this design can be wrong. See FIOCVoxelCache.
    FIOCRecipeHasher Hasher;
    Hasher.Add(Seed).Add(NFreq).Add(NThresh)
          .Add(Octaves).Add(Lacunarity).Add(Persistence)
          .Add(ChamberWeight).Add(RidgedWeight).Add(DensityBias)
          .Add(DWarp).Add(TSteps)
          .Add(bTunnel).Add(bSplineMode)
          .Add(LocalTStart).Add(LocalTEnd).Add(TRadius).Add(TWall)
          .Add(bWorldSpaceNoise).Add(ActorTransform)
          .Add(Bounds).Add(GridOrigin);

    for (const FIOCBiomeData& Biome : LocalBiomeData)
    {
        Hasher.Add(Biome.Transform).Add(Biome.LocalCenter).Add(Biome.LocalExtents).Add(Biome.Priority)
              .Add(Biome.bOverride_NoiseFrequency).Add(Biome.NoiseFrequency)
              .Add(Biome.bOverride_TunnelRadius).Add(Biome.TunnelRadius)
              .Add(Biome.bOverride_WallThickness).Add(Biome.WallThickness)
              .Add(Biome.bOverride_TerraceSteps).Add(Biome.TerraceSteps);
    }

    Hasher.Add(CachedSegments.Num());
    for (const FIOCSegment& Segment : CachedSegments)
    {
        Hasher.Add(Segment.A).Add(Segment.BA).Add(Segment.LengthSq);
    }

    const uint32 RecipeSignature = Hasher.Hash;

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
    RuntimeCarves.AppendCapturesTo(LocalCarvingCaptures);

    // Only caves that actually carry carves pay for a cache. The first carve on a cave still
    // costs a full fill because nothing is cached yet; every carve after it replays the field.
    const bool bCacheVoxelField = Settings.bCacheVoxelFieldForCarving && !LocalCarvingCaptures.IsEmpty();
    if (bCacheVoxelField && !VoxelCache.IsValid())
    {
        VoxelCache = MakeShared<FIOCVoxelCache, ESPMode::ThreadSafe>();
    }
    TSharedPtr<FIOCVoxelCache, ESPMode::ThreadSafe> VoxelCacheRef = bCacheVoxelField ? VoxelCache : nullptr;

    Async(EAsyncExecution::ThreadPool, [=, LocalDecorationLayers = MoveTemp(LocalDecorationLayers), Segments = MoveTemp(CachedSegments), LocalCarvingCaptures = MoveTemp(LocalCarvingCaptures), LocalBiomeData = MoveTemp(LocalBiomeData)]() mutable
    {
        auto FinishCancelled = [WeakThis, GenerationState]()
        {
            AsyncTask(ENamedThreads::GameThread, [WeakThis, GenerationState]()
            {
                if (!WeakThis.IsValid())
                {
                    return;
                }

                AIOCProceduralActor* Actor = WeakThis.Get();
                if (Actor->ActiveGeneration != GenerationState)
                {
                    return;
                }

                const bool bShouldRegenerate = Actor->bPendingRegeneration;
                Actor->ActiveGeneration.Reset();
                Actor->bIsGenerating = false;
                Actor->bIsGeneratingDisplay = false;
                Actor->bPendingRegeneration = false;
                Actor->BroadcastGenerationFinished(true, bShouldRegenerate);

                if (bShouldRegenerate && !Actor->IsActorBeingDestroyed())
                {
                    Actor->GenerateCave();
                }
            });
        };

        // Accumulated across both the primary and LOD BuildMesh calls.
        FIOCStageTimings StageTimings;
        double StageStart = 0.0;
        bool bAnyFieldReplayed = false;

        // Built once and shared by the primary and LOD passes: it depends only on the
        // segments and the influence radius, not on the voxel size.
        FIOCSegmentGrid SegmentGrid;
        if (bSplineMode)
        {
            SegmentGrid.Build(Segments, SegmentInfluenceRadius);
        }

        // ---------------------------------------------------------------
        // Helper: build a cave mesh at the given voxel resolution.
        // Runs synchronously inside this async task.
        // ---------------------------------------------------------------
        auto BuildMesh = [&](double VoxSize, bool bColors, float WallThickOverride) -> FDynamicMesh3
        {
            double SafeVoxel = FMath::Max(10.0, VoxSize);
            const int32 RequestedX = (int32)(Bounds.X / SafeVoxel);
            const int32 RequestedY = (int32)(Bounds.Y / SafeVoxel);
            const int32 RequestedZ = (int32)(Bounds.Z / SafeVoxel);
            int32 SizeX = FMath::Clamp(RequestedX, 2, MaxGridAxis);
            int32 SizeY = FMath::Clamp(RequestedY, 2, MaxGridAxis);
            int32 SizeZ = FMath::Clamp(RequestedZ, 2, MaxGridAxis);

            // Clamping here shrinks the generated volume below the requested bounds. Every
            // other budget overrun reports itself; this one used to be silent, so a cave asked
            // to span 500 m quietly came back 200 m across.
            if (RequestedX > MaxGridAxis || RequestedY > MaxGridAxis || RequestedZ > MaxGridAxis)
            {
                UE_LOG(LogIOC, Warning,
                    TEXT("Generation bounds need %d x %d x %d cells at voxel size %.1f, above the %d cell per-axis limit ")
                    TEXT("(Project Settings > Plugins > Instant Organic Caves). The generated cave will be SMALLER than ")
                    TEXT("GenerationBounds. Increase VoxelSize, reduce GenerationBounds, or raise the limit."),
                    RequestedX, RequestedY, RequestedZ, (float)SafeVoxel, MaxGridAxis);
            }
            // World-space density chunks sample one cell beyond every boundary.
            // The halo lets face extraction consult the same neighbor value that
            // an adjacent voxel-aligned chunk will use, avoiding artificial caps.
            const int32 GridHalo = (bWorldSpaceNoise && !bTunnel) ? 1 : 0;
            const int32 GridSizeX = SizeX + GridHalo * 2;
            const int32 GridSizeY = SizeY + GridHalo * 2;
            const int32 GridSizeZ = SizeZ + GridHalo * 2;
            const int64 TotalVoxels = (int64)GridSizeX * GridSizeY * GridSizeZ;
            if (TotalVoxels > VoxelBudget)
            {
                UE_LOG(LogIOC, Warning,
                    TEXT("Voxel grid budget exceeded (%dx%dx%d including halo = %lld voxels, max %lld). Increase VoxelSize or reduce GenerationBounds."),
                    GridSizeX, GridSizeY, GridSizeZ, TotalVoxels, (int64)VoxelBudget);
                return FDynamicMesh3{};
            }

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
            Voxels.Init(false, TotalVoxels);
            auto GetIdx = [&](int32 x, int32 y, int32 z)
            {
                return (int64)x + (int64)y * GridSizeX + (int64)z * GridSizeX * GridSizeY;
            };

            FVector SeedOffset = FVector((float)Seed * 132.5f, (float)Seed * 37.2f, (float)Seed * 91.8f);
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
                // Only segments binned into this point's cell can be close enough to matter;
                // the grid is padded by the maximum influence radius, so anything not listed
                // here is provably outside the shell.
                const TArray<int32>* Candidates = SegmentGrid.GetCandidates(P);
                if (!Candidates || Candidates->IsEmpty())
                {
                    OutTRaw = 0.0f;
                    bOutOfRange = true;
                    return MAX_flt;
                }

                float MinDistSq = MAX_flt;
                float BestTRaw = 0.0f;
                bool bFoundInRange = false;
                bool bBestOutOfRange = true;
                for (const int32 SegIndex : *Candidates)
                {
                    const FIOCSegment& Seg = InSegments[SegIndex];
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
            if (TotalVoxels > 10000000) // 10M voxel warning threshold
            {
                UE_LOG(LogIOC, Warning, TEXT("Large voxel grid (%dx%dx%d = %lld voxels). Consider increasing VoxelSize or reducing GenerationBounds."),
                    GridSizeX, GridSizeY, GridSizeZ, TotalVoxels);
            }

            // ---- Voxel fill ----
            // On a cache hit the field is replayed and the fill is skipped outright: passing 0
            // to ParallelFor makes it a no-op without having to re-indent the body.
            const FIntVector GridDims(GridSizeX, GridSizeY, GridSizeZ);
            const bool bUsedCachedField = VoxelCacheRef.IsValid() &&
                VoxelCacheRef->TryRead(RecipeSignature, SafeVoxel, GridDims, Voxels);
            bAnyFieldReplayed = bAnyFieldReplayed || bUsedCachedField;

            StageStart = FPlatformTime::Seconds();
            ParallelFor(bUsedCachedField ? (int64)0 : TotalVoxels, [&](int64 Index)
            {
                if (GenerationState->bCancelRequested.load(std::memory_order_relaxed))
                {
                    return;
                }

                const int32 z = (int32)(Index / ((int64)GridSizeX * GridSizeY));
                const int32 rem = (int32)(Index % ((int64)GridSizeX * GridSizeY));
                const int32 y = rem / GridSizeX;
                const int32 x = rem % GridSizeX;
                const int32 LocalX = x - GridHalo;
                const int32 LocalY = y - GridHalo;
                const int32 LocalZ = z - GridHalo;

                FVector P = GridOrigin + FVector(LocalX, LocalY, LocalZ) * SafeVoxel;
                FVector LocalP = P - FillCenteringOffset;
                FVector WorldP = ActorTransform.TransformPosition(LocalP);
                FVector SampleP = bWorldSpaceNoise ? (WorldP + SeedOffset) : (LocalP + SeedOffset);

                // --- Biome Overrides ---
                float CurrentNFreq = NFreq;
                float CurrentTRadius = TRadius;
                float CurrentTWall = EffectiveWall;
                float CurrentTSteps = TSteps;

                for (const FIOCBiomeData& Biome : LocalBiomeData)
                {
                    // Point-in-OBB: transform world point into biome's local space
                    const FVector BiomeLocalP = Biome.Transform.InverseTransformPosition(WorldP);
                    const FVector Delta = BiomeLocalP - Biome.LocalCenter;
                    const FVector HalfExt = Biome.LocalExtents;
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
                    float Wx = IOC_GradientNoise3D(SampleP * CurrentNFreq + FVector(5.2, 1.3, 0));
                    float Wy = IOC_GradientNoise3D(SampleP * CurrentNFreq + FVector(1.7, 9.2, 0));
                    float Wz = IOC_GradientNoise3D(SampleP * CurrentNFreq + FVector(8.3, 2.8, 0));
                    SampleP += FVector(Wx, Wy, Wz) * DWarp;
                }

                if (CurrentTSteps > 1.0f)
                {
                    float RelZ = SampleP.Z;
                    SampleP.Z = FMath::RoundToFloat(RelZ / CurrentTSteps) * CurrentTSteps;
                }

                float Noise = IOC_GradientNoise3D(SampleP * CurrentNFreq);

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

            StageTimings.Fill += FPlatformTime::Seconds() - StageStart;

            // Store the pristine field before anything carves into it.
            if (!bUsedCachedField && VoxelCacheRef.IsValid() &&
                !GenerationState->bCancelRequested.load())
            {
                VoxelCacheRef->Write(RecipeSignature, SafeVoxel, GridDims, Voxels);
            }

            // ---- Cancellation checkpoint ----
            if (GenerationState->bCancelRequested.load()) return FDynamicMesh3{};

            // ---- Carving pass (parallelized over the affected region only) ----
            StageStart = FPlatformTime::Seconds();
            if (!LocalCarvingCaptures.IsEmpty())
            {
                // Restrict the pass to voxels a carve can actually reach. Outside that box the
                // field is identical to the filled/replayed result, so testing it is pure waste
                // -- and with a replayed field this box is the only voxel work a carve does.
                const FTransform ActorInverse = ActorTransform.Inverse();
                FBox CarveLocalBounds(ForceInit);

                for (const FIOCCarvingCapture& Cap : LocalCarvingCaptures)
                {
                    const float Falloff = FMath::Max(0.0f, Cap.FalloffRadius);
                    FVector ShapeExtent;
                    switch (Cap.ShapeType)
                    {
                    case EIOCCarvingShape::Box:
                        ShapeExtent = Cap.BoxExtent + FVector(Falloff);
                        break;
                    case EIOCCarvingShape::Capsule:
                        ShapeExtent = FVector(
                            Cap.CapsuleRadius + Falloff,
                            Cap.CapsuleRadius + Falloff,
                            Cap.CapsuleHalfHeight + Cap.CapsuleRadius + Falloff);
                        break;
                    default:
                        ShapeExtent = FVector(Cap.SphereRadius + Falloff);
                        break;
                    }

                    // Shape space -> world -> actor local, taking the AABB at each step. Both
                    // steps are conservative, which is what we want: over-covering costs a few
                    // wasted voxel tests, under-covering would leave rock behind.
                    const FBox ShapeLocal(-ShapeExtent, ShapeExtent);
                    CarveLocalBounds += ShapeLocal.TransformBy(Cap.WorldTransform).TransformBy(ActorInverse);
                }

                // LocalP = GridOrigin + (GridIndex - Halo) * SafeVoxel - FillCenteringOffset
                //   =>  GridIndex = (LocalP - GridOrigin + FillCenteringOffset) / SafeVoxel + Halo
                const FVector IndexMinF = (CarveLocalBounds.Min - GridOrigin + FillCenteringOffset) / SafeVoxel;
                const FVector IndexMaxF = (CarveLocalBounds.Max - GridOrigin + FillCenteringOffset) / SafeVoxel;

                const FIntVector BoxMin(
                    FMath::Clamp(FMath::FloorToInt(IndexMinF.X) - 1 + GridHalo, 0, GridSizeX - 1),
                    FMath::Clamp(FMath::FloorToInt(IndexMinF.Y) - 1 + GridHalo, 0, GridSizeY - 1),
                    FMath::Clamp(FMath::FloorToInt(IndexMinF.Z) - 1 + GridHalo, 0, GridSizeZ - 1));
                const FIntVector BoxMax(
                    FMath::Clamp(FMath::CeilToInt(IndexMaxF.X) + 1 + GridHalo, 0, GridSizeX - 1),
                    FMath::Clamp(FMath::CeilToInt(IndexMaxF.Y) + 1 + GridHalo, 0, GridSizeY - 1),
                    FMath::Clamp(FMath::CeilToInt(IndexMaxF.Z) + 1 + GridHalo, 0, GridSizeZ - 1));

                const FIntVector BoxDim(
                    FMath::Max(0, BoxMax.X - BoxMin.X + 1),
                    FMath::Max(0, BoxMax.Y - BoxMin.Y + 1),
                    FMath::Max(0, BoxMax.Z - BoxMin.Z + 1));
                const int64 BoxVoxels = (int64)BoxDim.X * (int64)BoxDim.Y * (int64)BoxDim.Z;

                ParallelFor(BoxVoxels, [&](int64 BoxIndex)
                {
                    if (GenerationState->bCancelRequested.load(std::memory_order_relaxed)) return;

                    const int32 bz = (int32)(BoxIndex / ((int64)BoxDim.X * BoxDim.Y));
                    const int32 brem = (int32)(BoxIndex % ((int64)BoxDim.X * BoxDim.Y));
                    const int32 by = brem / BoxDim.X;
                    const int32 bx = brem % BoxDim.X;

                    const int32 x = BoxMin.X + bx;
                    const int32 y = BoxMin.Y + by;
                    const int32 z = BoxMin.Z + bz;

                    const int64 Index = GetIdx(x, y, z);
                    if (!Voxels[Index]) return; // already empty

                    const int32 LocalX = x - GridHalo;
                    const int32 LocalY = y - GridHalo;
                    const int32 LocalZ = z - GridHalo;

                    FVector P = GridOrigin + FVector(LocalX, LocalY, LocalZ) * SafeVoxel;
                    FVector LocalP = P - FillCenteringOffset;
                    FVector WorldP = ActorTransform.TransformPosition(LocalP);

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
                        const FVector StableHashPosition = bWorldSpaceNoise
                            ? (WorldP / (float)SafeVoxel)
                            : FVector(x, y, z);
                        float HashVal  = IOC_GradientNoise3D(StableHashPosition * 0.7f + FVector((float)Seed));
                        float NormHash = (HashVal + 1.f) * 0.5f;
                        if (NormHash < Blend)
                        {
                            Voxels[Index] = false;
                        }
                    }
                });
            }

            StageTimings.Carve += FPlatformTime::Seconds() - StageStart;

            // ---- Cancellation checkpoint ----
            if (GenerationState->bCancelRequested.load()) return FDynamicMesh3{};

            // ---- Hole fill pass (tunnel shells only) ----
            StageStart = FPlatformTime::Seconds();
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

            StageTimings.HoleFill += FPlatformTime::Seconds() - StageStart;

            // ---- Cancellation checkpoint ----
            if (GenerationState->bCancelRequested.load()) return FDynamicMesh3{};

            // ---- Phase 2: collect quads with shared positions ----
            StageStart = FPlatformTime::Seconds();
            TArray<FVector3d> SharedPos;
            TArray<bool> BoundaryPos;
            TArray<FIOCQuad> Quads;

            // Corner lookup happens up to 24 times per surface voxel, so hashing an FIntVector
            // each time is measurable. Use a direct-indexed table over the corner lattice when
            // it fits in a sane amount of memory, and fall back to the map when it does not.
            const int64 CornerLatticeCount = (int64)(SizeX + 1) * (int64)(SizeY + 1) * (int64)(SizeZ + 1);
            const bool bUseDenseCornerTable = CornerLatticeCount <= 4000000;
            TArray<int32> DenseCornerIds;
            TMap<FIntVector, int32> PosCache;
            if (bUseDenseCornerTable)
            {
                DenseCornerIds.Init(INDEX_NONE, (int32)CornerLatticeCount);
            }

            FVector3d CenteringOffset = bTunnel ? FVector3d::Zero() : FVector3d(Bounds.X, Bounds.Y, Bounds.Z) * 0.5;

            auto GetPosIdx = [&](int32 cx, int32 cy, int32 cz) -> int32 {
                const int32 DenseIndex = bUseDenseCornerTable
                    ? (cx + cy * (SizeX + 1) + cz * (SizeX + 1) * (SizeY + 1))
                    : INDEX_NONE;

                if (bUseDenseCornerTable)
                {
                    if (DenseCornerIds[DenseIndex] != INDEX_NONE) return DenseCornerIds[DenseIndex];
                }
                else
                {
                    if (int32* Found = PosCache.Find(FIntVector(cx, cy, cz))) return *Found;
                }

                int32 ID = SharedPos.Num();
                FVector3d Pos = FVector3d(GridOrigin) + (FVector3d(cx, cy, cz) * SafeVoxel) - CenteringOffset;
                SharedPos.Add(Pos);
                BoundaryPos.Add(
                    GridHalo > 0 &&
                    (cx == 0 || cx == SizeX || cy == 0 || cy == SizeY || cz == 0 || cz == SizeZ));

                if (bUseDenseCornerTable)
                {
                    DenseCornerIds[DenseIndex] = ID;
                }
                else
                {
                    PosCache.Add(FIntVector(cx, cy, cz), ID);
                }
                return ID;
            };

            const int32 MaxQuadCount = FMath::Max(1, TriangleBudget / 2);
            bool bTriangleBudgetExceeded = false;
            auto AddQuad = [&](int32 a, int32 b, int32 c, int32 d) {
                if (Quads.Num() >= MaxQuadCount)
                {
                    bTriangleBudgetExceeded = true;
                    return;
                }
                FIOCQuad Q; Q.V[0] = a; Q.V[1] = b; Q.V[2] = c; Q.V[3] = d;
                Quads.Add(Q);
            };

            for (int32 z = 0; z < SizeZ && !bTriangleBudgetExceeded; ++z)
            {
                if (GenerationState->bCancelRequested.load())
                {
                    return FDynamicMesh3{};
                }

                for (int32 y = 0; y < SizeY && !bTriangleBudgetExceeded; ++y)
                {
                    for (int32 x = 0; x < SizeX && !bTriangleBudgetExceeded; ++x)
                    {
                        const int32 GridX = x + GridHalo;
                        const int32 GridY = y + GridHalo;
                        const int32 GridZ = z + GridHalo;
                        if (!Voxels[GetIdx(GridX, GridY, GridZ)]) continue;

                        auto IsAir = [&](int32 nx, int32 ny, int32 nz) {
                            const int32 NeighborGridX = nx + GridHalo;
                            const int32 NeighborGridY = ny + GridHalo;
                            const int32 NeighborGridZ = nz + GridHalo;
                            if (NeighborGridX < 0 || NeighborGridX >= GridSizeX ||
                                NeighborGridY < 0 || NeighborGridY >= GridSizeY ||
                                NeighborGridZ < 0 || NeighborGridZ >= GridSizeZ)
                            {
                                return true;
                            }
                            return !Voxels[GetIdx(NeighborGridX, NeighborGridY, NeighborGridZ)];
                        };

                        if (IsAir(x-1,y,z)) AddQuad(GetPosIdx(x,y,z),     GetPosIdx(x,y,z+1),     GetPosIdx(x,y+1,z+1),   GetPosIdx(x,y+1,z));
                        if (IsAir(x+1,y,z)) AddQuad(GetPosIdx(x+1,y+1,z), GetPosIdx(x+1,y+1,z+1), GetPosIdx(x+1,y,z+1),   GetPosIdx(x+1,y,z));
                        if (IsAir(x,y-1,z)) AddQuad(GetPosIdx(x+1,y,z),   GetPosIdx(x+1,y,z+1),   GetPosIdx(x,y,z+1),     GetPosIdx(x,y,z));
                        if (IsAir(x,y+1,z)) AddQuad(GetPosIdx(x,y+1,z),   GetPosIdx(x,y+1,z+1),   GetPosIdx(x+1,y+1,z+1), GetPosIdx(x+1,y+1,z));
                        if (IsAir(x,y,z-1)) AddQuad(GetPosIdx(x,y+1,z),   GetPosIdx(x+1,y+1,z),   GetPosIdx(x+1,y,z),     GetPosIdx(x,y,z));
                        if (IsAir(x,y,z+1)) AddQuad(GetPosIdx(x,y,z+1),   GetPosIdx(x+1,y,z+1),   GetPosIdx(x+1,y+1,z+1), GetPosIdx(x,y+1,z+1));
                    }
                }
            }

            StageTimings.Extract += FPlatformTime::Seconds() - StageStart;

            if (bTriangleBudgetExceeded)
            {
                UE_LOG(LogIOC, Warning,
                    TEXT("Triangle budget exceeded (max %d triangles). Increase VoxelSize, reduce bounds, or raise MaxGeneratedTriangles."),
                    TriangleBudget);
                return FDynamicMesh3{};
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

            ensureMsgf(DegenerateCount == 0, TEXT("%d degenerate quads detected. Check AddQuad winding/indices."), DegenerateCount);
#endif

            // ---- Phase 3: Laplacian smoothing on shared positions ----
            StageStart = FPlatformTime::Seconds();
            if (Smooth > 0)
            {
                const int32 NumShared = SharedPos.Num();

                // Adjacency is stored compressed-sparse-row rather than as one TSet per
                // vertex. The TSet version allocated a hash set for every vertex in the mesh,
                // and on a mid-size cave that allocator traffic -- not the arithmetic -- was
                // the single largest cost in the whole generator.
                //
                // Sorting each (short) neighbour slice and collapsing duplicates reproduces
                // TSet's unique-neighbour semantics exactly, so each vertex averages the same
                // set of neighbours as before. The summation order differs, and float addition
                // is not associative, so positions can differ in the last bits -- equivalent,
                // not bit-identical.
                TArray<int32> Offsets;
                Offsets.Init(0, NumShared + 1);

                // Pass 1: degree. Each quad corner contributes its successor to itself and
                // itself to its successor, matching the original insertion pattern.
                for (const FIOCQuad& Q : Quads)
                {
                    for (int32 i = 0; i < 4; ++i)
                    {
                        const int32 Next = (i + 1) & 3;
                        ++Offsets[Q.V[i]];
                        ++Offsets[Q.V[Next]];
                    }
                }

                // Prefix sum, shifting degrees into start offsets.
                int32 Running = 0;
                for (int32 i = 0; i < NumShared; ++i)
                {
                    const int32 Degree = Offsets[i];
                    Offsets[i] = Running;
                    Running += Degree;
                }
                Offsets[NumShared] = Running;

                TArray<int32> Neighbors;
                Neighbors.SetNumUninitialized(Running);

                TArray<int32> WriteCursor = Offsets;
                for (const FIOCQuad& Q : Quads)
                {
                    for (int32 i = 0; i < 4; ++i)
                    {
                        const int32 Next = (i + 1) & 3;
                        Neighbors[WriteCursor[Q.V[i]]++] = Q.V[Next];
                        Neighbors[WriteCursor[Q.V[Next]]++] = Q.V[i];
                    }
                }

                // Pass 2: dedupe each slice in place. Slices are independent, so this
                // parallelises cleanly.
                TArray<int32> UniqueCount;
                UniqueCount.SetNumUninitialized(NumShared);
                ParallelFor(NumShared, [&](int32 Vertex)
                {
                    const int32 Begin = Offsets[Vertex];
                    const int32 End = Offsets[Vertex + 1];
                    if (End <= Begin)
                    {
                        UniqueCount[Vertex] = 0;
                        return;
                    }

                    Algo::Sort(MakeArrayView(Neighbors.GetData() + Begin, End - Begin));

                    int32 Write = Begin;
                    for (int32 Read = Begin; Read < End; ++Read)
                    {
                        if (Read == Begin || Neighbors[Read] != Neighbors[Write - 1])
                        {
                            Neighbors[Write++] = Neighbors[Read];
                        }
                    }
                    UniqueCount[Vertex] = Write - Begin;
                });

                // Each vertex reads only OldPos (fixed for the whole pass) and writes only its
                // own SharedPos entry, so the update has no cross-vertex dependency.
                TArray<FVector3d> OldPos;
                for (int32 Iteration = 0; Iteration < Smooth; ++Iteration)
                {
                    OldPos = SharedPos;
                    ParallelFor(NumShared, [&](int32 Vertex)
                    {
                        if (BoundaryPos.IsValidIndex(Vertex) && BoundaryPos[Vertex]) return;

                        const int32 Count = UniqueCount[Vertex];
                        if (Count == 0) return;

                        const int32 Begin = Offsets[Vertex];
                        FVector3d Sum = FVector3d::Zero();
                        for (int32 n = 0; n < Count; ++n)
                        {
                            Sum += OldPos[Neighbors[Begin + n]];
                        }
                        SharedPos[Vertex] = FMath::Lerp(OldPos[Vertex], Sum / (double)Count, 0.5);
                    });
                }
            }

            StageTimings.Smooth += FPlatformTime::Seconds() - StageStart;

            // ---- Phase 4: build FDynamicMesh3 with per-quad vertices ----
            StageStart = FPlatformTime::Seconds();
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

            // Second pass: build the FDynamicMesh3.
            //
            // Emitting four unshared vertices per quad was the safe option, because
            // FDynamicMesh3 refuses any triangle that would create a non-manifold edge and
            // diagonal-touching voxels produce those readily. It also meant roughly 4x the
            // vertices, and the same multiplier on the collision data cooked from them.
            //
            // Welding shares a vertex per grid corner and falls back to duplicated vertices
            // for only the individual triangles that actually fail, so the manifold guarantee
            // is kept and no triangle is ever dropped. Normals, UVs and colours are unchanged:
            // they were already derived per shared position, not per mesh vertex.
            //
            // The shipped cave materials are two-sided, so no duplicate backfaces are emitted.
            FDynamicMesh3 LocalMesh;
            LocalMesh.EnableAttributes();

            TArray<int32> VertToShared;

            if (bWeldVertices)
            {
                TArray<int32> SharedToVert;
                SharedToVert.SetNumUninitialized(SharedPos.Num());
                VertToShared.Reserve(SharedPos.Num());

                for (int32 SharedIdx = 0; SharedIdx < SharedPos.Num(); ++SharedIdx)
                {
                    SharedToVert[SharedIdx] = LocalMesh.AppendVertex(SharedPos[SharedIdx]);
                    VertToShared.Add(SharedIdx);
                }

                // AppendVertex hands out sequential ids on a mesh with no deletions, so
                // VertToShared stays index-aligned with mesh vertex ids as long as every
                // AppendVertex is paired with exactly one Add here.
                auto AppendWeldedTriangle = [&](int32 SharedA, int32 SharedB, int32 SharedC)
                {
                    if (LocalMesh.AppendTriangle(
                            SharedToVert[SharedA], SharedToVert[SharedB], SharedToVert[SharedC]) >= 0)
                    {
                        return;
                    }

                    // NonManifoldID or DuplicateTriangleID: give this one triangle its own
                    // vertices rather than losing it.
                    const int32 A = LocalMesh.AppendVertex(SharedPos[SharedA]); VertToShared.Add(SharedA);
                    const int32 B = LocalMesh.AppendVertex(SharedPos[SharedB]); VertToShared.Add(SharedB);
                    const int32 C = LocalMesh.AppendVertex(SharedPos[SharedC]); VertToShared.Add(SharedC);
                    LocalMesh.AppendTriangle(A, B, C);
                };

                for (const FIOCQuad& Q : Quads)
                {
                    AppendWeldedTriangle(Q.V[0], Q.V[1], Q.V[2]);
                    AppendWeldedTriangle(Q.V[2], Q.V[3], Q.V[0]);
                }
            }
            else
            {
                VertToShared.Reserve(Quads.Num() * 4);
                for (const FIOCQuad& Q : Quads)
                {
                    int32 mv[4];
                    for (int32 j = 0; j < 4; ++j)
                    {
                        mv[j] = LocalMesh.AppendVertex(SharedPos[Q.V[j]]);
                        VertToShared.Add(Q.V[j]);
                    }
                    LocalMesh.AppendTriangle(mv[0], mv[1], mv[2]);
                    LocalMesh.AppendTriangle(mv[2], mv[3], mv[0]);
                }
            }

            StageTimings.MeshBuild += FPlatformTime::Seconds() - StageStart;

            // ---- Phase 5: normal & UV & Color overlays ----
            StageStart = FPlatformTime::Seconds();
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
                    TArray<int32> SharedToUV;
                    SharedToUV.Init(INDEX_NONE, SharedPos.Num());

                    for (int32 SharedIdx = 0; SharedIdx < SharedPos.Num(); ++SharedIdx)
                    {
                        FVector3d ProjectionPosition = SharedPos[SharedIdx];
                        FVector3d ProjectionNormal = NormalAccum[SharedIdx];
                        if (bWorldSpaceNoise)
                        {
                            ProjectionPosition = FVector3d(ActorTransform.TransformPosition((FVector)ProjectionPosition));
                            ProjectionNormal = FVector3d(
                                ActorTransform.TransformVectorNoScale((FVector)ProjectionNormal).GetSafeNormal());
                        }

                        SharedToUV[SharedIdx] = UVOvl->AppendElement(
                            IOC_ProjectStablePlanarUV(ProjectionPosition, ProjectionNormal, Tiling));
                    }

                    for (int32 tid : LocalMesh.TriangleIndicesItr())
                    {
                        FIndex3i Tri = LocalMesh.GetTriangle(tid);
                        FIndex3i UTri;
                        for (int32 j = 0; j < 3; ++j)
                        {
                            const int32 VertexId = Tri[j];
                            const int32 SharedIdx = (VertexId >= 0 && VertexId < VertToShared.Num())
                                ? VertToShared[VertexId]
                                : INDEX_NONE;

                            if (SharedIdx >= 0 && SharedIdx < SharedToUV.Num())
                            {
                                UTri[j] = SharedToUV[SharedIdx];
                            }
                            else
                            {
                                UTri[j] = UVOvl->AppendElement(FVector2f::ZeroVector);
                            }
                        }
                        UVOvl->SetTriangle(tid, UTri);
                    }
                }
            }

            StageTimings.Overlays += FPlatformTime::Seconds() - StageStart;

            return LocalMesh;
        }; // end BuildMesh

        // Build primary mesh
        FDynamicMesh3 ResultMesh = BuildMesh(VSize, bSmartColors, -1.0f);
        const int32 PrimaryTriangleCount = ResultMesh.TriangleCount();
        const bool bGenerationSucceeded = PrimaryTriangleCount > 0;
        const FString GenerationError = bGenerationSucceeded
            ? FString()
            : TEXT("Generation produced no surface. Check bounds, voxel/triangle budgets, threshold, and wall thickness.");

        // Check if cancelled (BuildMesh returns empty mesh on cancel)
        if (GenerationState->bCancelRequested.load())
        {
            FinishCancelled();
            return;
        }

        // Build LOD mesh (coarser, preserve smart-color data so material swaps remain consistent)
        FDynamicMesh3 LODResultMesh;
        if (bLOD && bGenerationSucceeded)
        {
            LODResultMesh = BuildMesh(VSize * (double)LODMult, bSmartColors, -1.0f);
            if (GenerationState->bCancelRequested.load())
            {
                FinishCancelled();
                return;
            }
        }
        const int32 LODTriangleCount = LODResultMesh.TriangleCount();

        // ---- Phase 6: Scattering Calculation (using primary mesh) ----
        StageStart = FPlatformTime::Seconds();
        TArray<TArray<FTransform>> ScatterResults;
        ScatterResults.SetNum(LocalDecorationLayers.Num());
        int32 RemainingScatterBudget = ScatterBudget;

        if (RemainingScatterBudget > 0 && LocalDecorationLayers.Num() > 0 && ResultMesh.TriangleCount() > 0)
        {
            FRandomStream Rnd(EffectiveScatterSeed);

            for (int32 LayerIdx = 0; LayerIdx < LocalDecorationLayers.Num() && RemainingScatterBudget > 0; ++LayerIdx)
            {
                const FIOCScatterLayer& Layer = LocalDecorationLayers[LayerIdx];
                if (!Layer.Mesh || Layer.Density <= SMALL_NUMBER) continue;

                TArray<FTransform>& OutTransforms = ScatterResults[LayerIdx];
                double Density = (double)Layer.Density / 10000.0;
                double PoissonMult = (Layer.PoissonMinSeparation > SMALL_NUMBER) ? 30.0 : 1.0;
                const float MinSlopeZ = FMath::Min(Layer.MinSlopeZ, Layer.MaxSlopeZ);
                const float MaxSlopeZ = FMath::Max(Layer.MinSlopeZ, Layer.MaxSlopeZ);
                const int32 CandidateBudget = Layer.PoissonMinSeparation > SMALL_NUMBER
                    ? (int32)FMath::Min<int64>((int64)RemainingScatterBudget * 4, (int64)MAX_int32)
                    : RemainingScatterBudget;

                double Accumulator = 0.0;

                for (int32 tid : ResultMesh.TriangleIndicesItr())
                {
                    if (OutTransforms.Num() >= CandidateBudget || GenerationState->bCancelRequested.load())
                    {
                        break;
                    }

                    FVector3d A, B, C;
                    ResultMesh.GetTriVertices(tid, A, B, C);
                    FVector3d FaceN = ResultMesh.GetTriNormal(tid);
                    double Area = VectorUtil::Area(A, B, C);

                    if (FaceN.Z < MinSlopeZ || FaceN.Z > MaxSlopeZ) continue;

                    double CountFloat = Area * Density * PoissonMult;
                    Accumulator += CountFloat;
                    int32 Count = (int32)Accumulator;
                    Accumulator -= Count;
                    Count = FMath::Min(Count, CandidateBudget - OutTransforms.Num());

                    for (int32 c = 0; c < Count; ++c)
                    {
                        double u = Rnd.FRand();
                        double v = Rnd.FRand();
                        if (u + v > 1.0) { u = 1.0 - u; v = 1.0 - v; }
                        double w = 1.0 - u - v;

                        FVector3d Pos = u*A + v*B + w*C;

                        FTransform T;
                        T.SetLocation((FVector)Pos);
                        T.SetRotation(IOC_BuildScatterRotation(Layer, FaceN, Rnd));

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
                    Accepted.Reserve(FMath::Min(OutTransforms.Num(), RemainingScatterBudget));

                    for (const FTransform& T : OutTransforms)
                    {
                        if (Accepted.Num() >= RemainingScatterBudget)
                        {
                            break;
                        }

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

                if (OutTransforms.Num() > RemainingScatterBudget)
                {
                    OutTransforms.SetNum(RemainingScatterBudget, EAllowShrinking::Yes);
                }
                RemainingScatterBudget -= OutTransforms.Num();
            }
        }

        if (GenerationState->bCancelRequested.load())
        {
            FinishCancelled();
            return;
        }

        StageTimings.Scatter += FPlatformTime::Seconds() - StageStart;

        if (bLogStageTimings)
        {
            UE_LOG(LogIOC, Log,
                TEXT("Stage ms -- fill %.1f | carve %.1f | holefill %.1f | extract %.1f | smooth %.1f | mesh %.1f | overlays %.1f | scatter %.1f | worker total %.1f%s"),
                StageTimings.Fill * 1000.0,
                StageTimings.Carve * 1000.0,
                StageTimings.HoleFill * 1000.0,
                StageTimings.Extract * 1000.0,
                StageTimings.Smooth * 1000.0,
                StageTimings.MeshBuild * 1000.0,
                StageTimings.Overlays * 1000.0,
                StageTimings.Scatter * 1000.0,
                (StageTimings.SumBuild() + StageTimings.Scatter) * 1000.0,
                bAnyFieldReplayed ? TEXT(" [field replayed]") : TEXT(""));
        }

        int32 ScatterInstanceCount = 0;
        for (const TArray<FTransform>& LayerTransforms : ScatterResults)
        {
            ScatterInstanceCount += LayerTransforms.Num();
        }

        AsyncTask(ENamedThreads::GameThread, [WeakThis, GenerationState, ResultMesh = MoveTemp(ResultMesh), LODResultMesh = MoveTemp(LODResultMesh), ScatterResults = MoveTemp(ScatterResults), LocalDecorationLayers, bNavMesh, bLOD, bGenerationSucceeded, GenerationError, GenerationStartSeconds, EstimatedVoxelCount, PrimaryTriangleCount, LODTriangleCount, ScatterInstanceCount]() mutable {
            if (!WeakThis.IsValid()) return;
            AIOCProceduralActor* Actor = WeakThis.Get();
            if (Actor->ActiveGeneration != GenerationState)
            {
                return;
            }

            if (GenerationState->bCancelRequested.load())
            {
                const bool bShouldRegenerate = Actor->bPendingRegeneration;
                Actor->ActiveGeneration.Reset();
                Actor->bIsGenerating = false;
                Actor->bIsGeneratingDisplay = false;
                Actor->bPendingRegeneration = false;
                Actor->BroadcastGenerationFinished(true, bShouldRegenerate);

                if (bShouldRegenerate && !Actor->IsActorBeingDestroyed())
                {
                    Actor->GenerateCave();
                }
                return;
            }

            // 1. Update primary mesh
            if (bGenerationSucceeded && Actor->MeshComponent)
            {
                Actor->MeshComponent->GetDynamicMesh()->SetMesh(MoveTemp(ResultMesh));

                // Collision updates are deferred (see the constructor), so cook exactly once
                // here rather than on every intermediate edit the generation performs.
                Actor->MeshComponent->UpdateCollision(true);
            }

            // 2. Update LOD mesh
            if (bGenerationSucceeded && bLOD && Actor->LODMeshComponent)
            {
                Actor->LODMeshComponent->GetDynamicMesh()->SetMesh(MoveTemp(LODResultMesh));
            }

            Actor->bLODActive = false;
            Actor->bHasValidLOD = bGenerationSucceeded && bLOD && LODTriangleCount > 0;
            Actor->SyncLODVisibility();

            Actor->ApplyCaveMaterials();

            // 3. Update Decoration HISMs
            if (bGenerationSucceeded)
            {
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
                        // AActor::OwnedComponents is not a UPROPERTY, so a component that is only
                        // registered is never serialised with the level: the cave mesh survived a
                        // save/reload but every scatter instance vanished until the next Generate.
                        // InstanceComponents is the serialised list. DestroyComponent() removes it
                        // again, so the rebuild path above stays correct.
                        Actor->AddInstanceComponent(HISM);
                        HISM->SetStaticMesh(Layer.Mesh);
                        HISM->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
                        HISM->SetupAttachment(Actor->GetRootComponent());

#if WITH_EDITOR
                        for (int32 MaterialIndex = 0; MaterialIndex < Layer.Mesh->GetStaticMaterials().Num(); ++MaterialIndex)
                        {
                            if (UMaterialInterface* MaterialInterface = Layer.Mesh->GetMaterial(MaterialIndex))
                            {
                                MaterialInterface->CheckMaterialUsage(MATUSAGE_InstancedStaticMeshes);
                                HISM->SetMaterial(MaterialIndex, MaterialInterface);
                            }
                        }
#endif

                        HISM->RegisterComponent();

                        HISM->AddInstances(Transforms, false);
                    }
                }
            }

            // 4. NavMesh incremental rebuild
            if (bGenerationSucceeded && bNavMesh && Actor->MeshComponent && Actor->GetNetMode() != NM_Client)
            {
                FNavigationSystem::UpdateComponentData(*Actor->MeshComponent);
            }

            Actor->LastGenerationTimeSeconds = FPlatformTime::Seconds() - GenerationStartSeconds;
            Actor->LastEstimatedVoxelCount = EstimatedVoxelCount;
            Actor->LastPrimaryTriangleCount = PrimaryTriangleCount;
            Actor->LastLODTriangleCount = bLOD ? LODTriangleCount : 0;
            Actor->LastScatterInstanceCount = ScatterInstanceCount;
            Actor->bLastGenerationSucceeded = bGenerationSucceeded;
            Actor->LastGenerationError = GenerationError;

            if (!bGenerationSucceeded)
            {
                UE_LOG(LogIOC, Error, TEXT("Cave generation failed for '%s': %s"), *Actor->GetName(), *GenerationError);
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
            Actor->ActiveGeneration.Reset();
            Actor->bPendingRegeneration = false;
            Actor->BroadcastGenerationFinished(false, false);
        });
    });
}
