// Copyright (c) 2026 GregOrigin. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/DynamicMeshComponent.h"

namespace UE { namespace Geometry { class FDynamicMesh3; } }

#include "IOCProceduralActor.generated.h"

UCLASS()
class INSTANTORGANICCAVES_API AIOCProceduralActor : public AActor
{
    GENERATED_BODY()

public:
    AIOCProceduralActor();

    UPROPERTY(EditAnywhere, Category = "IOC Generation")
    int32 GridSize = 60; // Increased for better resolution

    UPROPERTY(EditAnywhere, Category = "IOC Generation")
    double VoxelSize = 100.0;

    UPROPERTY(EditAnywhere, Category = "IOC Generation")
    float NoiseThreshold = 0.5f; // Determines wall thickness

    UPROPERTY(EditAnywhere, Category = "IOC Generation")
    float NoiseFrequency = 0.1f; // Controls cave size (Smaller = Bigger caves)

    UPROPERTY(EditAnywhere, Category = "IOC Generation")
    int32 SmoothIterations = 3;

    UPROPERTY(VisibleAnywhere, Category = "Components")
    UDynamicMeshComponent* MeshComponent;

protected:
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    virtual void BeginPlay() override;

private:
    void GenerateCave();
    void SmoothMeshInternal(UE::Geometry::FDynamicMesh3& Mesh, int32 Iterations);
};