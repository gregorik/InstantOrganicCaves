// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "IOCStreamingManager.h"
#include "InstantOrganicCavesModule.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

AIOCStreamingManager::AIOCStreamingManager()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = StreamingUpdateInterval;
    bReplicates = true;
    SetReplicateMovement(false);
}

void AIOCStreamingManager::BeginPlay()
{
    Super::BeginPlay();

    PrimaryActorTick.TickInterval = FMath::Clamp(StreamingUpdateInterval, 0.05f, 5.0f);

    if (GetWorld() && GetWorld()->IsGameWorld() && !HasAuthority())
    {
        SetActorTickEnabled(false);
        return;
    }

    if (bAutoCouplePlayerAtStart)
    {
        TryCouplePlayerToCaveStart();
    }
}

void AIOCStreamingManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnloadAllChunks();

    Super::EndPlay(EndPlayReason);
}

double AIOCStreamingManager::GetStreamingLODMultiplier() const
{
    return bEnableLOD
        ? (double)FMath::Clamp(FMath::RoundToInt(LODVoxelSizeMultiplier), 1, 16)
        : 1.0;
}

FVector AIOCStreamingManager::GetEffectiveChunkSize() const
{
    const double SafeVoxelSize = FMath::Max(10.0, VoxelSize);
    const int32 CellMultiple = (int32)GetStreamingLODMultiplier();
    auto AlignAxis = [SafeVoxelSize, CellMultiple](double RequestedSize)
    {
        const int32 RequestedCells = FMath::Max(2, FMath::RoundToInt(FMath::Abs(RequestedSize) / SafeVoxelSize));
        const int32 CellGroups = FMath::Max(1, FMath::RoundToInt((double)RequestedCells / (double)CellMultiple));
        const int32 CellCount = FMath::Max(2, CellGroups * CellMultiple);
        return (double)CellCount * SafeVoxelSize;
    };

    return FVector(
        AlignAxis(ChunkSize.X),
        AlignAxis(ChunkSize.Y),
        AlignAxis(ChunkSize.Z));
}

FIntVector AIOCStreamingManager::WorldToChunk(FVector WorldPos) const
{
    const FVector LocalPos = WorldPos - GetActorLocation();
    const FVector AlignedChunkSize = GetEffectiveChunkSize();

    // Z is folded in the same way as X and Y. With VerticalStreamRadius at 0 only layer 0 is
    // ever requested, so this reduces to the original horizontal-only grid.
    return FIntVector(
        FMath::FloorToInt((LocalPos.X + (AlignedChunkSize.X * 0.5)) / AlignedChunkSize.X),
        FMath::FloorToInt((LocalPos.Y + (AlignedChunkSize.Y * 0.5)) / AlignedChunkSize.Y),
        FMath::FloorToInt((LocalPos.Z + (AlignedChunkSize.Z * 0.5)) / AlignedChunkSize.Z)
    );
}

FVector AIOCStreamingManager::ChunkToWorld(FIntVector Coord) const
{
    const FVector AlignedChunkSize = GetEffectiveChunkSize();
    return GetActorLocation() + FVector(
        (double)Coord.X * AlignedChunkSize.X,
        (double)Coord.Y * AlignedChunkSize.Y,
        (double)Coord.Z * AlignedChunkSize.Z);
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

void AIOCStreamingManager::CollectTrackedPlayerLocations(TArray<FVector>& OutLocations) const
{
    OutLocations.Reset();
    const UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PC = It->Get();
        const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
        if (Pawn && !Pawn->IsActorBeingDestroyed())
        {
            OutLocations.Add(Pawn->GetActorLocation());
        }
    }
}

float AIOCStreamingManager::GetMinChunkDistanceSquared(FIntVector Coord, const TArray<FVector>& PlayerLocations) const
{
    const FVector ChunkLocation = ChunkToWorld(Coord);
    float MinDistanceSquared = MAX_flt;
    for (const FVector& PlayerLocation : PlayerLocations)
    {
        // Full 3D distance so load ordering is correct once vertical layers are in play.
        MinDistanceSquared = FMath::Min(
            MinDistanceSquared, (float)FVector::DistSquared(ChunkLocation, PlayerLocation));
    }
    return MinDistanceSquared;
}

void AIOCStreamingManager::UpdateStreamingForLocations(const TArray<FVector>& PlayerLocations)
{
    if (PlayerLocations.IsEmpty())
    {
        return;
    }

    // Chunks destroyed outside UnloadChunk() would otherwise block their coordinate
    // from ever reloading and keep a dead entry against MaxLoadedChunks.
    PruneInvalidChunks();

    TMap<FIntVector, float> CandidateDistances;
    const int32 SafeStreamRadius = FMath::Clamp(StreamRadius, 1, 8);
    const int32 SafeVerticalRadius = FMath::Clamp(VerticalStreamRadius, 0, 4);

    for (const FVector& PlayerLocation : PlayerLocations)
    {
        const FIntVector PlayerChunk = WorldToChunk(PlayerLocation);
        for (int32 dz = -SafeVerticalRadius; dz <= SafeVerticalRadius; ++dz)
        {
            for (int32 dy = -SafeStreamRadius; dy <= SafeStreamRadius; ++dy)
            {
                for (int32 dx = -SafeStreamRadius; dx <= SafeStreamRadius; ++dx)
                {
                    const FIntVector Coord(PlayerChunk.X + dx, PlayerChunk.Y + dy, PlayerChunk.Z + dz);
                    const float DistanceSquared = GetMinChunkDistanceSquared(Coord, PlayerLocations);
                    float& StoredDistance = CandidateDistances.FindOrAdd(Coord, DistanceSquared);
                    StoredDistance = FMath::Min(StoredDistance, DistanceSquared);
                }
            }
        }
    }

    TArray<TPair<FIntVector, float>> SortedCandidates;
    SortedCandidates.Reserve(CandidateDistances.Num());
    for (const TPair<FIntVector, float>& Pair : CandidateDistances)
    {
        SortedCandidates.Add(Pair);
    }
    SortedCandidates.Sort([](const TPair<FIntVector, float>& A, const TPair<FIntVector, float>& B)
    {
        return A.Value < B.Value;
    });

    const int32 SafeMaxLoadedChunks = FMath::Clamp(MaxLoadedChunks, 1, 1024);
    TSet<FIntVector> DesiredCoords;
    for (int32 Index = 0; Index < FMath::Min(SortedCandidates.Num(), SafeMaxLoadedChunks); ++Index)
    {
        DesiredCoords.Add(SortedCandidates[Index].Key);
    }

    PendingLoadCoords.RemoveAll([&](const FIntVector& Coord)
    {
        return !DesiredCoords.Contains(Coord) || LoadedChunks.Contains(Coord);
    });

    for (const TPair<FIntVector, float>& Candidate : SortedCandidates)
    {
        if (DesiredCoords.Contains(Candidate.Key) &&
            !LoadedChunks.Contains(Candidate.Key) &&
            !PendingLoadCoords.Contains(Candidate.Key))
        {
            PendingLoadCoords.Add(Candidate.Key);
        }
    }

    PendingLoadCoords.Sort([&](const FIntVector& A, const FIntVector& B)
    {
        return GetMinChunkDistanceSquared(A, PlayerLocations) < GetMinChunkDistanceSquared(B, PlayerLocations);
    });

    const float UnloadRadius = (float)SafeStreamRadius * FMath::Max(1.0f, UnloadDistanceBias);
    const float VerticalUnloadRadius = (float)SafeVerticalRadius * FMath::Max(1.0f, UnloadDistanceBias);
    TArray<FIntVector> ToUnload;
    for (const TTuple<FIntVector, TWeakObjectPtr<AIOCProceduralActor>>& Pair : LoadedChunks)
    {
        bool bWithinUnloadRadius = false;
        for (const FVector& PlayerLocation : PlayerLocations)
        {
            const FIntVector Delta = Pair.Key - WorldToChunk(PlayerLocation);
            const float HorizontalDistance = FMath::Max(FMath::Abs((float)Delta.X), FMath::Abs((float)Delta.Y));
            const float VerticalDistance = FMath::Abs((float)Delta.Z);
            if (HorizontalDistance <= UnloadRadius && VerticalDistance <= VerticalUnloadRadius)
            {
                bWithinUnloadRadius = true;
                break;
            }
        }

        if (!bWithinUnloadRadius || (!DesiredCoords.Contains(Pair.Key) && LoadedChunks.Num() >= SafeMaxLoadedChunks))
        {
            ToUnload.Add(Pair.Key);
        }
    }

    for (const FIntVector& Coord : ToUnload)
    {
        UnloadChunk(Coord);
    }
}

void AIOCStreamingManager::ProcessPendingLoads()
{
    const int32 LoadBudget = FMath::Clamp(MaxChunkLoadsPerUpdate, 1, 32);
    int32 LoadedThisUpdate = 0;

    while (!PendingLoadCoords.IsEmpty() && LoadedThisUpdate < LoadBudget && LoadedChunks.Num() < FMath::Max(1, MaxLoadedChunks))
    {
        const FIntVector Coord = PendingLoadCoords[0];
        PendingLoadCoords.RemoveAt(0, 1, EAllowShrinking::No);
        if (!LoadedChunks.Contains(Coord))
        {
            LoadChunk(Coord);
            ++LoadedThisUpdate;
        }
    }
}

void AIOCStreamingManager::RebuildAroundLocation(FVector WorldLocation)
{
    if (GetWorld() && GetWorld()->IsGameWorld() && !HasAuthority())
    {
        return;
    }

    const TArray<FVector> Locations{ WorldLocation };
    UpdateStreamingForLocations(Locations);
    ProcessPendingLoads();
}

bool AIOCStreamingManager::DoesCarveAffectChunk(
    const FIOCCarvingCapture& Carve,
    const AIOCProceduralActor* Chunk) const
{
    if (!Chunk)
    {
        return false;
    }

    const FVector HalfExtent = GetEffectiveChunkSize() * 0.5f;
    const FBox ChunkBounds = FBox::BuildAABB(Chunk->GetActorLocation(), HalfExtent);
    const float InfluenceRadius = FMath::Max(0.0f, Carve.SphereRadius) + FMath::Max(0.0f, Carve.FalloffRadius);
    return ChunkBounds.ComputeSquaredDistanceToPoint(Carve.WorldTransform.GetLocation()) <=
        FMath::Square(InfluenceRadius);
}

void AIOCStreamingManager::CarveStreamedCavesAtLocation(FVector WorldLocation, float Radius)
{
    if (!HasAuthority())
    {
        UE_LOG(LogIOC, Warning,
            TEXT("Streamed carve rejected on non-authority manager '%s'."), *GetName());
        return;
    }

    if (WorldLocation.ContainsNaN() || !FMath::IsFinite(Radius) || MaxStreamedRuntimeCarves <= 0)
    {
        UE_LOG(LogIOC, Warning, TEXT("Invalid or disabled streamed carve request on '%s'."), *GetName());
        return;
    }

    FIOCCarvingCapture Carve;
    Carve.ShapeType = EIOCCarvingShape::Sphere;
    Carve.SphereRadius = FMath::Clamp(
        Radius,
        MinStreamedCarveRadius,
        FMath::Max(MinStreamedCarveRadius, MaxStreamedCarveRadius));
    Carve.FalloffRadius = FMath::Max(0.0f, StreamedCarveFalloff);
    Carve.WorldTransform = FTransform(WorldLocation);

    const int32 SafeMaxCarves = FMath::Clamp(MaxStreamedRuntimeCarves, 1, 4096);
    TArray<FIOCCarvingCapture> EvictedCarves;
    if (StreamedRuntimeCarves.Num() >= SafeMaxCarves)
    {
        const int32 RemoveCount = StreamedRuntimeCarves.Num() - SafeMaxCarves + 1;
        EvictedCarves.Append(StreamedRuntimeCarves.GetData(), RemoveCount);
        StreamedRuntimeCarves.RemoveAt(
            0,
            RemoveCount,
            EAllowShrinking::No);
    }
    StreamedRuntimeCarves.Add(Carve);

    PruneInvalidChunks();
    for (const TPair<FIntVector, TWeakObjectPtr<AIOCProceduralActor>>& Pair : LoadedChunks)
    {
        AIOCProceduralActor* Chunk = Pair.Value.Get();
        if (!Chunk)
        {
            continue;
        }

        const bool bNewCarveAffectsChunk = DoesCarveAffectChunk(Carve, Chunk);
        const bool bEvictedCarveAffectedChunk = EvictedCarves.ContainsByPredicate(
            [this, Chunk](const FIOCCarvingCapture& EvictedCarve)
            {
                return DoesCarveAffectChunk(EvictedCarve, Chunk);
            });
        if (!bNewCarveAffectsChunk && !bEvictedCarveAffectedChunk)
        {
            continue;
        }

        Chunk->MaxRuntimeCarves = SafeMaxCarves;
        Chunk->MinRuntimeCarveRadius = MinStreamedCarveRadius;
        Chunk->MaxRuntimeCarveRadius = MaxStreamedCarveRadius;
        // The manager rewrites the chunk's whole retained set, so this is a full-array
        // update by nature; MarkArrayDirty once rather than per item.
        Chunk->RuntimeCarves.Items.Reset();
        for (const FIOCCarvingCapture& RetainedCarve : StreamedRuntimeCarves)
        {
            if (DoesCarveAffectChunk(RetainedCarve, Chunk))
            {
                Chunk->RuntimeCarves.Items.AddDefaulted_GetRef().Carve = RetainedCarve;
            }
        }
        Chunk->RuntimeCarves.MarkArrayDirty();
        Chunk->ForceNetUpdate();
        Chunk->GenerateCave();
    }
}

void AIOCStreamingManager::ClearStreamedRuntimeCarves()
{
    if (!HasAuthority())
    {
        UE_LOG(LogIOC, Warning,
            TEXT("Streamed carve clear rejected on non-authority manager '%s'."), *GetName());
        return;
    }

    if (StreamedRuntimeCarves.IsEmpty())
    {
        return;
    }

    StreamedRuntimeCarves.Reset();
    PruneInvalidChunks();
    for (const TPair<FIntVector, TWeakObjectPtr<AIOCProceduralActor>>& Pair : LoadedChunks)
    {
        if (AIOCProceduralActor* Chunk = Pair.Value.Get())
        {
            Chunk->ClearRuntimeCarves();
        }
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

    if (World->GetNetMode() == NM_DedicatedServer ||
        (World->GetNetMode() != NM_Standalone && !bAllowCouplingInNetworkGames))
    {
        return false;
    }

    APlayerController* PC = World->GetFirstPlayerController();
    APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!Pawn || !PC->IsLocalController())
    {
        return false;
    }

    CoupledPlayerStartLocation = GetDesiredPlayerStartLocation(Pawn);
    CoupledPlayerHoldLocation = CoupledPlayerStartLocation + FVector(0.0f, 0.0f, PlayerStartHoldHeight);

    const FRotator StartRotation(0.0f, GetActorRotation().Yaw, 0.0f);
    const TArray<FVector> InitialLocations{ CoupledPlayerStartLocation };
    UpdateStreamingForLocations(InitialLocations);
    ProcessPendingLoads();

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

void AIOCStreamingManager::LoadChunk(FIntVector Coord)
{
    UWorld* World = GetWorld();
    if (!World) return;
    if (World->IsGameWorld() && !HasAuthority()) return;
    if (LoadedChunks.Contains(Coord)) return;

    const FTransform SpawnTransform(FRotator::ZeroRotator, ChunkToWorld(Coord));

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.bDeferConstruction = true;
    if (!World->IsGameWorld())
    {
        // Editor-world previews are throwaway. Without RF_Transient they would be saved
        // into the user's level and accumulate as orphans on every reload.
        SpawnParams.ObjectFlags |= RF_Transient;
    }

    AIOCProceduralActor* Chunk = World->SpawnActor<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(), SpawnTransform, SpawnParams);
    if (!Chunk) return;

    const FVector AlignedChunkSize = GetEffectiveChunkSize();

    // Streaming integration
    Chunk->bUseWorldSpaceNoise = true;
    Chunk->GenerationBounds = AlignedChunkSize;
    Chunk->bUseFixedBoundsForTunnel = true;

    // Every chunk shares the global density seed so halo samples match at
    // boundaries. The cave actor folds its world grid coordinate into scatter
    // randomization independently, avoiding repeated decoration layouts.
    Chunk->CaveSeed = BaseSeed;

    if (CavePreset != EIOCCavePreset::Custom)
    {
        Chunk->CavePreset = CavePreset;
        Chunk->ApplyPresetSettingsOnly();
        Chunk->CavePreset = EIOCCavePreset::Custom;
    }
    else
    {
        // Core generation parameters
        Chunk->VoxelSize = VoxelSize;
        Chunk->NoiseFrequency = NoiseFrequency;
        Chunk->bGenerateTunnel = bGenerateTunnel;
        Chunk->TunnelRadius = TunnelRadius;
        Chunk->WallThickness = WallThickness;
        Chunk->DomainWarpIntensity = DomainWarpIntensity;
        Chunk->TerraceSteps = TerraceSteps;
    }

    // Streaming owns resolution so chunk spacing and sampling lattices always
    // use the same voxel size, including when a visual preset is selected.
    Chunk->VoxelSize = FMath::Max(10.0, VoxelSize);

    Chunk->NoiseThreshold = NoiseThreshold;
    Chunk->SmoothIterations = SmoothIterations;
    Chunk->MaxVoxelCount = FMath::Clamp(MaxVoxelCountPerChunk, 1000, 15000000);
    Chunk->MaxGeneratedTriangles = FMath::Max(1000, MaxTrianglesPerChunk);
    Chunk->MaxScatterInstances = FMath::Max(0, MaxScatterInstancesPerChunk);
    Chunk->MaxRuntimeCarves = FMath::Clamp(MaxStreamedRuntimeCarves, 1, 4096);
    Chunk->MinRuntimeCarveRadius = MinStreamedCarveRadius;
    Chunk->MaxRuntimeCarveRadius = MaxStreamedCarveRadius;

    // Tunnel mode
    if (Chunk->bGenerateTunnel)
    {
        const float HalfX = FMath::Max(AlignedChunkSize.X * 0.5f, Chunk->TunnelRadius * 2.0f);
        Chunk->TunnelStart = FVector(-HalfX, 0.0f, Chunk->TunnelRadius);
        Chunk->TunnelEnd = FVector(HalfX, 0.0f, Chunk->TunnelRadius);
    }

    // LOD
    Chunk->bEnableLOD = bEnableLOD;
    Chunk->LODDistance = LODDistance;
    Chunk->LODVoxelSizeMultiplier = (float)GetStreamingLODMultiplier();

    // Navigation
    Chunk->bAutoRebuildNavMesh = bAutoRebuildNavMesh;

    // Appearance
    if (SharedMaterial)
    {
        Chunk->CaveMaterial = SharedMaterial;
    }
    Chunk->TextureTiling = TextureTiling;

    // Decorations
    Chunk->DecorationLayers = SharedDecorationLayers;

    for (const FIOCCarvingCapture& Carve : StreamedRuntimeCarves)
    {
        if (DoesCarveAffectChunk(Carve, Chunk))
        {
            Chunk->RuntimeCarves.Items.AddDefaulted_GetRef().Carve = Carve;
        }
    }
    // Seeded before FinishSpawning, so no delta has been sent yet; one array-level mark is
    // enough to establish the initial state.
    Chunk->RuntimeCarves.MarkArrayDirty();

    Chunk->FinishSpawning(SpawnTransform);
    Chunk->ForceNetUpdate();

    if (World && !World->IsPlayInEditor() && !World->IsGameWorld())
    {
        Chunk->GenerateCave();
    }

    LoadedChunks.Add(Coord, Chunk);
}

void AIOCStreamingManager::UnloadChunk(FIntVector Coord)
{
    if (TWeakObjectPtr<AIOCProceduralActor>* Found = LoadedChunks.Find(Coord))
    {
        if (AIOCProceduralActor* Chunk = Found->Get())
        {
            Chunk->Destroy();
        }
        LoadedChunks.Remove(Coord);
    }
    PendingLoadCoords.Remove(Coord);
}

void AIOCStreamingManager::PruneInvalidChunks()
{
    for (auto It = LoadedChunks.CreateIterator(); It; ++It)
    {
        if (!It.Value().IsValid())
        {
            It.RemoveCurrent();
        }
    }
}

void AIOCStreamingManager::UnloadAllChunks()
{
    TArray<FIntVector> Coords;
    LoadedChunks.GenerateKeyArray(Coords);
    for (const FIntVector& Coord : Coords)
    {
        UnloadChunk(Coord);
    }
    PendingLoadCoords.Reset();
}

void AIOCStreamingManager::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    UWorld* World = GetWorld();
    if (!World) return;

    if (World->IsGameWorld() && !HasAuthority())
    {
        return;
    }

    if (bAutoCouplePlayerAtStart && !bHasCoupledPlayerAtStart)
    {
        TryCouplePlayerToCaveStart();
    }

    if (bHoldingPlayerForInitialGround)
    {
        UpdateInitialPlayerHold();
    }

    TArray<FVector> PlayerLocations;
    CollectTrackedPlayerLocations(PlayerLocations);
    if (PlayerLocations.IsEmpty())
    {
        TimeWithoutTrackedPlayers += DeltaTime;
        if (TimeWithoutTrackedPlayers >= FMath::Max(0.0f, NoPlayerUnloadDelay))
        {
            UnloadAllChunks();
        }
        return;
    }

    TimeWithoutTrackedPlayers = 0.0f;

    UpdateStreamingForLocations(PlayerLocations);
    ProcessPendingLoads();
}
