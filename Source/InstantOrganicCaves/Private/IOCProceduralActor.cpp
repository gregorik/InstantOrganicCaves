// Copyright (c) 2026 GregOrigin. All Rights Reserved.


#include "IOCProceduralActor.h"

// --- CORE API ---
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "UDynamicMesh.h"
#include "Math/RandomStream.h"
#include "GenericPlatform/GenericPlatformMath.h" // For Perlin

#include "UObject/NoExportTypes.h"
#include "PhysicsEngine/BodySetup.h" 

using namespace UE::Geometry;

AIOCProceduralActor::AIOCProceduralActor()
{
    PrimaryActorTick.bCanEverTick = false;

    MeshComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("ProceduralMesh"));
    RootComponent = MeshComponent;

    MeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
    MeshComponent->SetComplexAsSimpleCollisionEnabled(true, true);
}

void AIOCProceduralActor::BeginPlay()
{
    Super::BeginPlay();
    GenerateCave();
}

void AIOCProceduralActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    GenerateCave();
}

void AIOCProceduralActor::GenerateCave()
{
    if (!MeshComponent || !MeshComponent->GetDynamicMesh()) return;

    MeshComponent->GetDynamicMesh()->EditMesh([&](FDynamicMesh3& Mesh)
        {
            Mesh.Clear();
            Mesh.EnableAttributes();

            // 1. Setup Grid
            int32 GridDim = FMath::Clamp(GridSize, 10, 200);
            int32 TotalVoxels = GridDim * GridDim * GridDim;
            if (TotalVoxels > 8000000) return;

            TArray<bool> Voxels;
            Voxels.Init(false, TotalVoxels);

            // seed offset
            FVector Offset = GetActorLocation() * 0.1f;

            // 2. PERLIN NOISE GENERATION (The "Organic" Look)
            // This replaces the broken random static with smooth, continuous tunnels.
            for (int32 Z = 0; Z < GridDim; ++Z)
            {
                for (int32 Y = 0; Y < GridDim; ++Y)
                {
                    for (int32 X = 0; X < GridDim; ++X)
                    {
                        // Sample Perlin Noise at this coordinate
                        // Ideally frequency should be roughly 0.05 - 0.15
                        float Noise = FMath::PerlinNoise3D(FVector(X, Y, Z) * NoiseFrequency + Offset);

                        // Perlin output is -1 to 1. Threshold > 0 gives roughly 50% fill.
                        int32 Idx = X + Y * GridDim + Z * GridDim * GridDim;
                        Voxels[Idx] = (Noise > (NoiseThreshold - 0.5f));
                    }
                }
            }

            // 3. GEOMETRY GENERATION (WELDED QUADS)
            int32 CornerDim = GridDim + 1;
            TMap<int32, int32> VertexCache;

            double Off = (GridDim * VoxelSize) * 0.5;
            double HS = VoxelSize * 0.5; // Half Size

            // Cached Vertex Creator (Logic: create vertices at corners of voxels)
            auto GetVert = [&](int32 cx, int32 cy, int32 cz) -> int32
                {
                    int32 Key = cx + (cy * CornerDim) + (cz * CornerDim * CornerDim);
                    if (VertexCache.Contains(Key)) return VertexCache[Key];

                    FVector3d Pos(cx * VoxelSize - Off - HS, cy * VoxelSize - Off - HS, cz * VoxelSize - Off - HS);
                    int32 ID = Mesh.AppendVertex(Pos);
                    VertexCache.Add(Key, ID);
                    return ID;
                };

            // Standard Quad (2 Triangles)
            auto AddFace = [&](int32 a, int32 b, int32 c, int32 d)
                {
                    Mesh.AppendTriangle(a, b, c);
                    Mesh.AppendTriangle(c, d, a);
                };

            auto IsAir = [&](int32 x, int32 y, int32 z) {
                if (x < 0 || x >= GridDim || y < 0 || y >= GridDim || z < 0 || z >= GridDim) return false; // Bounds are AIR
                return !Voxels[x + y * GridDim + z * GridDim * GridDim];
                };

            for (int32 Z = 0; Z < GridDim; ++Z)
            {
                for (int32 Y = 0; Y < GridDim; ++Y)
                {
                    for (int32 X = 0; X < GridDim; ++X)
                    {
                        if (!Voxels[X + Y * GridDim + Z * GridDim * GridDim]) continue;

                        // Face Culling: Only draw if touching Air
                        // Correct Winding Order for Cube Faces
                        if (IsAir(X - 1, Y, Z)) AddFace(GetVert(X, Y, Z), GetVert(X, Y + 1, Z), GetVert(X, Y + 1, Z + 1), GetVert(X, Y, Z + 1)); // -X
                        if (IsAir(X + 1, Y, Z)) AddFace(GetVert(X + 1, Y + 1, Z), GetVert(X + 1, Y, Z), GetVert(X + 1, Y, Z + 1), GetVert(X + 1, Y + 1, Z + 1)); // +X

                        if (IsAir(X, Y - 1, Z)) AddFace(GetVert(X + 1, Y, Z), GetVert(X, Y, Z), GetVert(X, Y, Z + 1), GetVert(X + 1, Y, Z + 1)); // -Y
                        if (IsAir(X, Y + 1, Z)) AddFace(GetVert(X, Y + 1, Z), GetVert(X + 1, Y + 1, Z), GetVert(X + 1, Y + 1, Z + 1), GetVert(X, Y + 1, Z + 1)); // +Y

                        if (IsAir(X, Y, Z - 1)) AddFace(GetVert(X, Y, Z), GetVert(X + 1, Y, Z), GetVert(X + 1, Y + 1, Z), GetVert(X, Y + 1, Z)); // -Z
                        if (IsAir(X, Y, Z + 1)) AddFace(GetVert(X, Y + 1, Z + 1), GetVert(X + 1, Y + 1, Z + 1), GetVert(X + 1, Y, Z + 1), GetVert(X, Y, Z + 1)); // +Z
                    }
                }
            }

            // 4. SMOOTHING 
            SmoothMeshInternal(Mesh, SmoothIterations);

            // 5. NORMALS
            FMeshNormals Normals(&Mesh);
            Normals.ComputeVertexNormals();
            Normals.CopyToOverlay(Mesh.Attributes()->PrimaryNormals());

        }, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);
}

void AIOCProceduralActor::SmoothMeshInternal(FDynamicMesh3& Mesh, int32 Iterations)
{
    if (Mesh.VertexCount() == 0) return;
    TArray<FVector3d> OldPos;
    OldPos.SetNum(Mesh.MaxVertexID());

    for (int32 k = 0; k < Iterations; ++k)
    {
        for (int32 vid : Mesh.VertexIndicesItr()) OldPos[vid] = Mesh.GetVertex(vid);

        for (int32 vid : Mesh.VertexIndicesItr())
        {
            FVector3d Sum = FVector3d::Zero();
            int32 Count = 0;

            Mesh.EnumerateVertexVertices(vid, [&](int32 NbrID) {
                Sum += OldPos[NbrID];
                Count++;
                });

            if (Count > 0)
            {
                FVector3d Avg = Sum / (double)Count;
                // Alpha 0.5 is safe for quad grids
                Mesh.SetVertex(vid, FMath::Lerp(OldPos[vid], Avg, 0.5));
            }
        }
    }
}