// Copyright (c) 2026 GregOrigin. All Rights Reserved.


#include "Elements/IOCVoxelCore.h"

// Framework Includes
#include "PCGElement.h"          
#include "PCGContext.h"          
#include "Data/PCGPointData.h"   
#include "Data/PCGSpatialData.h" 
#include "Helpers/PCGAsync.h"    

#define LOCTEXT_NAMESPACE "FIOCVoxelCoreElement"

// ==============================================================================
// 1. Worker Definition
// ==============================================================================
class FIOCVoxelCoreElement : public IPCGElement
{
public:
    // We use ExecuteInternal as mandated by UE 5.x architecture
    virtual bool ExecuteInternal(FPCGContext* Context) const override;
};

// ==============================================================================
// 2. Factory Connection
// ==============================================================================
FPCGElementPtr UIOCVoxelCoreSettings::CreateElement() const
{
    return MakeShared<FIOCVoxelCoreElement>();
}

// ==============================================================================
// 3. Logic Implementation
// ==============================================================================

FORCEINLINE int32 GetIndex(int32 X, int32 Y, int32 Z, int32 SizeX, int32 SizeY)
{
    return X + (Y * SizeX) + (Z * SizeX * SizeY);
}

bool FIOCVoxelCoreElement::ExecuteInternal(FPCGContext* Context) const
{
    // --- Settings & Setup ---
    const UIOCVoxelCoreSettings* Settings = Context->GetInputSettings<UIOCVoxelCoreSettings>();
    check(Settings);

    const UPCGSpatialData* InputSpatialData = nullptr;
    TArray<FPCGTaggedData> Inputs = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

    if (Inputs.Num() > 0)
    {
        InputSpatialData = Cast<UPCGSpatialData>(Inputs[0].Data);
    }

    FBox Bounds = InputSpatialData ? InputSpatialData->GetBounds() : FBox(FVector(-1000), FVector(1000));

    // --- Size Calculations ---
    FVector Extent = Bounds.GetSize();
    int32 SizeX = FMath::Max(1, FMath::RoundToInt(Extent.X / Settings->VoxelSize));
    int32 SizeY = FMath::Max(1, FMath::RoundToInt(Extent.Y / Settings->VoxelSize));
    int32 SizeZ = FMath::Max(1, FMath::RoundToInt(Extent.Z / Settings->VoxelSize));
    int64 TotalVoxels = (int64)SizeX * (int64)SizeY * (int64)SizeZ;

    // Safety Cap
    if (TotalVoxels > 10000000)
    {
        PCGLog::LogErrorOnGraph(LOCTEXT("IOCError", "Volume too large!"), Context);
        return true;
    }

    // --- Logic ---
    TArray<uint8> GridA;
    TArray<uint8> GridB;
    GridA.SetNumUninitialized(TotalVoxels);
    GridB.SetNumUninitialized(TotalVoxels);

    int32 Seed = Settings->CaveSeed;

    // 1. Noise
    ParallelFor(TotalVoxels, [&](int32 Index)
        {
            uint32 Hash = (Index * 196314165) + 907633515 + Seed;
            float RandomVal = (float)Hash / (float)UINT32_MAX;
            GridA[Index] = (RandomVal < Settings->FillProbability) ? 1 : 0;
        });

    // 2. Automata
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
                            else { Neighbors++; }
                        }
                    }
                }
                (*WriteGrid)[Index] = (Neighbors > 13) ? 1 : 0;
            });
        Swap(ReadGrid, WriteGrid);
    }

    // --- Output Conversion ---
    UPCGPointData* OutputData = NewObject<UPCGPointData>();
    OutputData->InitializeFromData(InputSpatialData);
    TArray<FPCGPoint>& OutputPoints = OutputData->GetMutablePoints();

    FVector Origin = Bounds.Min;
    for (int32 z = 0; z < SizeZ; ++z)
    {
        for (int32 y = 0; y < SizeY; ++y)
        {
            for (int32 x = 0; x < SizeX; ++x)
            {
                // Note: We output points where the grid is 1 (Rock)
                // You can invert this logic to output hollow space air if preferred.
                if ((*ReadGrid)[GetIndex(x, y, z, SizeX, SizeY)] == 1)
                {
                    FPCGPoint& Point = OutputPoints.Emplace_GetRef();
                    Point.Transform.SetLocation(Origin + FVector(x, y, z) * Settings->VoxelSize);
                    Point.Transform.SetScale3D(FVector(1.0f));
                    Point.Density = 1.0f;
                    Point.BoundsMin = FVector(-Settings->VoxelSize * 0.5f);
                    Point.BoundsMax = FVector(Settings->VoxelSize * 0.5f);
                }
            }
        }
    }

    FPCGTaggedData& ResultData = Context->OutputData.TaggedData.Emplace_GetRef();
    ResultData.Data = OutputData;
    ResultData.Pin = PCGPinConstants::DefaultOutputLabel;

    return true;
}

#undef LOCTEXT_NAMESPACE