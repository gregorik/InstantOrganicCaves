// Copyright (c) 2026 GregOrigin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "IOCVoxelCore.generated.h"

UENUM(BlueprintType)
enum class EIOCGenerationMode : uint8
{
    CellularAutomata UMETA(DisplayName = "Cellular Automata (Classic)"),
    PerlinTunnel UMETA(DisplayName = "Perlin Tunnel (Smooth)"),
    InfiniteCellularAutomata UMETA(DisplayName = "Cellular Automata (Infinite/Tileable)")
};

UCLASS(BlueprintType, ClassGroup = (Procedural), meta = (DisplayName = "IOC Voxel Core"))
class INSTANTORGANICCAVES_API UIOCVoxelCoreSettings : public UPCGSettings
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
    EIOCGenerationMode GenerationMode = EIOCGenerationMode::CellularAutomata;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
    float VoxelSize = 100.0f;

    // --- Cellular Automata Settings ---
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Cellular Automata", meta = (EditCondition = "GenerationMode == EIOCGenerationMode::CellularAutomata"))
    float FillProbability = 0.48f;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Cellular Automata", meta = (EditCondition = "GenerationMode == EIOCGenerationMode::CellularAutomata"))
    int32 CaveSeed = 42;

    // --- Perlin Tunnel Settings ---
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Perlin Tunnel", meta = (EditCondition = "GenerationMode == EIOCGenerationMode::PerlinTunnel"))
    FVector TunnelStart = FVector(0, 0, 0);

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Perlin Tunnel", meta = (EditCondition = "GenerationMode == EIOCGenerationMode::PerlinTunnel"))
    FVector TunnelEnd = FVector(1000, 1000, 1000);

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Perlin Tunnel", meta = (EditCondition = "GenerationMode == EIOCGenerationMode::PerlinTunnel"))
    float TunnelRadius = 300.0f;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Perlin Tunnel", meta = (EditCondition = "GenerationMode == EIOCGenerationMode::PerlinTunnel"))
    float WallThickness = 50.0f;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Perlin Tunnel", meta = (EditCondition = "GenerationMode == EIOCGenerationMode::PerlinTunnel"))
    float NoiseFrequency = 0.1f;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Perlin Tunnel", meta = (EditCondition = "GenerationMode == EIOCGenerationMode::PerlinTunnel"))
    float NoiseThreshold = 0.5f;

    // --- Infinite CA Settings ---
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Infinite CA",
        meta = (EditCondition = "GenerationMode == EIOCGenerationMode::InfiniteCellularAutomata"))
    FVector WorldOriginOffset = FVector::ZeroVector;

    // Common
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
    int32 SmoothingIterations = 3;

    /** Hard ceiling on the working grid. Generation is rejected (with a graph error) rather than
     *  allowed to exhaust memory. Includes the smoothing halo in Infinite mode. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings", meta = (ClampMin = "1000", ClampMax = "15000000"))
    int32 MaxVoxelCount = 15000000;

#if WITH_EDITOR
    virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }
    virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("IOC", "NodeTitle", "IOC Voxel Core"); }
#endif

    virtual FPCGElementPtr CreateElement() const override;
};
