// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "IOCProceduralActor.h"
#include "IOCCharacter.h"
#include "UDynamicMesh.h"
#include "IOCCarvingComponent.h"
#include "DynamicMesh/DynamicMesh3.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCSpawnTunnelTest, "InstantOrganicCaves.Demo.SpawnTunnel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCSpawnTunnelTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    // 1. Run the Console Command
    // We assume GEditor is available since we are in EditorContext
    if (!GEditor) return false;

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        AddError(TEXT("No Editor World found."));
        return false;
    }

    // Clean up any existing demo actors to ensure fresh test
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetActorLabel() == TEXT("IOC_Tunnel_Demo") || It->GetActorLabel() == TEXT("IOC_Player"))
        {
            World->DestroyActor(*It);
        }
    }

    // Execute Command
    GEngine->Exec(World, TEXT("IOC.SpawnTunnelDemo"));

    // 2. Verify Actor Exists
    AIOCProceduralActor* DemoActor = nullptr;
    for (TActorIterator<AIOCProceduralActor> It(World); It; ++It)
    {
        if (It->GetActorLabel() == TEXT("IOC_Tunnel_Demo"))
        {
            DemoActor = *It;
            break;
        }
    }

    if (!DemoActor)
    {
        AddError(TEXT("IOC_Tunnel_Demo actor was not spawned."));
        return false;
    }

    // 3. Verify Properties
    if (!DemoActor->bGenerateTunnel)
    {
        AddError(TEXT("bGenerateTunnel should be true."));
        return false;
    }

    if (!FMath::IsNearlyEqual(DemoActor->TunnelRadius, 450.0f))
    {
        AddError(TEXT("TunnelRadius was not set correctly (expected 450)."));
        return false;
    }

    // 4. Verify Mesh Generation (Async Note)
    AddInfo(TEXT("Mesh generation is Async. Skipping immediate triangle check to avoid race conditions in simple test."));

    // 5. Verify Player Character
    AIOCCharacter* DemoPlayer = nullptr;
    for (TActorIterator<AIOCCharacter> It(World); It; ++It)
    {
        if (It->GetActorLabel() == TEXT("IOC_Player"))
        {
            DemoPlayer = *It;
            break;
        }
    }

    if (!DemoPlayer)
    {
        AddError(TEXT("IOC_Player character was not spawned."));
        return false;
    }

    AddInfo(TEXT("Successfully spawned Tunnel Demo and Player Character (Mesh generation is Async)."));

    return true;
#else
    return true; // Skip in Game builds
#endif
}

// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCCarvingVolumeTest, "InstantOrganicCaves.Carving.SmokeTest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCCarvingVolumeTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor) return false;
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) { AddError(TEXT("No Editor World")); return false; }

    // 1. Spawn a cave actor
    FActorSpawnParameters SP;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AIOCProceduralActor* Cave = World->SpawnActor<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SP);

    if (!Cave) { AddError(TEXT("Failed to spawn AIOCProceduralActor")); return false; }

    // 2. Add a carving component as a child
    UIOCCarvingComponent* Carve = NewObject<UIOCCarvingComponent>(Cave);
    Carve->ShapeType    = EIOCCarvingShape::Sphere;
    Carve->SphereRadius = 300.f;
    Carve->FalloffRadius = 80.f;
    Carve->RegisterComponent();
    Carve->AttachToComponent(Cave->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

    // 3. MakeCapture must return correct data
    FIOCCarvingCapture Cap = Carve->MakeCapture();
    if (Cap.ShapeType != EIOCCarvingShape::Sphere)
    {
        AddError(TEXT("MakeCapture: ShapeType mismatch"));
        World->DestroyActor(Cave);
        return false;
    }
    if (!FMath::IsNearlyEqual(Cap.SphereRadius, 300.f))
    {
        AddError(TEXT("MakeCapture: SphereRadius mismatch"));
        World->DestroyActor(Cave);
        return false;
    }
    if (!FMath::IsNearlyEqual(Cap.FalloffRadius, 80.f))
    {
        AddError(TEXT("MakeCapture: FalloffRadius mismatch"));
        World->DestroyActor(Cave);
        return false;
    }

    // 4. GenerateCave with a carving volume must not crash
    Cave->GenerateCave();

    AddInfo(TEXT("IOCCarvingComponent smoke test passed (mesh generation is async)."));
    World->DestroyActor(Cave);
    return true;
#else
    return true;
#endif
}
