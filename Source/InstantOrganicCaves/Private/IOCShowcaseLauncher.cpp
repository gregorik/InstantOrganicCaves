// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "IOCShowcaseLauncher.h"

#include "Components/SceneComponent.h"
#include "InstantOrganicCavesModule.h"
#include "Engine/World.h"
#include "TimerManager.h"

AIOCShowcaseLauncher::AIOCShowcaseLauncher()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AIOCShowcaseLauncher::BeginPlay()
{
    Super::BeginPlay();

#if !UE_BUILD_SHIPPING
    if (!bAutoStart)
    {
        return;
    }

    if (StartDelay <= 0.0f)
    {
        StartShowcase();
        return;
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().SetTimer(
            AutoStartTimerHandle,
            this,
            &AIOCShowcaseLauncher::DeferredStartShowcase,
            StartDelay,
            false);
    }
#endif
}

void AIOCShowcaseLauncher::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(AutoStartTimerHandle);
    }

    ClearShowcase();
    Super::EndPlay(EndPlayReason);
}

void AIOCShowcaseLauncher::DeferredStartShowcase()
{
    StartShowcase();
}

void AIOCShowcaseLauncher::StartShowcase()
{
#if UE_BUILD_SHIPPING
    return;
#else
    FIOCShowcaseOptions Options;
    Options.bCaptureMode = bCaptureMode;
    Options.bShowCaptions = bShowCaptions;
    Options.bLoop = bLoopShowcase;
    FInstantOrganicCavesModule::SpawnShowcase(Options, GetWorld());
#endif
}

void AIOCShowcaseLauncher::StartStandardShowcase()
{
    bCaptureMode = false;
    StartShowcase();
}

void AIOCShowcaseLauncher::StartCaptureShowcase()
{
    bCaptureMode = true;
    StartShowcase();
}

void AIOCShowcaseLauncher::ClearShowcase()
{
#if !UE_BUILD_SHIPPING
    FInstantOrganicCavesModule::ClearShowcase(GetWorld());
#endif
}
