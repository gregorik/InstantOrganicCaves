// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SplineComponent.h"
#include "IOCCarvingComponent.h"
#include "IOCBiomeVolume.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "IOCVoxelCache.h"
#include <atomic>
#include "IOCProceduralActor.generated.h"

class AIOCProceduralActor;

UENUM(BlueprintType)
enum class EIOCCavePreset : uint8
{
    Custom        UMETA(DisplayName = "Custom"),
    LargeTunnel   UMETA(DisplayName = "Large Tunnel"),
    TightCrawl    UMETA(DisplayName = "Tight Crawlspace"),
    OpenCavern    UMETA(DisplayName = "Open Cavern"),
    AlienHive     UMETA(DisplayName = "Alien Hive (Warped)"),
    CanyonStrata  UMETA(DisplayName = "Canyon Strata (Terraced)")
};

UENUM(BlueprintType)
enum class EIOCBakeCollisionMode : uint8
{
    ProjectDefault  UMETA(DisplayName = "Project Default"),
    ComplexAsSimple UMETA(DisplayName = "Complex as Simple")
};

USTRUCT(BlueprintType)
struct FIOCScatterLayer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
    TObjectPtr<UStaticMesh> Mesh = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta=(ClampMin="0.0"))
    float Density = 0.1f; // Instances per square meter

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
    FVector2D ScaleRange = FVector2D(0.8f, 1.2f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
    bool bAlignToNormal = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta=(ClampMin="-1.0", ClampMax="1.0", UIMin="-1.0", UIMax="1.0"))
    float MinSlopeZ = 0.7f; // 1.0 = Flat Floor, 0.0 = Vertical Wall, -1.0 = Ceiling

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta=(ClampMin="-1.0", ClampMax="1.0", UIMin="-1.0", UIMax="1.0"))
    float MaxSlopeZ = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta=(ClampMin="0.0", ClampMax="180.0", UIMin="0.0", UIMax="45.0"))
    float RandomPitch = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta=(ClampMin="0"))
    float PoissonMinSeparation = 0.0f;
    // 0 = random (legacy), > 0 = Poisson disk with this minimum distance in cm
};

// POD struct for thread-safe biome lookup
struct FIOCBiomeData
{
    FTransform Transform = FTransform::Identity;
    FVector LocalCenter = FVector::ZeroVector;
    FVector LocalExtents = FVector::ZeroVector;
    int32 Priority = 0;

    bool bOverride_NoiseFrequency = false;
    float NoiseFrequency = 0.0f;
    bool bOverride_TunnelRadius = false;
    float TunnelRadius = 0.0f;
    bool bOverride_WallThickness = false;
    float WallThickness = 0.0f;
    bool bOverride_TerraceSteps = false;
    float TerraceSteps = 0.0f;
};

// Native delegates for C++ callers (the setup wizard binds lambdas to these).
DECLARE_MULTICAST_DELEGATE_OneParam(FIOCGenerationStartedSignature, AIOCProceduralActor*);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FIOCGenerationFinishedSignature, AIOCProceduralActor*, bool, bool);

// Blueprint-facing equivalents. Generation is asynchronous and can take seconds, so a
// Blueprint that wants to gate gameplay on a finished cave previously had no way to know:
// it had to poll bIsGeneratingDisplay on tick. These are broadcast alongside the native ones.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIOCGenerationStartedDynamic,
    AIOCProceduralActor*, Cave);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FIOCGenerationFinishedDynamic,
    AIOCProceduralActor*, Cave, bool, bCancelled, bool, bWillRegenerate);

UCLASS()
class INSTANTORGANICCAVES_API AIOCProceduralActor : public AActor
{
    GENERATED_BODY()

public:
    AIOCProceduralActor();

    // ===================================================================
    // IOC — Main controls (preset, generate, status)
    // ===================================================================

    UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC")
    EIOCCavePreset CavePreset = EIOCCavePreset::Custom;

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "IOC", meta=(DisplayName="Generate Cave"))
    void RegenerateInEditor();

    UFUNCTION(CallInEditor, Category = "IOC", meta=(DisplayName="Apply Preset"))
    void ApplyPreset();

    void ApplyPresetSettingsOnly();

    UPROPERTY(VisibleAnywhere, Transient, Category = "IOC", meta=(DisplayName="Generating..."))
    bool bIsGeneratingDisplay = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "IOC|Performance")
    double LastGenerationTimeSeconds = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "IOC|Performance")
    int64 LastEstimatedVoxelCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "IOC|Performance")
    int32 LastPrimaryTriangleCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "IOC|Performance")
    int32 LastLODTriangleCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "IOC|Performance")
    int32 LastScatterInstanceCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "IOC|Performance")
    bool bLastGenerationSucceeded = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "IOC|Performance")
    FString LastGenerationError;

    // ===================================================================
    // IOC|Tunnel — Tunnel geometry controls
    // ===================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Tunnel")
    bool bGenerateTunnel = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Tunnel", meta=(EditCondition="bGenerateTunnel"))
    bool bUseSpline = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Tunnel", meta=(EditCondition="bGenerateTunnel && !bUseSpline", MakeEditWidget=true))
    FVector TunnelStart = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Tunnel", meta=(EditCondition="bGenerateTunnel && !bUseSpline", MakeEditWidget=true))
    FVector TunnelEnd = FVector(1000, 0, 0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Tunnel", meta=(EditCondition="bGenerateTunnel"))
    float TunnelRadius = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Tunnel", meta=(EditCondition="bGenerateTunnel"))
    float WallThickness = 60.0f;

    // ===================================================================
    // IOC|Shape — Noise, voxel resolution, and deformation
    // ===================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Shape")
    int32 CaveSeed = 1337;

    /**
     * Requested world-space size of the generated volume.
     *
     * The working grid is capped at 2048 cells per axis, so bounds larger than
     * 2048 * VoxelSize on any axis are truncated (with a warning in the log) and the total
     * cell count is still subject to MaxVoxelCount.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Shape")
    FVector GenerationBounds = FVector(1000, 1000, 1000);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Shape")
    double VoxelSize = 50.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Shape")
    float NoiseFrequency = 0.005f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Shape")
    float NoiseThreshold = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Shape")
    int32 SmoothIterations = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Shape", meta=(ClampMin="1", ClampMax="8"))
    int32 NoiseOctaves = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Shape", meta=(ClampMin="1.1"))
    float NoiseLacunarity = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Shape", meta=(ClampMin="0.1", ClampMax="0.95"))
    float NoisePersistence = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Shape", meta=(DisplayName="Macro Chamber Weight", ClampMin="0.0", ClampMax="1.0"))
    float MacroChamberWeight = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Shape", meta=(DisplayName="Ridged Detail Weight", ClampMin="0.0", ClampMax="1.0"))
    float RidgedDetailWeight = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Shape", meta=(DisplayName="Interior Density Bias", ClampMin="-1.0", ClampMax="1.0"))
    float InteriorDensityBias = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Shape", meta=(DisplayName="Domain Warp Intensity", ClampMin="0.0"))
    float DomainWarpIntensity = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Shape", meta=(DisplayName="Terrace Steps", ClampMin="0.0"))
    float TerraceSteps = 0.0f; // 0 = disabled, >1 = step height

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Production", meta=(ClampMin="1000", ClampMax="15000000"))
    int32 MaxVoxelCount = 15000000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Production", meta=(ClampMin="1000"))
    int32 MaxGeneratedTriangles = 2000000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Production", meta=(ClampMin="0"))
    int32 MaxScatterInstances = 100000;

    // ===================================================================
    // IOC|Appearance — Material, tiling, vertex colors
    // ===================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Appearance")
    TObjectPtr<UMaterialInterface> CaveMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Appearance")
    float TextureTiling = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Appearance")
    bool bGenerateSmartColors = true;

    // ===================================================================
    // IOC|Decoration — Scatter layers
    // ===================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Decoration")
    TArray<FIOCScatterLayer> DecorationLayers;

    // ===================================================================
    // IOC|Advanced — LOD, streaming, nav, debug, bake
    // ===================================================================

    UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Advanced")
    bool bEnableLOD = true;

    UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Advanced", meta=(EditCondition="bEnableLOD"))
    float LODDistance = 5000.0f;

    UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Advanced", meta=(EditCondition="bEnableLOD"))
    float LODVoxelSizeMultiplier = 3.0f;

    UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Advanced")
    bool bUseWorldSpaceNoise = false;

    UPROPERTY(EditAnywhere, ReplicatedUsing=OnRep_GenerationSettings, Category = "IOC|Advanced")
    bool bUseFixedBoundsForTunnel = false;

    UPROPERTY(EditAnywhere, Category = "IOC|Advanced")
    bool bAutoRebuildNavMesh = false;

    UPROPERTY(EditAnywhere, Category = "IOC|Advanced", meta=(DisplayName="Force Preset on Play"))
    bool bForcePreset = false;

    UPROPERTY(EditAnywhere, Category = "IOC|Advanced")
    bool bShowDebugViz = true;

    UPROPERTY(EditAnywhere, Category = "IOC|Advanced", meta=(DisplayName="Log Preset Debug"))
    bool bLogPresetDebug = false;

    UPROPERTY(EditAnywhere, Category = "IOC|Production|Runtime Carving", meta=(ClampMin="0", ClampMax="4096"))
    int32 MaxRuntimeCarves = 256;

    UPROPERTY(EditAnywhere, Category = "IOC|Production|Runtime Carving", meta=(ClampMin="1.0"))
    float MinRuntimeCarveRadius = 25.0f;

    UPROPERTY(EditAnywhere, Category = "IOC|Production|Runtime Carving", meta=(ClampMin="1.0"))
    float MaxRuntimeCarveRadius = 5000.0f;

    UPROPERTY(EditAnywhere, Category = "IOC|Production|Bake")
    FString BakedAssetBaseName = TEXT("SM_IOC_Cave");

    UPROPERTY(EditAnywhere, Category = "IOC|Production|Bake")
    TObjectPtr<UMaterialInterface> BakeMaterialOverride = nullptr;

    UPROPERTY(EditAnywhere, Category = "IOC|Production|Bake")
    bool bBakeGeneratedLOD = true;

    UPROPERTY(EditAnywhere, Category = "IOC|Production|Bake")
    bool bBakeEnableNanite = false;

    UPROPERTY(EditAnywhere, Category = "IOC|Production|Bake")
    EIOCBakeCollisionMode BakeCollisionMode = EIOCBakeCollisionMode::ComplexAsSimple;

    UPROPERTY(EditAnywhere, Category = "IOC|Production|Bake", meta=(ClampMin="0.01", ClampMax="1.0"))
    float BakedLODScreenSize = 0.25f;

#if WITH_EDITOR
    UFUNCTION(CallInEditor, Category = "IOC|Advanced", meta=(DisplayName="Bake to Static Mesh"))
    void BakeToStaticMesh();

    virtual void PostEditMove(bool bFinished) override;

    /** Editor viewports must tick this actor so the debug gizmos can be re-issued; the
     *  transient line batcher expires them after about a second. */
    virtual bool ShouldTickIfViewportsOnly() const override;
#endif

    // ===================================================================
    // Components (auto-created, not user-facing)
    // ===================================================================

    UPROPERTY(VisibleAnywhere, Category = "Components")
    TObjectPtr<UDynamicMeshComponent> MeshComponent;

    UPROPERTY(VisibleAnywhere, Category = "Components")
    TObjectPtr<UDynamicMeshComponent> LODMeshComponent;

    UPROPERTY(VisibleAnywhere, Category = "Components")
    TObjectPtr<USplineComponent> CaveSpline;

    // ===================================================================
    // Gameplay API
    // ===================================================================

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IOC Gameplay")
    void CarveAtLocation(FVector WorldLocation, float Radius);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IOC Gameplay")
    void ClearRuntimeCarves();

    UFUNCTION(BlueprintCallable, Category = "IOC|Performance")
    FString GetPerformanceSummary() const;

    /**
     * Authoritative carve history, delta-replicated.
     *
     * BREAKING (0.3.7): this was TArray<FIOCCarvingCapture>. It is now FIOCCarveHistory so a
     * carve replicates as a delta rather than resending the entire array. C++ that iterated it
     * directly should use RuntimeCarves.Items or GetRuntimeCarves(); Blueprint should use
     * GetRuntimeCarves(). The SaveGame layout changed with it.
     */
    UPROPERTY(BlueprintReadOnly, SaveGame, Replicated, Category = "IOC Gameplay")
    FIOCCarveHistory RuntimeCarves;

    /** Snapshot of the carve history as plain values, for Blueprint and save code. */
    UFUNCTION(BlueprintCallable, Category = "IOC Gameplay")
    TArray<FIOCCarvingCapture> GetRuntimeCarves() const;

    /**
     * Schedules the rebuild a carve requires, coalescing bursts within a frame.
     * Public because the replicated carve history calls it from PostReplicatedReceive.
     */
    void RequestCarveRebuild();

    /** Fires when a generation task is dispatched. */
    UPROPERTY(BlueprintAssignable, Category = "IOC|Events")
    FIOCGenerationStartedDynamic OnGenerationStartedEvent;

    /**
     * Fires when a generation task settles.
     * bCancelled is true if it was superseded; bWillRegenerate is true when another pass is
     * already queued, so listeners can skip intermediate results.
     */
    UPROPERTY(BlueprintAssignable, Category = "IOC|Events")
    FIOCGenerationFinishedDynamic OnGenerationFinishedEvent;

    // Public API
    UFUNCTION(BlueprintCallable, Category = "IOC")
    void GenerateCave();

    UFUNCTION(BlueprintCallable, Category = "IOC")
    void RequestRegeneration();

    FIOCGenerationStartedSignature OnGenerationStarted;
    FIOCGenerationFinishedSignature OnGenerationFinished;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Destroyed() override;

private:
    struct FIOCGenerationState
    {
        std::atomic<bool> bCancelRequested{ false };
    };

#if WITH_EDITOR
    /** Draws the bounds/tunnel/spline gizmos. Called from OnConstruction, PostEditMove and
     *  the editor viewport tick, because debug lines only live for about one second. */
    void DrawConfigurationDebug() const;
#endif

    bool ShouldAutoPreset() const;
    void ApplyPresetParams();
    void ApplyCaveMaterials();
    void ApplyCaveMaterialToComponent(UDynamicMeshComponent* TargetComponent) const;
    void SyncLODVisibility();
    void CancelActiveGeneration();
    void BroadcastGenerationFinished(bool bCancelled, bool bWillRegenerate);

    UFUNCTION()
    void OnRep_GenerationSettings();

    std::atomic<bool> bIsGenerating{ false };
    TSharedPtr<FIOCGenerationState, ESPMode::ThreadSafe> ActiveGeneration;

    /**
     * Pre-carve voxel field, so repeat carves skip the noise evaluation.
     * Shared rather than owned outright: a cancelled generation can still be running when the
     * actor is torn down, and the worker holds a reference for the life of its task.
     */
    TSharedPtr<FIOCVoxelCache, ESPMode::ThreadSafe> VoxelCache;
    bool bPendingRegeneration = false;
    bool bLODActive = false;
    bool bHasValidLOD = false;

    virtual void Tick(float DeltaTime) override;
};
