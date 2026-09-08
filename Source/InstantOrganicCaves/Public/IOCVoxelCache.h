// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/BitArray.h"
#include "HAL/CriticalSection.h"

/**
 * Cache of the post-noise, pre-carve voxel grid for one cave actor.
 *
 * Why this exists: a carve used to cost a full regeneration, and the dominant term in that
 * is the noise fill -- up to four fBm fields of up to eight octaves each, per voxel. The
 * carve pass itself, the hole fill and the mesh extraction are all comparatively cheap.
 * Since carves never change the underlying density field, the field can be computed once
 * and replayed.
 *
 * The cache is keyed by a recipe signature covering every input to the fill (seed, noise
 * parameters, tunnel geometry, biome overrides, spline segments, actor transform, bounds),
 * plus the voxel size and grid dimensions. Change any generation parameter and the signature
 * stops matching, so a stale field can never be served -- there is no explicit invalidation
 * to forget to call.
 *
 * Carves are always replayed from the cached *pre-carve* grid rather than applied
 * incrementally to the previous result, because the carve history is not append-only:
 * ClearRuntimeCarves and the FIFO eviction both restore voxels.
 *
 * Storage is a TBitArray (1 bit per voxel) rather than the TArray<bool> the generator works
 * in (1 byte per voxel): a 2M-voxel streamed chunk costs ~250 KB instead of ~2 MB, which
 * matters when 64 chunks are resident. The working grid cannot itself be a TBitArray --
 * ParallelFor writes distinct voxels concurrently, and packed bits share words, which would
 * be a data race.
 *
 * All methods are safe to call from the generation worker thread. At most one entry is kept
 * per voxel size, so a cave holds the primary grid plus its LOD grid and nothing more.
 */
class INSTANTORGANICCAVES_API FIOCVoxelCache
{
public:
    /**
     * Fills OutVoxels from the cache if a stored grid matches the recipe, voxel size and
     * dimensions. Returns false and leaves OutVoxels untouched otherwise.
     */
    bool TryRead(uint32 RecipeSignature, double VoxelSize, const FIntVector& GridDims,
                 TArray<bool>& OutVoxels) const;

    /** Stores (or replaces) the pre-carve grid for this voxel size. */
    void Write(uint32 RecipeSignature, double VoxelSize, const FIntVector& GridDims,
               const TArray<bool>& Voxels);

    /** Drops everything. Not required for correctness -- the signature check covers staleness. */
    void Invalidate();

    /** Approximate resident size, for logging and budgeting. */
    int64 GetApproxMemoryBytes() const;

private:
    struct FEntry
    {
        uint32 RecipeSignature = 0;
        double VoxelSize = 0.0;
        FIntVector GridDims = FIntVector::ZeroValue;
        TBitArray<> Bits;
    };

    mutable FCriticalSection Mutex;
    TArray<FEntry> Entries;
};

/**
 * Accumulates the recipe signature for a voxel fill.
 *
 * Everything the fill reads must be folded in. A value that affects the density field but is
 * missing here would let the cache serve a grid that no longer matches the parameters, which
 * is the one way this design can produce a wrong result -- so err towards hashing too much.
 */
struct FIOCRecipeHasher
{
    uint32 Hash = 0x9E3779B9u;

    FIOCRecipeHasher& Add(bool Value) { Hash = HashCombine(Hash, Value ? 1u : 0u); return *this; }
    FIOCRecipeHasher& Add(int32 Value) { Hash = HashCombine(Hash, ::GetTypeHash(Value)); return *this; }
    FIOCRecipeHasher& Add(float Value) { Hash = HashCombine(Hash, ::GetTypeHash(Value)); return *this; }
    FIOCRecipeHasher& Add(double Value) { Hash = HashCombine(Hash, ::GetTypeHash(Value)); return *this; }

    FIOCRecipeHasher& Add(const FVector& Value)
    {
        return Add(Value.X).Add(Value.Y).Add(Value.Z);
    }

    FIOCRecipeHasher& Add(const FTransform& Value)
    {
        const FQuat Rotation = Value.GetRotation();
        Add(Value.GetLocation());
        Add(Rotation.X).Add(Rotation.Y).Add(Rotation.Z).Add(Rotation.W);
        return Add(Value.GetScale3D());
    }
};
