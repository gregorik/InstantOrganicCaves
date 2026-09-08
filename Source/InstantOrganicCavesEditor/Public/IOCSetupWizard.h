// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/GCObject.h"
#include "IOCProceduralActor.h"

/**
 * Slate tags on wizard widgets, so layout tests can find them by name.
 *
 * The wizard's layout had no automated coverage at all until SetupWizard.Layout, which is
 * how a stretched hero and twelve vertically-stacked button rows both shipped unnoticed --
 * the button sweep proves handlers run, not that anything is arranged correctly.
 */
namespace IOCWizardTags
{
    inline const FName HeroImage(TEXT("IOC.Hero.Image"));

    /**
     * Carried by every SWrapBox the wizard builds.
     *
     * SWrapBox defaults to PreferredSize = 100 with UseAllottedSize = false, so it wraps
     * after roughly one child regardless of the space available -- which is how twelve
     * button rows shipped stacked vertically. The tag and .UseAllottedSize(true) are set
     * together at every site, and SetupWizard.Layout fails on any untagged wrap box, so a
     * bare SNew(SWrapBox) copied in later is caught rather than silently mislaying out.
     */
    inline const FName WrapBox(TEXT("IOC.WrapBox"));
}

class SWindow;
class SWidgetSwitcher;
class FEditorViewportClient;
class AIOCProceduralActor;
class SIOCPreviewViewport;

struct FIOCSetupWizardSettings
{
    int32 SelectedPresetIndex = 0;
    bool bIsCustom = false;

    bool bCustomTunnelMode = false;
    bool bCustomUseSpline = false;
    int32 CustomSeed = 1337;
    FVector CustomBounds = FVector(4000, 4000, 2500);
    FVector CustomTunnelStart = FVector(-2000, 0, 400);
    FVector CustomTunnelEnd = FVector(2000, 0, 400);
    double CustomVoxelSize = 50.0;
    float CustomTunnelRadius = 400.0f;
    float CustomWallThickness = 80.0f;
    float CustomNoiseFrequency = 0.005f;
    int32 CustomSmoothIterations = 3;
    float CustomDomainWarpIntensity = 0.0f;
    float CustomTerraceSteps = 0.0f;
    float CustomTextureTiling = 0.005f;

    bool bGenerateSmartColors = true;
    bool bEnableLOD = true;
    float CustomLODDistance = 5000.0f;
    float CustomLODMultiplier = 3.0f;
    bool bUseWorldSpaceNoise = false;
    bool bUseFixedBoundsForTunnel = false;
    bool bAutoRebuildNavMesh = false;
    bool bShowDebugViz = true;
    bool bPreviewFullFidelity = false;
    bool bAutoFocusConfiguredCave = true;

    bool bAddLighting = true;
    bool bAddPlayer = true;
    bool bReconfigureExisting = false;
    int32 SelectedExistingIndex = -1;
    FString ProfileName = TEXT("My Cave Setup");
};

struct FIOCSetupWizardViewModel
{
    bool bStarterAssetsPrepared = false;
    bool bSetupAttempted = false;
    bool bGenerationDone = false;
    bool bIsGenerating = false;
    bool bValidationPassed = true;
    bool bValidationDirty = true;
    bool bPendingPreviewRefresh = false;
    bool bPendingExistingCaveRefresh = true;
    bool bLastSetupReconfiguredExisting = false;

    double NextPreviewRefreshTime = 0.0;
    double NextValidationRefreshTime = 0.0;
    double NextExistingCaveRefreshTime = 0.0;
    uint32 LastPreviewSettingsSignature = 0;
    uint32 LastValidationSignature = 0;
    uint32 LastExistingCaveSignature = 0;

    FString GenerationSummary;
    FString SetupError;
    FString StarterAssetStatus;
    FString StarterLevelStatus;
    FString InstallationStatus;
    FString ProfileStatus;
    FString PreviewStatus;
    FString GenerationStats;
    FDateTime LastPreviewBuiltAt;

    TWeakObjectPtr<AIOCProceduralActor> LastConfiguredCave;
    TWeakObjectPtr<AIOCProceduralActor> PreviewActor;
    TArray<TWeakObjectPtr<AIOCProceduralActor>> ExistingCaves;
    float GenerationProgress = 0.0f;
};

struct FIOCSetupWizardRollbackSnapshot
{
    bool bValid = false;
    TWeakObjectPtr<AIOCProceduralActor> TargetCave;
    EIOCCavePreset CavePreset = EIOCCavePreset::Custom;
    bool bGenerateTunnel = false;
    bool bUseSpline = false;
    FVector TunnelStart = FVector::ZeroVector;
    FVector TunnelEnd = FVector(1000, 0, 0);
    float TunnelRadius = 300.0f;
    float WallThickness = 60.0f;
    int32 CaveSeed = 1337;
    FVector GenerationBounds = FVector(1000, 1000, 1000);
    double VoxelSize = 50.0;
    float NoiseFrequency = 0.005f;
    float NoiseThreshold = 0.5f;
    int32 SmoothIterations = 3;
    int32 NoiseOctaves = 4;
    float NoiseLacunarity = 2.0f;
    float NoisePersistence = 0.5f;
    float MacroChamberWeight = 0.35f;
    float RidgedDetailWeight = 0.25f;
    float InteriorDensityBias = 0.05f;
    float DomainWarpIntensity = 0.0f;
    float TerraceSteps = 0.0f;
    TObjectPtr<UMaterialInterface> CaveMaterial = nullptr;
    float TextureTiling = 0.01f;
    bool bGenerateSmartColors = true;
    TArray<FIOCScatterLayer> DecorationLayers;
    bool bEnableLOD = true;
    float LODDistance = 5000.0f;
    float LODVoxelSizeMultiplier = 3.0f;
    bool bUseWorldSpaceNoise = false;
    bool bUseFixedBoundsForTunnel = false;
    bool bAutoRebuildNavMesh = false;
    bool bForcePreset = false;
    bool bShowDebugViz = true;
    bool bLogPresetDebug = false;
    TArray<FVector> SplinePoints;
};

struct FIOCPreflightEstimate
{
    int64 EstimatedVoxels = 0;
    double EstimatedMillions = 0.0;
    double PathLength = 0.0;
    double ComplexityScore = 0.0;
    double ComplexityMultiplier = 1.0;
    int32 ScatterLayerCount = 0;
    FString RiskLabel;
};

/**
 * FGCObject because the rollback snapshot below stores bare UObject pointers (a material and
 * the scatter meshes) on a Slate widget. Nothing else keeps them reachable, so without
 * reporting them the GC is free to collect the very assets the wizard promises to restore.
 */
class SIOCSetupWizard : public SCompoundWidget, public FGCObject
{
public:
    SLATE_BEGIN_ARGS(SIOCSetupWizard) {}
        SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
    SLATE_END_ARGS()

    ~SIOCSetupWizard();

    void Construct(const FArguments& InArgs);

    static void OpenWizard();
    static bool CaptureRenderedPresetPreview(const FString& PresetName, bool bExitOnComplete = false);

    // FGCObject
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override { return TEXT("SIOCSetupWizard"); }

private:
    TWeakPtr<SWindow> ParentWindow;
    TSharedPtr<SWidgetSwitcher> PageSwitcher;
    TSharedPtr<SVerticalBox> PresetCardsContainer;
    TSharedPtr<SVerticalBox> ExistingActorListContainer;
    TWeakPtr<FActiveTimerHandle> ActiveTimerHandle;

    int32 CurrentPage = 0;
    static constexpr int32 TotalPages = 5;
    FIOCSetupWizardSettings WizardSettings;
    FIOCSetupWizardViewModel ViewModel;
    FIOCSetupWizardRollbackSnapshot RollbackSnapshot;

    // Feature 1: Live 3D preview
    TSharedPtr<SIOCPreviewViewport> PreviewViewport;
    void UpdatePreviewActor();
    void DestroyPreviewActor();
    void CollectGenerationStats(AIOCProceduralActor* Cave);
    void DetectExistingCaves();
    void RebuildExistingActorList();
    TSharedRef<SWidget> BuildExistingActorSelector();
    void RequestPreviewRefresh(double DelaySeconds = 0.25);
    void RequestValidationRefresh(double DelaySeconds = 0.0);
    void RequestExistingCaveRefresh(double DelaySeconds = 0.0);
    EActiveTimerReturnType HandleActiveTimer(double InCurrentTime, float InDeltaTime);
    uint32 BuildPreviewStateSignature() const;
    uint32 BuildValidationSignature() const;
    uint32 BuildExistingCaveSignature() const;
    void RefreshValidationState(bool bForce = false);
    void SetCurrentPage(int32 NewPage);
    bool CanRunSetup() const;
    bool CanAdvanceFromCurrentPage() const;
    FIOCPreflightEstimate BuildPreflightEstimate() const;
    void BuildGenerationSummary();

    // Feature 9: Progress
    TSharedPtr<SWidget> ProgressBarWidget;

    // Feature 10: Undo - track spawned actors
    TArray<TWeakObjectPtr<AActor>> SpawnedActors;
    FDelegateHandle WorldCleanupHandle;
    void RegisterCleanupHandler();
    void CleanupSpawnedActors();
    void CaptureRollbackSnapshot(AIOCProceduralActor* Cave);
    bool RestoreRollbackSnapshot();
    bool HasRollbackSnapshot() const;

    FDelegateHandle PreviewGenerationStartedHandle;
    FDelegateHandle PreviewGenerationFinishedHandle;
    FDelegateHandle ConfiguredGenerationStartedHandle;
    FDelegateHandle ConfiguredGenerationFinishedHandle;
    void BindPreviewActorDelegates(AIOCProceduralActor* Cave);
    void UnbindPreviewActorDelegates();
    void BindConfiguredCaveDelegates(AIOCProceduralActor* Cave);
    void UnbindConfiguredCaveDelegates();
    void HandlePreviewGenerationStarted(AIOCProceduralActor* Cave);
    void HandlePreviewGenerationFinished(AIOCProceduralActor* Cave, bool bCancelled, bool bWillRegenerate);
    void HandleConfiguredGenerationStarted(AIOCProceduralActor* Cave);
    void HandleConfiguredGenerationFinished(AIOCProceduralActor* Cave, bool bCancelled, bool bWillRegenerate);

    // Feature 11: Persist settings
    void SaveSettings();
    void LoadSettings();
    void SaveStateToSection(const FString& SectionName);
    void LoadStateFromSection(const FString& SectionName);

    // Page builders
    TSharedRef<SWidget> BuildStepIndicator();
    TSharedRef<SWidget> BuildNavigation();
    TSharedRef<SWidget> BuildWelcomePage();
    TSharedRef<SWidget> BuildPresetPage();
    TSharedRef<SWidget> BuildEnvironmentPage();
    TSharedRef<SWidget> BuildAdvancedPage();
    TSharedRef<SWidget> BuildFinishPage();

    // Feature 6: Comparison table
    TSharedRef<SWidget> BuildComparisonTable();

    // Feature 8: Environment defaults per preset
    void ApplyEnvironmentDefaults();

    TSharedRef<SWidget> MakePresetCard(int32 Index, const FString& Title,
        const FString& Icon, const FString& Desc, const FString& Specs, const FLinearColor& Tint);

    FReply OnNext();
    FReply OnBack();
    FReply OnCancel();
    FReply OnRunShowcase();

    // Feature 3: Open docs
    FReply OnOpenDocumentation();

    // Feature 4: Copy commands
    FReply OnCopyCommand(const FString& Command);

    // Feature 10: Undo
    FReply OnUndoLastSetup();
    FReply OnRefreshStats();
    FReply OnSelectConfiguredCave();
    FReply OnValidateInstallation();
    FReply OnRunStarterAssetSetup();
    FReply OnCreateStarterLevel();
    FReply OnOpenStarterAssets();
    FReply OnOpenShowcaseMap();

    /** Opens the demo map that ships inside the plugin. Always available, so unlike
     *  OnOpenShowcaseMap it cannot fail on a fresh install. */
    FReply OnOpenDemoMap();

    /** Removes every actor the demos spawned, so the wizard can undo what it just started. */
    FReply OnClearAllDemos();
    FReply OnRefreshExistingCaves();
    FReply OnRestorePreviousSettings();
    FReply OnAddCarvingVolume();
    FReply OnAddStreamingManager();
    FReply OnConvertPresetToCustom();
    FReply OnRandomizeSeed();
    FReply OnUseSaferVoxelSize();
    FReply OnReduceBounds();
    FReply OnEnableFixedTunnelBounds();
    FReply OnSaveProfile();
    FReply OnLoadProfile();
    FReply OnCopyProfile();

    void PerformSetup();
    void ApplyWizardSettingsToCave(AIOCProceduralActor* Cave, bool bPreview) const;
    void FocusConfiguredCave();
    void CopySelectedPresetToCustomSettings();
    bool PrepareStarterAssets(FString& OutStatus);
    bool CreateStarterLevel(FString& OutStatus);
    void HandlePostWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
    FString BuildInstallationValidationReport(bool& bOutAllOk) const;
    int64 EstimateVoxelCount() const;
    FString GetPreflightSummary() const;
    FString GetRecommendationText() const;
    FString ExportProfileText() const;

    EIOCCavePreset GetSelectedPreset() const;
    FString GetSelectedPresetName() const;
    FText GetNextButtonText() const;

    // Feature 12: Keyboard navigation
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

    friend class FIOCSetupWizardButtonSweepTest;
    // Pages through the wizard so its layout assertions see every page, not just the one
    // that happens to be active.
    friend class FIOCSetupWizardLayoutTest;
    friend class FIOCSetupWizardWorkflowTest;
    friend class FIOCSetupWizardRollbackTest;
};

#endif
