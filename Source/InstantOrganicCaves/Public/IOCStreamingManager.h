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
 * Place one instance of this actor in the level. At runtime the authority tracks
 * all player pawns, spawns replicated AIOCProceduralActor chunks within
 * StreamRadius, and destroys chunks once no tracked pawn is within the unload
 * radius. Chunk creation is throttled and capped to protect frame time/memory.
 *
 * All spawned chunks use a voxel-aligned grid and world-space noise. The cave
 * generator samples a one-voxel halo for streamed density fields so neighboring
 * chunks agree on boundary faces. Materials and project-specific collision
 * profiles should still be validated on every shipping platform.
 *
 * Chunks are keyed in three dimensions. VerticalStreamRadius defaults to 0, which reproduces
 * the original single-layer behaviour exactly; raising it streams cave systems that are
 * deeper than one chunk, at a cost of (2*V+1) times as many chunks.
 */
UCLASS()
class INSTANTORGANICCAVES_API AIOCStreamingManager : public AActor
{
    GENERATED_BODY()

public:
    AIOCStreamingManager();

    // --- Streaming Parameters ---

    /** Requested world-space size. Runtime dimensions are rounded to whole voxels to keep neighboring grids aligned. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming")
    FVector ChunkSize = FVector(2000.0f, 2000.0f, 1500.0f);

    /** Number of chunks to keep loaded horizontally in each direction from the player chunk. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming", meta=(ClampMin="1", ClampMax="8"))
    int32 StreamRadius = 2;

    /**
     * Number of chunk layers to keep loaded above and below the player.
     *
     * 0 streams a single horizontal layer, which is what this manager did historically and
     * remains the default. 1 loads three layers, 2 loads five, and so on -- note the total
     * chunk count scales by (2 * VerticalStreamRadius + 1), so raise MaxLoadedChunks to match
     * or the furthest chunks will simply be dropped.
     */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming", meta=(ClampMin="0", ClampMax="4"))
    int32 VerticalStreamRadius = 0;

    /** Chunks beyond StreamRadius * UnloadDistanceBias are destroyed. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming", meta=(ClampMin="1.0"))
    float UnloadDistanceBias = 1.2f;

    /** Maximum number of generated chunks retained across all tracked players. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming|Production", meta=(ClampMin="1", ClampMax="1024"))
    int32 MaxLoadedChunks = 64;

    /** Maximum new chunk actors spawned during one streaming update. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming|Production", meta=(ClampMin="1", ClampMax="32"))
    int32 MaxChunkLoadsPerUpdate = 2;

    /** Server-side streaming update cadence. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming|Production", meta=(ClampMin="0.05", ClampMax="5.0"))
    float StreamingUpdateInterval = 0.25f;

    /** Seconds without a possessed player before generated chunks are released. Zero unloads immediately. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming|Production", meta=(ClampMin="0.0", ClampMax="300.0"))
    float NoPlayerUnloadDelay = 10.0f;

    /** Maximum streamed carve operations retained across chunk unload/reload. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming|Production|Runtime Carving", meta=(ClampMin="0", ClampMax="4096"))
    int32 MaxStreamedRuntimeCarves = 256;

    UPROPERTY(EditAnywhere, Category = "IOC Streaming|Production|Runtime Carving", meta=(ClampMin="1.0"))
    float MinStreamedCarveRadius = 25.0f;

    UPROPERTY(EditAnywhere, Category = "IOC Streaming|Production|Runtime Carving", meta=(ClampMin="1.0"))
    float MaxStreamedCarveRadius = 5000.0f;

    UPROPERTY(EditAnywhere, Category = "IOC Streaming|Production|Runtime Carving", meta=(ClampMin="0.0"))
    float StreamedCarveFalloff = 50.0f;

    /** Authority-owned carve history applied to current and future chunks; serialize the manager to persist it. */
    UPROPERTY(BlueprintReadOnly, SaveGame, Category = "IOC Streaming|Production|Runtime Carving")
    TArray<FIOCCarvingCapture> StreamedRuntimeCarves;

    /** Snap the active player pawn to this cave's start point when play begins. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming|Coupling")
    bool bAutoCouplePlayerAtStart = false;

    /** Player coupling is disabled in network games unless explicitly opted in. */
    UPROPERTY(EditAnywhere, Category = "IOC Streaming|Coupling", meta=(EditCondition="bAutoCouplePlayerAtStart"))
    bool bAllowCouplingInNetworkGames = false;

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

    UPROPERTY(EditAnywhere, Category = "IOC Generation|Production", meta=(ClampMin="1000", ClampMax="15000000"))
    int32 MaxVoxelCountPerChunk = 2000000;

    UPROPERTY(EditAnywhere, Category = "IOC Generation|Production", meta=(ClampMin="1000"))
    int32 MaxTrianglesPerChunk = 500000;

    UPROPERTY(EditAnywhere, Category = "IOC Generation|Production", meta=(ClampMin="0"))
    int32 MaxScatterInstancesPerChunk = 10000;

    UPROPERTY(EditAnywhere, Category = "IOC Appearance")
    TObjectPtr<UMaterialInterface> SharedMaterial = nullptr;

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

    /** Streaming uses the nearest whole-number multiplier so both LOD grids divide chunk dimensions exactly. */
    UPROPERTY(EditAnywhere, Category = "IOC LOD", meta=(EditCondition="bEnableLOD", ClampMin="1.0", ClampMax="16.0"))
    float LODVoxelSizeMultiplier = 3.0f;

    // --- Navigation ---

    UPROPERTY(EditAnywhere, Category = "IOC Navigation")
    bool bAutoRebuildNavMesh = false;

    // --- Preset ---

    UPROPERTY(EditAnywhere, Category = "IOC Generation")
    EIOCCavePreset CavePreset = EIOCCavePreset::Custom;

    /** Force-load a preview grid centered on this world-space location. Useful for editor showcases and scripted demos. */
    UFUNCTION(BlueprintCallable, Category = "IOC Streaming")
    void RebuildAroundLocation(FVector WorldLocation);

    /** Returns the voxel/LOD-aligned dimensions actually used for chunk spacing and generation. */
    UFUNCTION(BlueprintPure, Category = "IOC Streaming")
    FVector GetEffectiveChunkSize() const;

    UFUNCTION(BlueprintPure, Category = "IOC Streaming")
    int32 GetLoadedChunkCount() const { return LoadedChunks.Num(); }

    UFUNCTION(BlueprintPure, Category = "IOC Streaming")
    int32 GetPendingChunkCount() const { return PendingLoadCoords.Num(); }

    /** Adds an authoritative sphere carve to every affected loaded chunk and to future reloaded chunks. */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IOC Streaming|Runtime Carving")
    void CarveStreamedCavesAtLocation(FVector WorldLocation, float Radius);

    /** Clears manager-owned carve history and regenerates every loaded chunk. */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "IOC Streaming|Runtime Carving")
    void ClearStreamedRuntimeCarves();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    /**
     * Weak handles, not raw pointers: chunk actors can also be destroyed by level
     * teardown, undo, or project code, and a raw UObject* here would be invisible to
     * the GC and left dangling. Every read goes through PruneInvalidChunks() or an
     * explicit .Get() null check.
     */
    TMap<FIntVector, TWeakObjectPtr<AIOCProceduralActor>> LoadedChunks;
    TArray<FIntVector> PendingLoadCoords;
    TWeakObjectPtr<APawn> CoupledPawn;
    FVector CoupledPlayerStartLocation = FVector::ZeroVector;
    FVector CoupledPlayerHoldLocation = FVector::ZeroVector;
    bool bHasCoupledPlayerAtStart = false;
    bool bHoldingPlayerForInitialGround = false;
    float TimeWithoutTrackedPlayers = 0.0f;

    double GetStreamingLODMultiplier() const;
    FIntVector WorldToChunk(FVector WorldPos) const;
    FVector ChunkToWorld(FIntVector Coord) const;
    FVector GetDesiredPlayerStartLocation(const APawn* Pawn) const;
    void CollectTrackedPlayerLocations(TArray<FVector>& OutLocations) const;
    void UpdateStreamingForLocations(const TArray<FVector>& PlayerLocations);
    void ProcessPendingLoads();
    float GetMinChunkDistanceSquared(FIntVector Coord, const TArray<FVector>& PlayerLocations) const;
    bool TryCouplePlayerToCaveStart();
    void UpdateInitialPlayerHold();
    void LoadChunk(FIntVector Coord);
    void UnloadChunk(FIntVector Coord);
    void UnloadAllChunks();
    /** Drops entries whose actor has been destroyed outside UnloadChunk(). */
    void PruneInvalidChunks();
    bool DoesCarveAffectChunk(const FIOCCarvingCapture& Carve, const AIOCProceduralActor* Chunk) const;

    virtual void Tick(float DeltaTime) override;
};
