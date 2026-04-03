// Copyright (c) 2026 GregOrigin. All Rights Reserved.


#include "Elements/IOCVoxelCore.h"

// Framework Includes
#include "PCGElement.h"          
#include "PCGContext.h"          
#include "Data/PCGPointData.h"   
#include "Data/PCGSpatialData.h" 
#include "Helpers/PCGAsync.h"    
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

    FVector Extent = Bounds.GetSize();
    int32 SizeX = FMath::Max(1, FMath::RoundToInt(Extent.X / Settings->VoxelSize));
    int32 SizeY = FMath::Max(1, FMath::RoundToInt(Extent.Y / Settings->VoxelSize));
    int32 SizeZ = FMath::Max(1, FMath::RoundToInt(Extent.Z / Settings->VoxelSize));
    int64 TotalVoxels = (int64)SizeX * (int64)SizeY * (int64)SizeZ;

    if (TotalVoxels > 15000000)
    {
        PCGLog::LogErrorOnGraph(LOCTEXT("IOCError", "Volume too large! Reduce bounds or increase VoxelSize."), Context);
        return false;
    }

    TArray<uint8> GridA;
    TArray<uint8> GridB;
    GridA.SetNumUninitialized(TotalVoxels);
    
    // --- Logic Selection ---
    
    if (Settings->GenerationMode == EIOCGenerationMode::CellularAutomata)
    {
        // 1. Noise
        int32 Seed = Settings->CaveSeed;
        ParallelFor(TotalVoxels, [&](int32 Index)
        {
            uint32 Hash = (Index * 196314165) + 907633515 + Seed;
            float RandomVal = (float)Hash / (float)UINT32_MAX;
            GridA[Index] = (RandomVal < Settings->FillProbability) ? 1 : 0;
        });

        // 2. Automata Smoothing
        GridB.SetNumUninitialized(TotalVoxels);
        TArray<uint8>* ReadGrid = &GridA;
        TArray<uint8>* WriteGrid = &GridB;

        for (int32 i = 0; i < Settings->SmoothingIterations; ++i)
        {
            ParallelFor(TotalVoxels, [&](int32 Index)
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

        ParallelFor(TotalVoxels, [&](int32 Index)
        {
            int32 z = Index / (SizeX * SizeY);
            int32 rem = Index % (SizeX * SizeY);
            int32 y = rem / SizeX;
            int32 x = rem % SizeX;

            FVector WorldPos = Origin + FVector(x, y, z) * Settings->VoxelSize;
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
        // Perlin-seeded initial fill — deterministic at any world coordinate
        float InfFreq = Settings->NoiseFrequency * 0.5f;
        float FillThreshold = Settings->FillProbability * 2.0f - 1.0f; // remap [0,1] to [-1,1]
        FVector WO = Settings->WorldOriginOffset;

        ParallelFor(TotalVoxels, [&](int32 Index)
        {
            int32 z = Index / (SizeX * SizeY);
            int32 rem = Index % (SizeX * SizeY);
            int32 y = rem / SizeX;
            int32 x = rem % SizeX;
            FVector WorldPos = WO + FVector(x, y, z) * Settings->VoxelSize;
            float N = FMath::PerlinNoise3D(WorldPos * InfFreq);
            GridA[Index] = (N < FillThreshold) ? 1 : 0;
        });

        // CA smoothing (same as CellularAutomata mode, OOB treated as empty)
        GridB.SetNumUninitialized(TotalVoxels);
        TArray<uint8>* ReadGrid = &GridA;
        TArray<uint8>* WriteGrid = &GridB;

        for (int32 i = 0; i < Settings->SmoothingIterations; ++i)
        {
            ParallelFor(TotalVoxels, [&](int32 Index)
            {
                int32 z = Index / (SizeX * SizeY);
                int32 rem = Index % (SizeX * SizeY);
                int32 y = rem / SizeX;
                int32 x = rem % SizeX;

                int32 Neighbors = 0;
                for (int32 dz = -1; dz <= 1; ++dz)
                    for (int32 dy = -1; dy <= 1; ++dy)
                        for (int32 dx = -1; dx <= 1; ++dx)
                        {
                            if (dx == 0 && dy == 0 && dz == 0) continue;
                            int32 nx = x + dx, ny = y + dy, nz = z + dz;
                            if (nx >= 0 && nx < SizeX && ny >= 0 && ny < SizeY && nz >= 0 && nz < SizeZ)
                                if ((*ReadGrid)[GetIndex(nx, ny, nz, SizeX, SizeY)] == 1) Neighbors++;
                        }
                (*WriteGrid)[Index] = (Neighbors > 13) ? 1 : 0;
            });
            Swap(ReadGrid, WriteGrid);
        }

        if (ReadGrid != &GridA) GridA = GridB;
    }

    // --- Output Conversion (Parallelized) ---
    UPCGPointData* OutputData = NewObject<UPCGPointData>();
    OutputData->InitializeFromData(InputSpatialData);
    TArray<FPCGPoint>& OutputPoints = OutputData->GetMutablePoints();

    // Split work into chunks based on available cores
    const int32 NumThreads = FMath::Max(1, FPlatformMisc::NumberOfCoresIncludingHyperthreads());
    const int32 ChunkSize = (TotalVoxels + NumThreads - 1) / NumThreads; // Ceil div

    TArray<TArray<FPCGPoint>> ThreadPoints;
    ThreadPoints.SetNum(NumThreads);

    FVector Origin = Bounds.Min;

    ParallelFor(NumThreads, [&](int32 ThreadIdx)
    {
        int32 StartIndex = ThreadIdx * ChunkSize;
        int32 EndIndex = FMath::Min(StartIndex + ChunkSize, (int32)TotalVoxels);
        
        // Reserve memory to avoid reallocations (heuristic: 20% fill rate)
        ThreadPoints[ThreadIdx].Reserve((EndIndex - StartIndex) / 5);

        for (int32 Index = StartIndex; Index < EndIndex; ++Index)
        {
            if (GridA[Index] == 1)
            {
                int32 z = Index / (SizeX * SizeY);
                int32 rem = Index % (SizeX * SizeY);
                int32 y = rem / SizeX;
                int32 x = rem % SizeX;

                FPCGPoint& Point = ThreadPoints[ThreadIdx].Emplace_GetRef();
                Point.Transform.SetLocation(Origin + FVector(x, y, z) * Settings->VoxelSize);
                Point.Transform.SetScale3D(FVector(1.0f));
                Point.Density = 1.0f;
                Point.BoundsMin = FVector(-Settings->VoxelSize * 0.5f);
                Point.BoundsMax = FVector(Settings->VoxelSize * 0.5f);
            }
        }
    });

    // Merge results
    for (const TArray<FPCGPoint>& LocalPoints : ThreadPoints)
    {
        OutputPoints.Append(LocalPoints);
    }

    FPCGTaggedData& ResultData = Context->OutputData.TaggedData.Emplace_GetRef();
    ResultData.Data = OutputData;
    ResultData.Pin = PCGPinConstants::DefaultOutputLabel;

    return true;
}

#undef LOCTEXT_NAMESPACE
