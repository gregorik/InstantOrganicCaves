// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SplineComponent.h"
#include "IOCCarvingComponent.h"
#include "IOCBiomeVolume.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include <atomic>
#include "IOCProceduralActor.generated.h"

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

USTRUCT(BlueprintType)
struct FIOCScatterLayer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
    UStaticMesh* Mesh = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta=(ClampMin="0.0"))
    float Density = 0.1f; // Instances per square meter

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
    FVector2D ScaleRange = FVector2D(0.8f, 1.2f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
    bool bAlignToNormal = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta=(ClampMin="0.0", ClampMax="1.0"))
    float MinSlopeZ = 0.7f; // 1.0 = Flat Floor, 0.0 = Vertical Wall, -1.0 = Ceiling

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta=(ClampMin="-1.0", ClampMax="1.0"))
    float MaxSlopeZ = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter")
    float RandomPitch = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scatter", meta=(ClampMin="0"))
    float PoissonMinSeparation = 0.0f;
    // 0 = random (legacy), > 0 = Poisson disk with this minimum distance in cm
};

// POD struct for thread-safe biome lookup
struct FIOCBiomeData
{
    FTransform Transform;
    FBoxSphereBounds Bounds;
    int32 Priority;

    bool bOverride_NoiseFrequency;
    float NoiseFrequency;
    bool bOverride_TunnelRadius;
    float TunnelRadius;
    bool bOverride_WallThickness;
    float WallThickness;
    bool bOverride_TerraceSteps;
    float TerraceSteps;
};

UCLASS()
class INSTANTORGANICCAVES_API AIOCProceduralActor : public AActor
{
    GENERATED_BODY()

public:
    AIOCProceduralActor();

    // ===================================================================
    // IOC — Main controls (preset, generate, status)
    // ===================================================================

    UPROPERTY(EditAnywhere, Category = "IOC")
    EIOCCavePreset CavePreset = EIOCCavePreset::Custom;

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "IOC", meta=(DisplayName="Generate Cave"))
    void RegenerateInEditor();

    UFUNCTION(CallInEditor, Category = "IOC", meta=(DisplayName="Apply Preset"))
    void ApplyPreset();

    void ApplyPresetSettingsOnly();

    UPROPERTY(VisibleAnywhere, Transient, Category = "IOC", meta=(DisplayName="Generating..."))
    bool bIsGeneratingDisplay = false;

    // ===================================================================
    // IOC|Tunnel — Tunnel geometry controls
    // ===================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Tunnel")
    bool bGenerateTunnel = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Tunnel", meta=(EditCondition="bGenerateTunnel"))
    bool bUseSpline = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Tunnel", meta=(EditCondition="bGenerateTunnel && !bUseSpline", MakeEditWidget=true))
    FVector TunnelStart = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Tunnel", meta=(EditCondition="bGenerateTunnel && !bUseSpline", MakeEditWidget=true))
    FVector TunnelEnd = FVector(1000, 0, 0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Tunnel", meta=(EditCondition="bGenerateTunnel"))
    float TunnelRadius = 300.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Tunnel", meta=(EditCondition="bGenerateTunnel"))
    float WallThickness = 60.0f;

    // ===================================================================
    // IOC|Shape — Noise, voxel resolution, and deformation
    // ===================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Shape")
    int32 CaveSeed = 1337;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Shape")
    FVector GenerationBounds = FVector(1000, 1000, 1000);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Shape")
    double VoxelSize = 50.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Shape")
    float NoiseFrequency = 0.005f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Shape")
    float NoiseThreshold = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Shape")
    int32 SmoothIterations = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Shape", meta=(ClampMin="1", ClampMax="8"))
    int32 NoiseOctaves = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Shape", meta=(ClampMin="1.1"))
    float NoiseLacunarity = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Shape", meta=(ClampMin="0.1", ClampMax="0.95"))
    float NoisePersistence = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Shape", meta=(DisplayName="Macro Chamber Weight", ClampMin="0.0", ClampMax="1.0"))
    float MacroChamberWeight = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Shape", meta=(DisplayName="Ridged Detail Weight", ClampMin="0.0", ClampMax="1.0"))
    float RidgedDetailWeight = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Shape", meta=(DisplayName="Interior Density Bias", ClampMin="-1.0", ClampMax="1.0"))
    float InteriorDensityBias = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Shape", meta=(DisplayName="Domain Warp Intensity", ClampMin="0.0"))
    float DomainWarpIntensity = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Shape", meta=(DisplayName="Terrace Steps", ClampMin="0.0"))
    float TerraceSteps = 0.0f; // 0 = disabled, >1 = step height

    // ===================================================================
    // IOC|Appearance — Material, tiling, vertex colors
    // ===================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Appearance")
    UMaterialInterface* CaveMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Appearance")
    float TextureTiling = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Appearance")
    bool bGenerateSmartColors = true;

    // ===================================================================
    // IOC|Decoration — Scatter layers
    // ===================================================================

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Decoration")
    TArray<FIOCScatterLayer> DecorationLayers;

    // ===================================================================
    // IOC|Advanced — LOD, streaming, nav, debug, bake
    // ===================================================================

    UPROPERTY(EditAnywhere, Category = "IOC|Advanced")
    bool bEnableLOD = true;

    UPROPERTY(EditAnywhere, Category = "IOC|Advanced", meta=(EditCondition="bEnableLOD"))
    float LODDistance = 5000.0f;

    UPROPERTY(EditAnywhere, Category = "IOC|Advanced", meta=(EditCondition="bEnableLOD"))
    float LODVoxelSizeMultiplier = 3.0f;

    UPROPERTY(EditAnywhere, Category = "IOC|Advanced")
    bool bUseWorldSpaceNoise = false;

    UPROPERTY(EditAnywhere, Category = "IOC|Advanced")
    bool bAutoRebuildNavMesh = false;

    UPROPERTY(EditAnywhere, Category = "IOC|Advanced", meta=(DisplayName="Force Preset on Play"))
    bool bForcePreset = false;

    UPROPERTY(EditAnywhere, Category = "IOC|Advanced")
    bool bShowDebugViz = true;

    UPROPERTY(EditAnywhere, Category = "IOC|Advanced", meta=(DisplayName="Log Preset Debug"))
    bool bLogPresetDebug = false;

#if WITH_EDITOR
    UFUNCTION(CallInEditor, Category = "IOC|Advanced", meta=(DisplayName="Bake to Static Mesh"))
    void BakeToStaticMesh();

    virtual void PostEditMove(bool bFinished) override;
#endif

    // ===================================================================
    // Components (auto-created, not user-facing)
    // ===================================================================

    UPROPERTY(VisibleAnywhere, Category = "Components")
    UDynamicMeshComponent* MeshComponent;

    UPROPERTY(VisibleAnywhere, Category = "Components")
    UDynamicMeshComponent* LODMeshComponent;

    UPROPERTY(VisibleAnywhere, Category = "Components")
    USplineComponent* CaveSpline;

    // ===================================================================
    // Gameplay API
    // ===================================================================

    UFUNCTION(BlueprintCallable, Category = "IOC Gameplay")
    void CarveAtLocation(FVector WorldLocation, float Radius);

    TArray<FIOCCarvingCapture> RuntimeCarves;

    // Public API
    void GenerateCave();

protected:
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;

private:
    bool ShouldAutoPreset() const;
    void ApplyPresetParams();
    void ApplyCaveMaterials();
    void ApplyCaveMaterialToComponent(UDynamicMeshComponent* TargetComponent) const;

    std::atomic<bool> bIsGenerating{ false };
    std::atomic<bool> bCancelRequested{ false };
    bool bPendingRegeneration = false;
    bool bLODActive = false;

    virtual void Tick(float DeltaTime) override;
};
