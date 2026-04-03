// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "IOCBiomeVolume.generated.h"

/**
 * A volume that overrides procedural generation settings for the cave parts inside it.
 */
UCLASS(Blueprintable, hidecategories=(Collision, Brush, Attachment))
class INSTANTORGANICCAVES_API AIOCBiomeVolume : public AVolume
{
    GENERATED_BODY()

public:
    AIOCBiomeVolume();

    // Priority for overlapping volumes. Higher values take precedence.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC Biome")
    int32 Priority = 0;

    // --- Overrides ---

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC Biome", meta=(InlineEditConditionToggle))
    bool bOverride_NoiseFrequency = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC Biome", meta=(EditCondition="bOverride_NoiseFrequency"))
    float NoiseFrequency = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC Biome", meta=(InlineEditConditionToggle))
    bool bOverride_TunnelRadius = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC Biome", meta=(EditCondition="bOverride_TunnelRadius"))
    float TunnelRadius = 200.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC Biome", meta=(InlineEditConditionToggle))
    bool bOverride_WallThickness = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC Biome", meta=(EditCondition="bOverride_WallThickness"))
    float WallThickness = 40.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC Biome", meta=(InlineEditConditionToggle))
    bool bOverride_TerraceSteps = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC Biome", meta=(EditCondition="bOverride_TerraceSteps"))
    float TerraceSteps = 0.0f;
};
