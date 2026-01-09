// Copyright (c) 2026 GregOrigin. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "PCGSettings.h"
#include "IOCVoxelCore.generated.h"

// Added 'DisplayName' meta - This fixes the missing name in the palette
UCLASS(BlueprintType, ClassGroup = (Procedural), meta = (DisplayName = "IOC Voxel Core"))
class INSTANTORGANICCAVES_API UIOCVoxelCoreSettings : public UPCGSettings
{
    GENERATED_BODY()

public:
    // --- Configuration ---
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Performance")
    float VoxelSize = 100.0f;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Organic Logic")
    float FillProbability = 0.48f;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Organic Logic")
    int32 SmoothingIterations = 3;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Organic Logic")
    int32 CaveSeed = 42;

    // --- VISIBILITY FIX ---
    // 1. Define the Type. 'Spatial' ensures it appears near Samplers/Spawners.
    virtual EPCGSettingsType GetType() const override { return EPCGSettingsType::Spatial; }

#if WITH_EDITOR
    // 2. Editor UI Text (The Safe Way)
    virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("IOC", "NodeTitle", "IOC Voxel Core"); }
    virtual FText GetNodeTooltipText() const override { return NSLOCTEXT("IOC", "NodeTooltip", "High-performance cellular automata cave generator."); }
#endif

    // --- Internal Factory ---
    virtual FPCGElementPtr CreateElement() const override;
};