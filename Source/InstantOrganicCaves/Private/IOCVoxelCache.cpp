// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "IOCVoxelCache.h"

bool FIOCVoxelCache::TryRead(uint32 RecipeSignature, double VoxelSize, const FIntVector& GridDims,
                             TArray<bool>& OutVoxels) const
{
    const int64 Expected = (int64)GridDims.X * (int64)GridDims.Y * (int64)GridDims.Z;
    if (Expected <= 0)
    {
        return false;
    }

    FScopeLock Lock(&Mutex);

    for (const FEntry& Entry : Entries)
    {
        if (Entry.RecipeSignature != RecipeSignature ||
            Entry.GridDims != GridDims ||
            !FMath::IsNearlyEqual(Entry.VoxelSize, VoxelSize))
        {
            continue;
        }

        if ((int64)Entry.Bits.Num() != Expected)
        {
            // Dimensions agreed but the payload does not; treat as a miss rather than
            // risk handing back a partially sized grid.
            continue;
        }

        OutVoxels.SetNumUninitialized((int32)Expected);
        for (int32 Index = 0; Index < (int32)Expected; ++Index)
        {
            OutVoxels[Index] = Entry.Bits[Index];
        }
        return true;
    }

    return false;
}

void FIOCVoxelCache::Write(uint32 RecipeSignature, double VoxelSize, const FIntVector& GridDims,
                           const TArray<bool>& Voxels)
{
    const int64 Expected = (int64)GridDims.X * (int64)GridDims.Y * (int64)GridDims.Z;
    if (Expected <= 0 || (int64)Voxels.Num() != Expected)
    {
        return;
    }

    TBitArray<> Bits;
    Bits.Init(false, Voxels.Num());
    for (int32 Index = 0; Index < Voxels.Num(); ++Index)
    {
        Bits[Index] = Voxels[Index];
    }

    FScopeLock Lock(&Mutex);

    // One entry per voxel size: the primary grid and its LOD grid, nothing more. A cancelled
    // generation still running can land here after a newer one; replacing by voxel size keeps
    // that harmless, since the signature is stored alongside and checked on read.
    for (FEntry& Entry : Entries)
    {
        if (FMath::IsNearlyEqual(Entry.VoxelSize, VoxelSize))
        {
            Entry.RecipeSignature = RecipeSignature;
            Entry.GridDims = GridDims;
            Entry.Bits = MoveTemp(Bits);
            return;
        }
    }

    FEntry& NewEntry = Entries.AddDefaulted_GetRef();
    NewEntry.RecipeSignature = RecipeSignature;
    NewEntry.VoxelSize = VoxelSize;
    NewEntry.GridDims = GridDims;
    NewEntry.Bits = MoveTemp(Bits);
}

void FIOCVoxelCache::Invalidate()
{
    FScopeLock Lock(&Mutex);
    Entries.Reset();
}

int64 FIOCVoxelCache::GetApproxMemoryBytes() const
{
    FScopeLock Lock(&Mutex);

    int64 Total = 0;
    for (const FEntry& Entry : Entries)
    {
        Total += (int64)Entry.Bits.Num() / 8;
    }
    return Total;
}
