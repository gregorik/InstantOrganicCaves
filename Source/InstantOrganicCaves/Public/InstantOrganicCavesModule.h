// Copyright (c) 2026 GregOrigin. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UWorld;

/** All plugin logging goes here so users can filter or silence it independently of LogTemp. */
DECLARE_LOG_CATEGORY_EXTERN(LogIOC, Log, All);

struct FIOCShowcaseOptions
{
    bool bCaptureMode = false;
    bool bShowCaptions = true;
    bool bLoop = true;
};

/**
 * Level-viewport control for the showcase, supplied by the editor module.
 *
 * The showcase itself is runtime code -- it runs in PIE and in packaged builds -- but when it
 * runs in an editor world it wants to drive the level viewport camera. Reaching for
 * FLevelEditorViewportClient directly would force the Runtime module to depend on
 * LevelEditor and UnrealEd, so the dependency is inverted: the editor module binds these on
 * startup and the runtime module calls them only if bound.
 */
struct FIOCShowcaseViewportHooks
{
    /** Remember the current view and force realtime so the flythrough animates. */
    TFunction<void()> CaptureState;
    /** Put the view and realtime flag back the way the user had them. */
    TFunction<void()> RestoreState;
    /** Point the level viewport at a showcase camera pose. */
    TFunction<void(const FVector& /*Location*/, const FRotator& /*Rotation*/)> ApplyView;
    /** Redraw editor viewports after an off-thread generation lands. */
    TFunction<void()> RedrawViewports;

    bool IsBound() const { return (bool)CaptureState && (bool)RestoreState && (bool)ApplyView; }
};

/** Process-wide hook table. Bound by FInstantOrganicCavesEditorModule, empty otherwise. */
INSTANTORGANICCAVES_API FIOCShowcaseViewportHooks& IOCGetShowcaseViewportHooks();

class INSTANTORGANICCAVES_API FInstantOrganicCavesModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

    static bool SpawnTunnelDemo(UWorld* World = nullptr);
    static bool SpawnSpectacularDemo(UWorld* World = nullptr);
    static bool SpawnShowcase(const FIOCShowcaseOptions& Options = FIOCShowcaseOptions(), UWorld* World = nullptr);
    static bool ClearShowcase(UWorld* World = nullptr);

    /** Removes every actor the demo commands spawned -- showcase, tunnel demo, spectacular
     *  demo and the demo character. ClearShowcase deliberately only clears the showcase, so
     *  this is the one that leaves a user's level as it was found. Returns the count removed. */
    static int32 ClearAllDemos(UWorld* World = nullptr);
};
