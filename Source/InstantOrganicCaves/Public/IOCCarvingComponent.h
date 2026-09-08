// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "IOCCarvingComponent.generated.h"

UENUM(BlueprintType)
enum class EIOCCarvingShape : uint8
{
    Sphere  UMETA(DisplayName = "Sphere"),
    Box     UMETA(DisplayName = "Box"),
    Capsule UMETA(DisplayName = "Capsule")
};

/**
 * Serializable, replicable snapshot of a carving volume.
 *
 * The generator only reads copies of this struct on worker threads. Keeping the
 * runtime carve representation as reflected data also makes authoritative carve
 * history persistable and suitable for initial/late-join replication.
 */
USTRUCT(BlueprintType)
struct INSTANTORGANICCAVES_API FIOCCarvingCapture
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "IOC|Carving")
    EIOCCarvingShape ShapeType = EIOCCarvingShape::Sphere;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "IOC|Carving")
    FTransform WorldTransform = FTransform::Identity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "IOC|Carving", meta=(ClampMin="1.0"))
    float SphereRadius = 200.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "IOC|Carving")
    FVector BoxExtent = FVector(200.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "IOC|Carving", meta=(ClampMin="1.0"))
    float CapsuleRadius = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "IOC|Carving", meta=(ClampMin="1.0"))
    float CapsuleHalfHeight = 200.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "IOC|Carving", meta=(ClampMin="0.0"))
    float FalloffRadius = 50.f;
};

/**
 * One entry in a replicated carve history.
 *
 * Wrapping FIOCCarvingCapture in a FFastArraySerializerItem is what allows the history to
 * replicate as a delta. A plain TArray<FIOCCarvingCapture> resends the whole array whenever
 * any element changes, so one carve against a 256-entry history pushed roughly 25 KB to every
 * relevant client, every time.
 */
USTRUCT(BlueprintType)
struct INSTANTORGANICCAVES_API FIOCCarveHistoryItem : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "IOC|Carving")
    FIOCCarvingCapture Carve;
};

/**
 * Delta-replicated carve history.
 *
 * Only PostReplicatedReceive is implemented, rather than the per-operation add/change/remove
 * hooks: the cave's response to any change is the same (its geometry is stale), and this hook
 * fires exactly once per received update instead of once per affected element.
 *
 * Owner is deliberately NotReplicated. The client fills it in from the actor that owns this
 * struct; replicating a pointer back to that actor would be circular.
 */
USTRUCT(BlueprintType)
struct INSTANTORGANICCAVES_API FIOCCarveHistory : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, SaveGame, Category = "IOC|Carving")
    TArray<FIOCCarveHistoryItem> Items;

    UPROPERTY(NotReplicated)
    TObjectPtr<class AIOCProceduralActor> Owner = nullptr;

    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FIOCCarveHistoryItem, FIOCCarveHistory>(
            Items, DeltaParms, *this);
    }

    void PostReplicatedReceive(const FFastArraySerializer::FPostReplicatedReceiveParameters&);

    // --- Read helpers, so calling code reads much like the old TArray ---
    int32 Num() const { return Items.Num(); }
    bool IsEmpty() const { return Items.IsEmpty(); }

    /** Flattens to plain captures. The generator only ever reads copies on worker threads. */
    void AppendCapturesTo(TArray<FIOCCarvingCapture>& Out) const
    {
        Out.Reserve(Out.Num() + Items.Num());
        for (const FIOCCarveHistoryItem& Item : Items)
        {
            Out.Add(Item.Carve);
        }
    }
};

template<>
struct TStructOpsTypeTraits<FIOCCarveHistory> : public TStructOpsTypeTraitsBase2<FIOCCarveHistory>
{
    enum { WithNetDeltaSerializer = true };
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
