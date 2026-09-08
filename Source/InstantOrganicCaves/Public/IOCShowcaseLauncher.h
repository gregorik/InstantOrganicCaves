// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IOCShowcaseLauncher.generated.h"

UCLASS(Blueprintable, meta=(DisplayName="IOC Showcase Launcher"))
class INSTANTORGANICCAVES_API AIOCShowcaseLauncher : public AActor
{
    GENERATED_BODY()

public:
    AIOCShowcaseLauncher();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC Showcase", meta = (DisplayName = "Start Automatically", ToolTip = "Start the showcase automatically when Play begins."))
    bool bAutoStart = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC Showcase", meta = (DisplayName = "Capture Mode", ToolTip = "Use the capture-polished showcase variant with the cinematic presentation defaults."))
    bool bCaptureMode = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC Showcase", meta = (DisplayName = "Show Captions", ToolTip = "Display the guided caption overlay during the showcase flythrough."))
    bool bShowCaptions = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC Showcase", meta = (DisplayName = "Loop Flythrough", ToolTip = "Loop the camera flythrough continuously. When false, the camera stops at the last section after one pass."))
    bool bLoopShowcase = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "IOC Showcase", meta = (ClampMin = "0.0", DisplayName = "Auto Start Delay", ToolTip = "Delay in seconds before the auto-started showcase begins."))
    float StartDelay = 0.5f;

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "IOC Showcase", meta = (DisplayName = "Start Using Current Settings"))
    void StartShowcase();

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "IOC Showcase", meta = (DisplayName = "Start Standard Showcase"))
    void StartStandardShowcase();

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "IOC Showcase", meta = (DisplayName = "Start Capture Showcase"))
    void StartCaptureShowcase();

    UFUNCTION(BlueprintCallable, CallInEditor, Category = "IOC Showcase", meta = (DisplayName = "Clear Running Showcase"))
    void ClearShowcase();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    FTimerHandle AutoStartTimerHandle;

    void DeferredStartShowcase();
};
