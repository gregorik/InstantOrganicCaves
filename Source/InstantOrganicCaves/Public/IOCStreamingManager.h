// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IOCProceduralActor.h"
#include "IOCStreamingManager.generated.h"

class APawn;

/**
 * AIOCStreamingManager
 *
 * Place one instance of this actor in the level. It ticks every frame to track
 * the player's position, spawning AIOCProceduralActor chunks within StreamRadius
 * and destroying them once they exceed StreamRadius * UnloadDistanceBias.
 *
 * All spawned chunks use bUseWorldSpaceNoise = true so noise samples are world-
 * relative and chunk borders are seamless.
 */
UCLASS()
class INSTANTORGANICCAVES_API AIOCStreamingManager : public AActor
{
    GENERATED_BODY()

public:
    AIOCStreamingManager();

    // --- Streaming Parameters ---

    /** World-space size of each chunk (X, Y used for grid; Z is generation depth). */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming")
    FVector ChunkSize = FVector(2000.0f, 2000.0f, 1500.0f);

    /** Number of chunks to keep loaded in each direction from the player chunk. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming", meta=(ClampMin="1", ClampMax="8"))
    int32 StreamRadius = 2;

    /** Chunks beyond StreamRadius * UnloadDistanceBias are destroyed. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming", meta=(ClampMin="1.0"))
    float UnloadDistanceBias = 1.2f;

    /** Snap the active player pawn to this cave's start point when play begins. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming|Coupling")
    bool bAutoCouplePlayerAtStart = true;

    /** Additional local offset applied to the player start point relative to this manager actor. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming|Coupling", meta=(EditCondition="bAutoCouplePlayerAtStart"))
    FVector CaveStartOffset = FVector::ZeroVector;

    /** Hold the player above the cave start while async chunk generation finishes. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming|Coupling", meta=(EditCondition="bAutoCouplePlayerAtStart", ClampMin="0.0"))
    float PlayerStartHoldHeight = 250.0f;

    /** Maximum downward trace distance used to release the player onto generated cave ground. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming|Coupling", meta=(EditCondition="bAutoCouplePlayerAtStart", ClampMin="100.0"))
    float GroundProbeDistance = 4000.0f;

    // --- Generation Parameters (copied to each spawned chunk) ---

    UPROPERTY(EditAnywhere, Category = "IOC Generation")
    int32 BaseSeed = 1337;

    UPROPERTY(EditAnywhere, Category = "IOC Generation")
    double VoxelSize = 80.0;

    UPROPERTY(EditAnywhere, Category = "IOC Generation")
    float NoiseFrequency = 0.005f;

    UPROPERTY(EditAnywhere, Category = "IOC Generation")
    float NoiseThreshold = 0.5f;

    UPROPERTY(EditAnywhere, Category = "IOC Generation")
    int32 SmoothIterations = 2;

    UPROPERTY(EditAnywhere, Category = "IOC Appearance")
    UMaterialInterface* SharedMaterial = nullptr;

    UPROPERTY(EditAnywhere, Category = "IOC Appearance")
    float TextureTiling = 0.01f;

    UPROPERTY(EditAnywhere, Category = "IOC Decoration")
    TArray<FIOCScatterLayer> SharedDecorationLayers;

    // --- Tunnel Mode (applied to each spawned chunk) ---

    UPROPERTY(EditAnywhere, Category = "IOC Tunnel")
    bool bGenerateTunnel = false;

    UPROPERTY(EditAnywhere, Category = "IOC Tunnel", meta=(EditCondition="bGenerateTunnel"))
    float TunnelRadius = 300.0f;

    UPROPERTY(EditAnywhere, Category = "IOC Tunnel", meta=(EditCondition="bGenerateTunnel"))
    float WallThickness = 60.0f;

    // --- Spectacular Features ---

    UPROPERTY(EditAnywhere, Category = "IOC Spectacular", meta=(ClampMin="0.0"))
    float DomainWarpIntensity = 0.0f;

    UPROPERTY(EditAnywhere, Category = "IOC Spectacular", meta=(ClampMin="0.0"))
    float TerraceSteps = 0.0f;

    // --- LOD ---

    UPROPERTY(EditAnywhere, Category = "IOC LOD")
    bool bEnableLOD = true;

    UPROPERTY(EditAnywhere, Category = "IOC LOD", meta=(EditCondition="bEnableLOD"))
    float LODDistance = 5000.0f;

    UPROPERTY(EditAnywhere, Category = "IOC LOD", meta=(EditCondition="bEnableLOD"))
    float LODVoxelSizeMultiplier = 3.0f;

    // --- Navigation ---

    UPROPERTY(EditAnywhere, Category = "IOC Navigation")
    bool bAutoRebuildNavMesh = false;

    // --- Preset ---

    UPROPERTY(EditAnywhere, Category = "IOC Generation")
    EIOCCavePreset CavePreset = EIOCCavePreset::Custom;

protected:
    virtual void BeginPlay() override;

private:
    TMap<FIntPoint, AIOCProceduralActor*> LoadedChunks;
    FIntPoint LastPlayerChunk = FIntPoint(INT_MAX, INT_MAX);
    TWeakObjectPtr<APawn> CoupledPawn;
    FVector CoupledPlayerStartLocation = FVector::ZeroVector;
    FVector CoupledPlayerHoldLocation = FVector::ZeroVector;
    bool bHasCoupledPlayerAtStart = false;
    bool bHoldingPlayerForInitialGround = false;

    FIntPoint WorldToChunk(FVector WorldPos) const;
    FVector ChunkToWorld(FIntPoint Coord) const;
    FVector GetDesiredPlayerStartLocation(const APawn* Pawn) const;
    void UpdateStreamingForLocation(const FVector& PlayerLoc);
    bool TryCouplePlayerToCaveStart();
    void UpdateInitialPlayerHold();
    void LoadChunk(FIntPoint Coord);
    void UnloadChunk(FIntPoint Coord);

    virtual void Tick(float DeltaTime) override;
};
