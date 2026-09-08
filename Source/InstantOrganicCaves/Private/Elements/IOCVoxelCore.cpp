// Copyright (c) 2026 GregOrigin. All Rights Reserved.


#include "Elements/IOCVoxelCore.h"

// Framework Includes
#include "PCGElement.h"          
#include "PCGContext.h"          
#include "Data/PCGSpatialData.h"
#include "Misc/EngineVersionComparison.h"

// UPCGBasePointData -- the structure-of-arrays point container -- arrived in 5.6. On the
// 5.5 floor UPCGPointData with its TArray<FPCGPoint> is the only representation that
// exists, so the output stage below has two implementations rather than one written
// against the older API on every engine.
#if UE_VERSION_OLDER_THAN(5, 6, 0)
#include "Data/PCGPointData.h"
#else
#include "PCGCommon.h"
#include "Data/PCGBasePointData.h"
#endif
#include "Async/ParallelFor.h"
#include "GenericPlatform/GenericPlatformMath.h" 

#define LOCTEXT_NAMESPACE "FIOCVoxelCoreElement"

class FIOCVoxelCoreElement : public IPCGElement
{
public:
    virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

FPCGElementPtr UIOCVoxelCoreSettings::CreateElement() const
{
    return MakeShared<FIOCVoxelCoreElement>();
}

FORCEINLINE int32 GetIndex(int32 X, int32 Y, int32 Z, int32 SizeX, int32 SizeY)
{
    return X + (Y * SizeX) + (Z * SizeX * SizeY);
}

/**
 * MurmurHash3 finalizer. A single LCG step is not a hash: consecutive inputs stay a
 * fixed stride apart, so seeding a grid with one yields a regular lattice instead of
 * white noise. This avalanches every input bit.
 */
FORCEINLINE uint32 IOCMixBits(uint32 Value)
{
    Value ^= Value >> 16;
    Value *= 0x85ebca6bu;
    Value ^= Value >> 13;
    Value *= 0xc2b2ae35u;
    Value ^= Value >> 16;
    return Value;
}

/** Deterministic white noise in [0,1) for a voxel coordinate; stable across grid resizes. */
FORCEINLINE float IOCCellRandom01(int32 X, int32 Y, int32 Z, int32 Seed)
{
    const uint32 Hash = IOCMixBits(
        (static_cast<uint32>(X) * 73856093u) ^
        (static_cast<uint32>(Y) * 19349663u) ^
        (static_cast<uint32>(Z) * 83492791u) ^
        IOCMixBits(static_cast<uint32>(Seed)));

    // Top 24 bits -> [0,1); avoids the rounding bias of dividing by UINT32_MAX.
    return static_cast<float>(Hash >> 8) * (1.0f / 16777216.0f);
}

bool FIOCVoxelCoreElement::ExecuteInternal(FPCGContext* Context) const
{
    const UIOCVoxelCoreSettings* Settings = Context->GetInputSettings<UIOCVoxelCoreSettings>();
    check(Settings);

    const UPCGSpatialData* InputSpatialData = nullptr;
    TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

    if (Inputs.Num() > 0)
    {
        InputSpatialData = Cast<UPCGSpatialData>(Inputs[0].Data);
    }

    FBox Bounds = InputSpatialData ? InputSpatialData->GetBounds() : FBox(FVector(-1000), FVector(1000));

    const float SafeVoxelSize = FMath::Max(Settings->VoxelSize, 1.0f);
    FVector Extent = Bounds.GetSize();
    int32 SizeX = FMath::Max(1, FMath::RoundToInt(Extent.X / SafeVoxelSize));
    int32 SizeY = FMath::Max(1, FMath::RoundToInt(Extent.Y / SafeVoxelSize));
    int32 SizeZ = FMath::Max(1, FMath::RoundToInt(Extent.Z / SafeVoxelSize));
    int64 TotalVoxels = (int64)SizeX * (int64)SizeY * (int64)SizeZ;

    const int64 VoxelBudget = (int64)FMath::Clamp(Settings->MaxVoxelCount, 1000, 15000000);
    if (TotalVoxels > VoxelBudget)
    {
        PCGLog::LogErrorOnGraph(
            FText::Format(
                LOCTEXT("IOCError", "IOC Voxel Core: {0} x {1} x {2} = {3} voxels exceeds the {4} voxel budget. Increase Voxel Size, reduce the input bounds, or raise Max Voxel Count."),
                SizeX, SizeY, SizeZ, TotalVoxels, VoxelBudget),
            Context);
        return false;
    }

    const int32 NumVoxels = static_cast<int32>(TotalVoxels);

    TArray<uint8> GridA;
    TArray<uint8> GridB;
    GridA.SetNumZeroed(NumVoxels);
    
    // --- Logic Selection ---
    
    if (Settings->GenerationMode == EIOCGenerationMode::CellularAutomata)
    {
        // 1. Noise
        const int32 Seed = Settings->CaveSeed;
        const float FillProbability = Settings->FillProbability;
        ParallelFor(NumVoxels, [&](int32 Index)
        {
            const int32 z = Index / (SizeX * SizeY);
            const int32 rem = Index % (SizeX * SizeY);
            const int32 y = rem / SizeX;
            const int32 x = rem % SizeX;

            GridA[Index] = (IOCCellRandom01(x, y, z, Seed) < FillProbability) ? 1 : 0;
        });

        // 2. Automata Smoothing
        GridB.SetNumZeroed(NumVoxels);
        TArray<uint8>* ReadGrid = &GridA;
        TArray<uint8>* WriteGrid = &GridB;

        for (int32 i = 0; i < Settings->SmoothingIterations; ++i)
        {
            ParallelFor(NumVoxels, [&](int32 Index)
            {
                int32 z = Index / (SizeX * SizeY);
                int32 rem = Index % (SizeX * SizeY);
                int32 y = rem / SizeX;
                int32 x = rem % SizeX;

                int32 Neighbors = 0;
                for (int32 dz = -1; dz <= 1; ++dz)
                {
                    for (int32 dy = -1; dy <= 1; ++dy)
                    {
                        for (int32 dx = -1; dx <= 1; ++dx)
                        {
                            if (dx == 0 && dy == 0 && dz == 0) continue;
                            int32 nx = x + dx;
                            int32 ny = y + dy;
                            int32 nz = z + dz;

                            if (nx >= 0 && nx < SizeX && ny >= 0 && ny < SizeY && nz >= 0 && nz < SizeZ)
                            {
                                if ((*ReadGrid)[GetIndex(nx, ny, nz, SizeX, SizeY)] == 1) Neighbors++;
                            }
                            // Out-of-bounds neighbors are treated as empty (air), not filled.
                        }
                    }
                }
                (*WriteGrid)[Index] = (Neighbors > 13) ? 1 : 0;
            });
            Swap(ReadGrid, WriteGrid);
        }
        
        // Ensure GridA holds final result
        if (ReadGrid != &GridA) GridA = GridB; 
    }
    else if (Settings->GenerationMode == EIOCGenerationMode::PerlinTunnel)
    {
        // Perlin Tunnel Logic
        FVector Origin = Bounds.Min;
        FVector TStart = Settings->TunnelStart;
        FVector TEnd = Settings->TunnelEnd;
        float TRadius = Settings->TunnelRadius;
        float TWall = Settings->WallThickness;
        float NFreq = Settings->NoiseFrequency;
        
        // Helper
        auto GetDistanceToSegment = [](const FVector& P, const FVector& A, const FVector& B, bool& bOutOfRange) -> float
        {
            FVector BA = B - A;
            float L2 = BA.SizeSquared();
            if (L2 == 0.0f)
            {
                bOutOfRange = true;
                return FVector::Dist(P, A);
            }

            float TRaw = FVector::DotProduct(P - A, BA) / L2;
            bOutOfRange = (TRaw < 0.0f || TRaw > 1.0f);
            float T = FMath::Clamp(TRaw, 0.0f, 1.0f);
            FVector Projection = A + T * BA;
            return FVector::Dist(P, Projection);
        };

        ParallelFor(NumVoxels, [&](int32 Index)
        {
            int32 z = Index / (SizeX * SizeY);
            int32 rem = Index % (SizeX * SizeY);
            int32 y = rem / SizeX;
            int32 x = rem % SizeX;

            FVector WorldPos = Origin + FVector(x, y, z) * SafeVoxelSize;
            float Noise = FMath::PerlinNoise3D(WorldPos * NFreq);
            bool bOutOfRange = false;
            float Dist = GetDistanceToSegment(WorldPos, TStart, TEnd, bOutOfRange);

            if (bOutOfRange)
            {
                GridA[Index] = 0;
                return;
            }
            
            float OrganicRadius = TRadius + (Noise * TRadius * 0.5f);
            float InnerRadius = OrganicRadius - TWall;

            bool bIsWall = (Dist < OrganicRadius) && (Dist > InnerRadius);
            GridA[Index] = bIsWall ? 1 : 0;
        });
    }

    else if (Settings->GenerationMode == EIOCGenerationMode::InfiniteCellularAutomata)
    {
        // Perlin-seeded initial fill — deterministic at any world coordinate.
        // Smoothing runs on a halo-padded grid so chunk borders use the same
        // neighbor data as adjacent chunks instead of treating OOB cells as air.
        const int32 Iterations = FMath::Max(0, Settings->SmoothingIterations);
        const int32 Halo = Iterations;
        const int32 ExtSizeX = SizeX + Halo * 2;
        const int32 ExtSizeY = SizeY + Halo * 2;
        const int32 ExtSizeZ = SizeZ + Halo * 2;
        const int64 ExtendedTotal = (int64)ExtSizeX * (int64)ExtSizeY * (int64)ExtSizeZ;
        if (ExtendedTotal > VoxelBudget)
        {
            PCGLog::LogErrorOnGraph(LOCTEXT("IOCInfiniteError", "Infinite CA volume plus smoothing halo is too large. Reduce bounds, smoothing, or increase VoxelSize."), Context);
            return false;
        }

        TArray<uint8> ExtGridA;
        TArray<uint8> ExtGridB;
        ExtGridA.SetNumUninitialized((int32)ExtendedTotal);
        if (Iterations > 0)
        {
            ExtGridB.SetNumUninitialized((int32)ExtendedTotal);
        }

        const float InfFreq = FMath::Max(Settings->NoiseFrequency * 0.5f, 0.00001f);
        const float FillThreshold = Settings->FillProbability * 2.0f - 1.0f; // remap [0,1] to [-1,1]
        const FVector WO = Bounds.Min + Settings->WorldOriginOffset;

        auto ExtIndex = [ExtSizeX, ExtSizeY](int32 X, int32 Y, int32 Z)
        {
            return X + (Y * ExtSizeX) + (Z * ExtSizeX * ExtSizeY);
        };

        auto SampleInfiniteSeed = [WO, SafeVoxelSize, InfFreq, FillThreshold](int32 X, int32 Y, int32 Z)
        {
            const FVector WorldPos = WO + FVector(X, Y, Z) * SafeVoxelSize;
            const float N = FMath::PerlinNoise3D(WorldPos * InfFreq);
            return (N < FillThreshold) ? (uint8)1 : (uint8)0;
        };

        ParallelFor((int32)ExtendedTotal, [&](int32 Index)
        {
            const int32 z = Index / (ExtSizeX * ExtSizeY);
            const int32 rem = Index % (ExtSizeX * ExtSizeY);
            const int32 y = rem / ExtSizeX;
            const int32 x = rem % ExtSizeX;
            ExtGridA[Index] = SampleInfiniteSeed(x - Halo, y - Halo, z - Halo);
        });

        TArray<uint8>* ReadGrid = &ExtGridA;
        TArray<uint8>* WriteGrid = &ExtGridB;

        for (int32 i = 0; i < Iterations; ++i)
        {
            ParallelFor((int32)ExtendedTotal, [&](int32 Index)
            {
                const int32 z = Index / (ExtSizeX * ExtSizeY);
                const int32 rem = Index % (ExtSizeX * ExtSizeY);
                const int32 y = rem / ExtSizeX;
                const int32 x = rem % ExtSizeX;

                int32 Neighbors = 0;
                for (int32 dz = -1; dz <= 1; ++dz)
                    for (int32 dy = -1; dy <= 1; ++dy)
                        for (int32 dx = -1; dx <= 1; ++dx)
                        {
                            if (dx == 0 && dy == 0 && dz == 0) continue;
                            const int32 nx = x + dx;
                            const int32 ny = y + dy;
                            const int32 nz = z + dz;
                            if (nx >= 0 && nx < ExtSizeX && ny >= 0 && ny < ExtSizeY && nz >= 0 && nz < ExtSizeZ)
                            {
                                if ((*ReadGrid)[ExtIndex(nx, ny, nz)] == 1) Neighbors++;
                            }
                            else if (SampleInfiniteSeed(nx - Halo, ny - Halo, nz - Halo) == 1)
                            {
                                Neighbors++;
                            }
                        }
                (*WriteGrid)[Index] = (Neighbors > 13) ? 1 : 0;
            });
            Swap(ReadGrid, WriteGrid);
        }

        ParallelFor(NumVoxels, [&](int32 Index)
        {
            const int32 z = Index / (SizeX * SizeY);
            const int32 rem = Index % (SizeX * SizeY);
            const int32 y = rem / SizeX;
            const int32 x = rem % SizeX;
            GridA[Index] = (*ReadGrid)[ExtIndex(x + Halo, y + Halo, z + Halo)];
        });
    }

    // --- Output conversion -------------------------------------------------------------
    //
    // Two passes. The first finds solid voxels in parallel and keeps per-thread index lists;
    // the second writes them out. Splitting it this way gives an exact point count up front,
    // which the 5.6+ SoA container needs before it can allocate, and lets both back-ends
    // write their output in parallel without any per-point reallocation.
    const int32 NumThreads = FMath::Max(1, FPlatformMisc::NumberOfCoresIncludingHyperthreads());
    const int32 ChunkSize = (NumVoxels + NumThreads - 1) / NumThreads; // Ceil div

    TArray<TArray<int32>> ThreadSolidIndices;
    ThreadSolidIndices.SetNum(NumThreads);

    ParallelFor(NumThreads, [&](int32 ThreadIdx)
    {
        const int32 StartIndex = ThreadIdx * ChunkSize;
        const int32 EndIndex = FMath::Min(StartIndex + ChunkSize, NumVoxels);

        TArray<int32>& LocalIndices = ThreadSolidIndices[ThreadIdx];
        LocalIndices.Reserve((EndIndex - StartIndex) / 5); // heuristic: ~20% fill

        for (int32 Index = StartIndex; Index < EndIndex; ++Index)
        {
            if (GridA[Index] == 1)
            {
                LocalIndices.Add(Index);
            }
        }
    });

    TArray<int32> ThreadWriteOffsets;
    ThreadWriteOffsets.SetNumUninitialized(NumThreads);
    int32 TotalPoints = 0;
    for (int32 ThreadIdx = 0; ThreadIdx < NumThreads; ++ThreadIdx)
    {
        ThreadWriteOffsets[ThreadIdx] = TotalPoints;
        TotalPoints += ThreadSolidIndices[ThreadIdx].Num();
    }

    const FVector Origin = Bounds.Min;
    const float HalfVoxel = SafeVoxelSize * 0.5f;

    auto VoxelLocation = [Origin, SafeVoxelSize, SizeX, SizeY](int32 Index)
    {
        const int32 z = Index / (SizeX * SizeY);
        const int32 rem = Index % (SizeX * SizeY);
        const int32 y = rem / SizeX;
        const int32 x = rem % SizeX;
        return Origin + FVector(x, y, z) * SafeVoxelSize;
    };

    // PCG elements execute off the game thread (IPCGElement::CanExecuteOnlyOnMainThread
    // defaults to false), so UObject allocation must go through the context on both paths:
    // it takes a GC scope guard and registers the object so PCG keeps it alive.
#if UE_VERSION_OLDER_THAN(5, 6, 0)
    UPCGPointData* OutputData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
    if (!OutputData)
    {
        PCGLog::LogErrorOnGraph(
            LOCTEXT("IOCAllocError", "IOC Voxel Core: failed to allocate output point data."), Context);
        return false;
    }

    OutputData->InitializeFromData(InputSpatialData);

    TArray<FPCGPoint>& OutputPoints = OutputData->GetMutablePoints();
    OutputPoints.SetNum(TotalPoints);

    ParallelFor(NumThreads, [&](int32 ThreadIdx)
    {
        int32 WriteIndex = ThreadWriteOffsets[ThreadIdx];
        for (const int32 VoxelIndex : ThreadSolidIndices[ThreadIdx])
        {
            FPCGPoint& Point = OutputPoints[WriteIndex++];
            Point.Transform = FTransform(VoxelLocation(VoxelIndex));
            Point.Density = 1.0f;
            Point.BoundsMin = FVector(-HalfVoxel);
            Point.BoundsMax = FVector(HalfVoxel);
        }
    });
#else
    // NewPointData_AnyThread also honours the CVar that selects the concrete point container,
    // so this picks up UPCGPointArrayData where a project has enabled it.
    UPCGBasePointData* OutputData = FPCGContext::NewPointData_AnyThread(Context);
    if (!OutputData)
    {
        PCGLog::LogErrorOnGraph(
            LOCTEXT("IOCAllocError", "IOC Voxel Core: failed to allocate output point data."), Context);
        return false;
    }

    OutputData->InitializeFromData(InputSpatialData);

    // Sizing and allocation invalidate value ranges, so both happen before any range is taken.
    OutputData->SetNumPoints(TotalPoints, /*bInitializeValues=*/false);
    OutputData->AllocateProperties(
        EPCGPointNativeProperties::Transform |
        EPCGPointNativeProperties::Density |
        EPCGPointNativeProperties::BoundsMin |
        EPCGPointNativeProperties::BoundsMax);

    auto TransformRange = OutputData->GetTransformValueRange();
    auto DensityRange = OutputData->GetDensityValueRange();
    auto BoundsMinRange = OutputData->GetBoundsMinValueRange();
    auto BoundsMaxRange = OutputData->GetBoundsMaxValueRange();

    ParallelFor(NumThreads, [&](int32 ThreadIdx)
    {
        int32 WriteIndex = ThreadWriteOffsets[ThreadIdx];
        for (const int32 VoxelIndex : ThreadSolidIndices[ThreadIdx])
        {
            TransformRange[WriteIndex] = FTransform(VoxelLocation(VoxelIndex));
            DensityRange[WriteIndex] = 1.0f;
            BoundsMinRange[WriteIndex] = FVector(-HalfVoxel);
            BoundsMaxRange[WriteIndex] = FVector(HalfVoxel);
            ++WriteIndex;
        }
    });
#endif

    FPCGTaggedData& ResultData = Context->OutputData.TaggedData.Emplace_GetRef();
    ResultData.Data = OutputData;
    ResultData.Pin = PCGPinConstants::DefaultOutputLabel;

    return true;
}

#undef LOCTEXT_NAMESPACE
