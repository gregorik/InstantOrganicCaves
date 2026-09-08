// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "IOCSettings.generated.h"

/**
 * Project-wide Instant Organic Caves settings (Project Settings > Plugins > Instant Organic Caves).
 *
 * These were previously either hardcoded in the generator or duplicated onto every actor.
 * The material path in particular was a string literal pointing into the plugin's own content
 * folder, which meant a project that forked or replaced the shipped materials had no way to
 * retarget the fallback.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Instant Organic Caves"))
class INSTANTORGANICCAVES_API UIOCSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UIOCSettings();

    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

    /** Convenience accessor. Never null: UDeveloperSettings CDOs always exist. */
    static const UIOCSettings& Get();

    /**
     * Material used for generated caves when an actor has no CaveMaterial set.
     * Falls back to the engine default surface material if this fails to load.
     */
    UPROPERTY(config, EditAnywhere, Category = "Content",
        meta = (AllowedClasses = "/Script/Engine.MaterialInterface", DisplayName = "Fallback Cave Material"))
    FSoftObjectPath FallbackCaveMaterial;

    /**
     * Hard ceiling on voxel cells per grid axis.
     *
     * The real budget guard is the per-actor MaxVoxelCount; this only stops a pathological
     * GenerationBounds / VoxelSize ratio from trying to allocate an absurd grid. Raising it
     * lets very long, thin tunnels resolve at fine voxel sizes, at the cost of more memory
     * during generation.
     */
    UPROPERTY(config, EditAnywhere, Category = "Limits",
        meta = (ClampMin = "16", ClampMax = "8192", DisplayName = "Max Grid Cells Per Axis"))
    int32 MaxGridAxis = 2048;

    /**
     * Upper bound on curve samples taken from a single spline when following it.
     * Higher values track tight curves more faithfully; the segment acceleration grid keeps
     * the per-voxel query cost flat either way.
     */
    UPROPERTY(config, EditAnywhere, Category = "Limits",
        meta = (ClampMin = "16", ClampMax = "16384", DisplayName = "Max Spline Samples"))
    int32 MaxSplineSamples = 4096;

    /**
     * Weld generated vertices that share a position instead of emitting four unshared
     * vertices per quad. Cuts mesh and collision memory substantially. Disable only if you
     * are chasing a mesh-topology difference against older content.
     */
    UPROPERTY(config, EditAnywhere, Category = "Generation",
        meta = (DisplayName = "Weld Generated Vertices"))
    bool bWeldGeneratedVertices = true;

    /**
     * Cache the pre-carve voxel field so a carve does not have to re-evaluate the noise.
     *
     * The noise fill dominates generation cost, and carves never change the underlying
     * density field, so it can be computed once and replayed. Costs roughly one bit per
     * voxel per cave (a 2M-voxel streamed chunk is ~250 KB), and only caves that actually
     * have carves populate a cache. Turn this off to trade carve latency for memory.
     */
    UPROPERTY(config, EditAnywhere, Category = "Generation",
        meta = (DisplayName = "Cache Voxel Field For Carving"))
    bool bCacheVoxelFieldForCarving = true;
};
