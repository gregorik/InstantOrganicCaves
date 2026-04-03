// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "IOCCarvingComponent.generated.h"

UENUM(BlueprintType)
enum class EIOCCarvingShape : uint8
{
    Sphere  UMETA(DisplayName = "Sphere"),
    Box     UMETA(DisplayName = "Box"),
    Capsule UMETA(DisplayName = "Capsule")
};

/** Thread-safe POD snapshot of a carving volume. Passed by value into the async lambda. */
struct FIOCCarvingCapture
{
    EIOCCarvingShape ShapeType    = EIOCCarvingShape::Sphere;
    FTransform       WorldTransform;
    float            SphereRadius    = 200.f;
    FVector          BoxExtent       = FVector(200.f);
    float            CapsuleRadius   = 100.f;
    float            CapsuleHalfHeight = 200.f;
    float            FalloffRadius   = 50.f;
};

/**
 * Place as a child component on an AIOCProceduralActor.
 * The actor automatically discovers all UIOCCarvingComponents at GenerateCave() time
 * and carves the voxel grid to guarantee open space inside the shape.
 */
UCLASS(ClassGroup=IOC, meta=(BlueprintSpawnableComponent), DisplayName="IOC Carving Volume")
class INSTANTORGANICCAVES_API UIOCCarvingComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UIOCCarvingComponent();

    /** Shape of this carving volume. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Carving")
    EIOCCarvingShape ShapeType = EIOCCarvingShape::Sphere;

    /** Radius for Sphere shape (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Carving", meta=(ClampMin="1.0", EditCondition="ShapeType==EIOCCarvingShape::Sphere"))
    float SphereRadius = 200.f;

    /** Half-extents for Box shape (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Carving", meta=(EditCondition="ShapeType==EIOCCarvingShape::Box"))
    FVector BoxExtent = FVector(200.f, 200.f, 200.f);

    /** Radius for Capsule shape (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Carving", meta=(ClampMin="1.0", EditCondition="ShapeType==EIOCCarvingShape::Capsule"))
    float CapsuleRadius = 100.f;

    /** Half-height of Capsule shaft (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Carving", meta=(ClampMin="1.0", EditCondition="ShapeType==EIOCCarvingShape::Capsule"))
    float CapsuleHalfHeight = 200.f;

    /**
     * Blend zone thickness beyond the hard surface edge (cm).
     * Voxels in this zone are progressively emptied via smoothstep,
     * creating organic cave-wall transitions instead of a sharp geometric cut.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC|Carving", meta=(ClampMin="0.0"))
    float FalloffRadius = 50.f;

    /** Snapshot all properties into a thread-safe POD struct for async use. */
    FIOCCarvingCapture MakeCapture() const;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
