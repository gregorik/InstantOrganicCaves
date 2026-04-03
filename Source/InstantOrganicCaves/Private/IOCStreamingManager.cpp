// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "IOCStreamingManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

AIOCStreamingManager::AIOCStreamingManager()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AIOCStreamingManager::BeginPlay()
{
    Super::BeginPlay();

    if (bAutoCouplePlayerAtStart)
    {
        TryCouplePlayerToCaveStart();
    }
}

FIntPoint AIOCStreamingManager::WorldToChunk(FVector WorldPos) const
{
    const FVector LocalPos = WorldPos - GetActorLocation();

    return FIntPoint(
        FMath::FloorToInt((LocalPos.X + (ChunkSize.X * 0.5f)) / ChunkSize.X),
        FMath::FloorToInt((LocalPos.Y + (ChunkSize.Y * 0.5f)) / ChunkSize.Y)
    );
}

FVector AIOCStreamingManager::ChunkToWorld(FIntPoint Coord) const
{
    return GetActorLocation() + FVector((float)Coord.X * ChunkSize.X, (float)Coord.Y * ChunkSize.Y, 0.0f);
}

FVector AIOCStreamingManager::GetDesiredPlayerStartLocation(const APawn* Pawn) const
{
    FVector StartLocation = GetActorTransform().TransformPosition(CaveStartOffset);

    float VerticalClearance = FMath::Max((float)VoxelSize, 50.0f);
    if (const ACharacter* Character = Cast<ACharacter>(Pawn))
    {
        if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
        {
            VerticalClearance = Capsule->GetScaledCapsuleHalfHeight() + 2.0f;
        }
    }

    StartLocation.Z += VerticalClearance;
    return StartLocation;
}

void AIOCStreamingManager::UpdateStreamingForLocation(const FVector& PlayerLoc)
{
    const FIntPoint PlayerChunk = WorldToChunk(PlayerLoc);
    if (PlayerChunk == LastPlayerChunk)
    {
        return;
    }

    LastPlayerChunk = PlayerChunk;

    for (int32 dy = -StreamRadius; dy <= StreamRadius; ++dy)
    {
        for (int32 dx = -StreamRadius; dx <= StreamRadius; ++dx)
        {
            const FIntPoint Coord(PlayerChunk.X + dx, PlayerChunk.Y + dy);
            if (!LoadedChunks.Contains(Coord))
            {
                LoadChunk(Coord);
            }
        }
    }

    const float UnloadRadius = (float)StreamRadius * UnloadDistanceBias;
    TArray<FIntPoint> ToUnload;
    for (const TTuple<FIntPoint, AIOCProceduralActor*>& Pair : LoadedChunks)
    {
        const FIntPoint Delta = Pair.Key - PlayerChunk;
        const float ChunkDist = FMath::Max(FMath::Abs((float)Delta.X), FMath::Abs((float)Delta.Y));
        if (ChunkDist > UnloadRadius)
        {
            ToUnload.Add(Pair.Key);
        }
    }

    for (const FIntPoint& Coord : ToUnload)
    {
        UnloadChunk(Coord);
    }
}

bool AIOCStreamingManager::TryCouplePlayerToCaveStart()
{
    if (bHasCoupledPlayerAtStart)
    {
        return true;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    APlayerController* PC = World->GetFirstPlayerController();
    APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!Pawn)
    {
        return false;
    }

    CoupledPlayerStartLocation = GetDesiredPlayerStartLocation(Pawn);
    CoupledPlayerHoldLocation = CoupledPlayerStartLocation + FVector(0.0f, 0.0f, PlayerStartHoldHeight);

    const FRotator StartRotation(0.0f, GetActorRotation().Yaw, 0.0f);
    LastPlayerChunk = FIntPoint(INT_MAX, INT_MAX);
    UpdateStreamingForLocation(CoupledPlayerStartLocation);

    Pawn->SetActorLocation(CoupledPlayerHoldLocation, false, nullptr, ETeleportType::TeleportPhysics);
    Pawn->SetActorRotation(StartRotation, ETeleportType::TeleportPhysics);
    if (PC)
    {
        PC->SetControlRotation(StartRotation);
    }

    if (ACharacter* Character = Cast<ACharacter>(Pawn))
    {
        if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
        {
            MoveComp->StopMovementImmediately();
            MoveComp->DisableMovement();
        }
    }

    CoupledPawn = Pawn;
    bHasCoupledPlayerAtStart = true;
    bHoldingPlayerForInitialGround = true;
    return true;
}

void AIOCStreamingManager::UpdateInitialPlayerHold()
{
    if (!bHoldingPlayerForInitialGround || !CoupledPawn.IsValid())
    {
        return;
    }

    APawn* Pawn = CoupledPawn.Get();
    UWorld* World = GetWorld();
    if (!Pawn || !World)
    {
        bHoldingPlayerForInitialGround = false;
        return;
    }

    const FVector TraceStart = CoupledPlayerHoldLocation + FVector(0.0f, 0.0f, 100.0f);
    const FVector TraceEnd = CoupledPlayerStartLocation - FVector(0.0f, 0.0f, GroundProbeDistance);

    FHitResult Hit;
    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(IOCStreamingGroundProbe), false, Pawn);
    const bool bHasGround = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);

    if (!bHasGround)
    {
        Pawn->SetActorLocation(CoupledPlayerHoldLocation, false, nullptr, ETeleportType::TeleportPhysics);

        if (ACharacter* Character = Cast<ACharacter>(Pawn))
        {
            if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
            {
                MoveComp->StopMovementImmediately();
            }
        }
        return;
    }

    FVector ReleaseLocation = CoupledPlayerStartLocation;
    if (const ACharacter* Character = Cast<ACharacter>(Pawn))
    {
        if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
        {
            ReleaseLocation.Z = Hit.ImpactPoint.Z + Capsule->GetScaledCapsuleHalfHeight() + 2.0f;
        }
    }
    else
    {
        ReleaseLocation.Z = Hit.ImpactPoint.Z;
    }

    Pawn->SetActorLocation(ReleaseLocation, false, nullptr, ETeleportType::TeleportPhysics);

    if (ACharacter* Character = Cast<ACharacter>(Pawn))
    {
        if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
        {
            MoveComp->StopMovementImmediately();
            MoveComp->SetMovementMode(MOVE_Walking);
        }
    }

    bHoldingPlayerForInitialGround = false;
}

void AIOCStreamingManager::LoadChunk(FIntPoint Coord)
{
    UWorld* World = GetWorld();
    if (!World) return;

    const FTransform SpawnTransform(FRotator::ZeroRotator, ChunkToWorld(Coord));
    AIOCProceduralActor* Chunk = World->SpawnActorDeferred<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(),
        SpawnTransform,
        nullptr,
        nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn
    );
    if (!Chunk) return;

    // Streaming integration
    Chunk->bUseWorldSpaceNoise = true;
    Chunk->GenerationBounds = ChunkSize;

    // Per-chunk seed variation: hash chunk coordinates into the seed for unique decoration patterns.
    // Noise-based terrain is already unique per-chunk via bUseWorldSpaceNoise.
    Chunk->CaveSeed = BaseSeed ^ (Coord.X * 73856093) ^ (Coord.Y * 19349663);

    // Core generation parameters
    Chunk->VoxelSize = VoxelSize;
    Chunk->NoiseFrequency = NoiseFrequency;
    Chunk->NoiseThreshold = NoiseThreshold;
    Chunk->SmoothIterations = SmoothIterations;

    // Tunnel mode
    Chunk->bGenerateTunnel = bGenerateTunnel;
    Chunk->TunnelRadius = TunnelRadius;
    Chunk->WallThickness = WallThickness;

    // Spectacular features
    Chunk->DomainWarpIntensity = DomainWarpIntensity;
    Chunk->TerraceSteps = TerraceSteps;

    // LOD
    Chunk->bEnableLOD = bEnableLOD;
    Chunk->LODDistance = LODDistance;
    Chunk->LODVoxelSizeMultiplier = LODVoxelSizeMultiplier;

    // Navigation
    Chunk->bAutoRebuildNavMesh = bAutoRebuildNavMesh;

    // Preset
    Chunk->CavePreset = CavePreset;

    // Appearance
    if (SharedMaterial)
    {
        Chunk->CaveMaterial = SharedMaterial;
    }
    Chunk->TextureTiling = TextureTiling;

    // Decorations
    Chunk->DecorationLayers = SharedDecorationLayers;

    Chunk->FinishSpawning(SpawnTransform);

    LoadedChunks.Add(Coord, Chunk);
}

void AIOCStreamingManager::UnloadChunk(FIntPoint Coord)
{
    if (AIOCProceduralActor** Found = LoadedChunks.Find(Coord))
    {
        if (*Found)
        {
            (*Found)->Destroy();
        }
        LoadedChunks.Remove(Coord);
    }
}

void AIOCStreamingManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    UWorld* World = GetWorld();
    if (!World) return;

    if (bAutoCouplePlayerAtStart && !bHasCoupledPlayerAtStart)
    {
        TryCouplePlayerToCaveStart();
    }

    if (bHoldingPlayerForInitialGround)
    {
        UpdateInitialPlayerHold();
    }

    APlayerController* PC = World->GetFirstPlayerController();
    if (!PC || !PC->GetPawn()) return;

    UpdateStreamingForLocation(PC->GetPawn()->GetActorLocation());
}
