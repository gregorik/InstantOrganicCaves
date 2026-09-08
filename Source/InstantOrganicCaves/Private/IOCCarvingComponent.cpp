// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "IOCCarvingComponent.h"
#include "IOCProceduralActor.h"
#include "DrawDebugHelpers.h"

UIOCCarvingComponent::UIOCCarvingComponent()
{
#if WITH_EDITOR
    PrimaryComponentTick.bCanEverTick = true;
#else
    PrimaryComponentTick.bCanEverTick = false;
#endif
}

void FIOCCarveHistory::PostReplicatedReceive(
    const FFastArraySerializer::FPostReplicatedReceiveParameters& /*Parameters*/)
{
    // Add, remove or in-place edit all mean the same thing to the cave: the carve set moved
    // and its geometry is stale. RequestCarveRebuild coalesces bursts.
    if (Owner)
    {
        Owner->RequestCarveRebuild();
    }
}

FIOCCarvingCapture UIOCCarvingComponent::MakeCapture() const
{
    FIOCCarvingCapture C;
    C.ShapeType          = ShapeType;
    C.WorldTransform     = GetComponentTransform();
    C.SphereRadius       = SphereRadius;
    C.BoxExtent          = BoxExtent;
    C.CapsuleRadius      = CapsuleRadius;
    C.CapsuleHalfHeight  = CapsuleHalfHeight;
    C.FalloffRadius      = FMath::Max(0.f, FalloffRadius);
    return C;
}

void UIOCCarvingComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if WITH_EDITORONLY_DATA
    UWorld* World = GetWorld();
    if (!World || World->IsGameWorld()) return;

    const FVector Loc  = GetComponentLocation();
    const FQuat   Rot  = GetComponentQuat();
    const FColor  Inner(255, 140,   0);   // solid orange  = hard carve edge
    const FColor  Outer(255, 200, 100);   // pale orange   = falloff zone

    switch (ShapeType)
    {
    case EIOCCarvingShape::Sphere:
        DrawDebugSphere(World, Loc, SphereRadius,                        16, Inner, false, -1.f, 0, 1.5f);
        DrawDebugSphere(World, Loc, SphereRadius + FalloffRadius,        16, Outer, false, -1.f, 0, 0.5f);
        break;

    case EIOCCarvingShape::Box:
        DrawDebugBox(World, Loc, BoxExtent,                              Rot, Inner, false, -1.f, 0, 1.5f);
        DrawDebugBox(World, Loc, BoxExtent + FVector(FalloffRadius),     Rot, Outer, false, -1.f, 0, 0.5f);
        break;

    case EIOCCarvingShape::Capsule:
        DrawDebugCapsule(World, Loc, CapsuleHalfHeight,                              CapsuleRadius,                Rot, Inner, false, -1.f, 0, 1.5f);
        DrawDebugCapsule(World, Loc, CapsuleHalfHeight + FalloffRadius,  CapsuleRadius + FalloffRadius, Rot, Outer, false, -1.f, 0, 0.5f);
        break;
    }
#endif
}
