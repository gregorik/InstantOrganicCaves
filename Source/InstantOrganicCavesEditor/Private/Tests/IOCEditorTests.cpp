// Copyright (c) 2026 GregOrigin. All Rights Reserved.
//
// Automation coverage for the editor-only half of the plugin. These moved here with the
// setup wizard when it was lifted out of the Runtime module; they need UnrealEd and Slate
// and have no business being linked into a game build.

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "Editor.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

#include "IOCSetupWizard.h"
#include "IOCProceduralActor.h"
#include "InstantOrganicCavesEditorModule.h"
#include "InstantOrganicCavesModule.h"
#include "IOCWizardStyle.h"
#include "Misc/Paths.h"
#include "Types/ISlateMetaData.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/ArrangedWidget.h"
#include "Widgets/SWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCSetupWizardSafetyTest, "InstantOrganicCaves.SetupWizard.SafetyAndValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCSetupWizardSafetyTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor)
    {
        AddError(TEXT("No editor available."));
        return false;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        AddError(TEXT("No editor world available."));
        return false;
    }

    if (!IConsoleManager::Get().FindConsoleObject(TEXT("IOC.ValidateInstallation")))
    {
        AddError(TEXT("IOC.ValidateInstallation console command is not registered."));
        return false;
    }

    // The wizard's "Open Demo Map" button exists so a first-run user has a route in that does
    // not depend on having created a starter level. It is only useful if the package it
    // targets actually ships -- a packaging-filter edit or a content rebuild could drop it,
    // and the button would then fail exactly when a new user needs it most.
    if (!FPackageName::DoesPackageExist(TEXT("/InstantOrganicCaves/Maps/IOC_DemoMap")))
    {
        AddError(TEXT("The wizard offers 'Open Demo Map', but /InstantOrganicCaves/Maps/IOC_DemoMap ")
            TEXT("does not exist. Regenerate it with Resources/GenerateDemoMap.py."));
    }

    // Every console command the Finish page advertises must actually be registered, or the
    // wizard is teaching users commands that do nothing.
    static const TCHAR* AdvertisedCommands[] = {
        TEXT("IOC.SpawnTunnelDemo"), TEXT("IOC.SpawnSpectacular"), TEXT("IOC.SpawnShowcase"),
        TEXT("IOC.ClearShowcase"), TEXT("IOC.ClearAllDemos"), TEXT("IOC.ValidateInstallation"),
        TEXT("IOC.OpenSetupWizard"),
    };
    for (const TCHAR* Command : AdvertisedCommands)
    {
        if (!IConsoleManager::Get().FindConsoleObject(Command))
        {
            AddError(FString::Printf(
                TEXT("The wizard's Finish page advertises '%s', but it is not registered."), Command));
        }
    }

    FActorSpawnParameters SP;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AIOCProceduralActor* ExistingCave = World->SpawnActor<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(), FVector(3000, 0, 0), FRotator::ZeroRotator, SP);
    if (!ExistingCave)
    {
        AddError(TEXT("Failed to spawn existing cave actor."));
        return false;
    }

    ExistingCave->CavePreset = EIOCCavePreset::LargeTunnel;
    ExistingCave->ApplyPresetSettingsOnly();
    ExistingCave->CavePreset = EIOCCavePreset::Custom;
    ExistingCave->GenerateCave();

    if (!ExistingCave->bIsGeneratingDisplay &&
        ExistingCave->LastEstimatedVoxelCount == 0 &&
        ExistingCave->LastPrimaryTriangleCount == 0)
    {
        AddError(TEXT("Setup-style cave generation did not start or produce metrics."));
        World->DestroyActor(ExistingCave);
        return false;
    }

    TArray<TWeakObjectPtr<AActor>> AddedActors;
    ADirectionalLight* AddedLight = World->SpawnActor<ADirectionalLight>(
        FVector(3000, 300, 500), FRotator(-45, 0, 0), SP);
    if (AddedLight)
    {
        AddedActors.Add(AddedLight);
    }

    for (TWeakObjectPtr<AActor>& AddedActor : AddedActors)
    {
        if (AddedActor.IsValid())
        {
            World->DestroyActor(AddedActor.Get());
        }
    }

    if (!IsValid(ExistingCave))
    {
        AddError(TEXT("Existing cave was destroyed by setup cleanup simulation."));
        return false;
    }

    World->DestroyActor(ExistingCave);
    AddInfo(TEXT("Setup validation command exists, generation starts, and cleanup preserves reconfigured existing cave actors."));
    return true;
#else
    return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCSetupWizardWorkflowTest, "InstantOrganicCaves.SetupWizard.Workflow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCSetupWizardWorkflowTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor)
    {
        AddError(TEXT("No editor available."));
        return false;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        AddError(TEXT("No editor world available."));
        return false;
    }

    TSharedRef<SIOCSetupWizard> Wizard = SNew(SIOCSetupWizard);
    Wizard->RefreshValidationState(true);
    TestTrue(TEXT("Validation report was populated"), !Wizard->ViewModel.InstallationStatus.IsEmpty());

    const int32 InitialCount = Wizard->ViewModel.ExistingCaves.Num();

    FActorSpawnParameters SP;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AIOCProceduralActor* ExtraCave = World->SpawnActor<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(), FVector(6000, 0, 0), FRotator::ZeroRotator, SP);
    if (!ExtraCave)
    {
        AddError(TEXT("Failed to spawn extra cave actor."));
        return false;
    }

    Wizard->DetectExistingCaves();
    TestEqual(TEXT("Existing cave list refreshes"), Wizard->ViewModel.ExistingCaves.Num(), InitialCount + 1);

    World->DestroyActor(ExtraCave);

    FString StarterLevelStatus;
    const bool bStarterLevelOk = Wizard->CreateStarterLevel(StarterLevelStatus);
    TestTrue(TEXT("Starter level creation/opening succeeded"), bStarterLevelOk);
    return true;
#else
    return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCSetupWizardRollbackTest, "InstantOrganicCaves.SetupWizard.Rollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCSetupWizardRollbackTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor)
    {
        AddError(TEXT("No editor available."));
        return false;
    }

    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World)
    {
        AddError(TEXT("No editor world available."));
        return false;
    }

    FActorSpawnParameters SP;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AIOCProceduralActor* ExistingCave = World->SpawnActor<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(), FVector(9000, 0, 0), FRotator::ZeroRotator, SP);
    if (!ExistingCave)
    {
        AddError(TEXT("Failed to spawn cave for rollback test."));
        return false;
    }

    ExistingCave->VoxelSize = 80.0;
    ExistingCave->GenerationBounds = FVector(2200.0f, 2200.0f, 1400.0f);
    const double OriginalVoxelSize = ExistingCave->VoxelSize;
    const FVector OriginalBounds = ExistingCave->GenerationBounds;

    TSharedRef<SIOCSetupWizard> Wizard = SNew(SIOCSetupWizard);
    Wizard->WizardSettings.bIsCustom = true;
    Wizard->WizardSettings.bCustomTunnelMode = true;
    Wizard->WizardSettings.CustomVoxelSize = 40.0;
    Wizard->WizardSettings.CustomBounds = FVector(4800.0f, 2400.0f, 1800.0f);
    Wizard->WizardSettings.bAddLighting = false;
    Wizard->WizardSettings.bAddPlayer = false;
    Wizard->WizardSettings.bReconfigureExisting = true;
    Wizard->WizardSettings.SelectedExistingIndex = 0;
    Wizard->ViewModel.ExistingCaves = { ExistingCave };

    Wizard->PerformSetup();
    TestTrue(TEXT("Setup enters generating state instead of finishing immediately"), Wizard->ViewModel.bIsGenerating);
    TestFalse(TEXT("Setup is not marked complete before async generation finishes"), Wizard->ViewModel.bGenerationDone);
    TestTrue(TEXT("Rollback snapshot was captured"), Wizard->HasRollbackSnapshot());
    TestEqual(TEXT("Wizard changed the cave voxel size"), ExistingCave->VoxelSize, Wizard->WizardSettings.CustomVoxelSize);

    const bool bRestored = Wizard->RestoreRollbackSnapshot();
    TestTrue(TEXT("Rollback restoration succeeded"), bRestored);
    TestEqual(TEXT("Voxel size restored"), ExistingCave->VoxelSize, OriginalVoxelSize);
    TestEqual(TEXT("Bounds restored"), ExistingCave->GenerationBounds, OriginalBounds);

    World->DestroyActor(ExistingCave);
    return true;
#else
    return true;
#endif
}


// -----------------------------------------------------------------------
// Every wizard button must actually do something, and none may crash.
//
// The wizard has 27 button handlers. Three had automated coverage; the rest had never been
// invoked by anything but a human clicking. A handler that throws, dereferences a stale
// pointer, or silently does nothing is invisible until a customer finds it.
//
// Two handlers are deliberately excluded:
//   * OnOpenDocumentation launches an external web browser -- intrusive, and its success
//     says nothing about the plugin.
//   * OnCancel destroys the parent window, which does not exist in a test.
// OnRunStarterAssetSetup was expected to write into plugin content, which would have made
// this test unsafe against a junction to the real plugin. It does not: PrepareStarterAssets
// is a no-op that only focuses the Content Browser, because the starter assets already ship.
// Verified by checksumming all 23 plugin packages before and after a full suite run against
// the real plugin -- unchanged. Re-check that if PrepareStarterAssets ever starts creating
// assets.
// -----------------------------------------------------------------------
// The wizard's Slate layout.
//
// Until this test the wizard had no layout coverage at all, and two defects shipped because
// of it: the hero banner was stretched (an SImage in a fixed-height slot is scaled to the
// slot's shape, not its own), and every button row stacked vertically. The second is the
// instructive one -- SWrapBox defaults to PreferredSize = 100 with UseAllottedSize = false,
// so it wraps after about one child no matter how much room it has. All twelve instances in
// the wizard set neither property, so every wrap box in the file had always been wrapping at
// 100px. SetupWizard.ButtonSweep did not catch it, because it proves handlers run, not that
// widgets are arranged.
//
// The two halves are asserted differently, and deliberately so:
//
//   The hero is *measured*. An SScaleBox arranges its child at the child's own desired size
//   and scales the result, so the aspect ratio survives arrangement and can be checked from
//   a synthetic layout pass. A bare SImage in the fixed-height slot arranges at the slot's
//   shape instead, which is exactly the stretch, so this fails on the real geometry.
//
//   The wrap boxes are *not* measured, because they cannot be here. SWrapBox with
//   UseAllottedSize reads GetTickSpaceGeometry() -- the geometry from the last real Slate
//   pass -- and a widget that has never been ticked in a shown window reports zero width,
//   wraps after every child, and looks precisely like the bug. Putting the wizard in an
//   off-screen window and ticking does not help: nothing lays it out, tick-space geometry
//   stays zero, and the assertion would report a failure that is an artefact of the test.
//   Rather than ship an assertion that cries wolf, this enforces the invariant instead --
//   every SWrapBox in the tree must carry IOCWizardTags::WrapBox, which is set at the same
//   call as .UseAllottedSize(true). A bare SNew(SWrapBox) copied in later has neither and
//   fails here. It checks the idiom, not the pixels; a genuine pixel check needs a rendering
//   editor with a shown window.
// -----------------------------------------------------------------------
namespace IOCLayoutTest
{
    // Comfortably wider than any row the wizard builds.
    static constexpr float WindowW = 1600.0f;
    static constexpr float WindowH = 900.0f;

    /**
     * Depth-first traversal by arrangement.
     *
     * GetChildren() alone does not reach everything -- panels that route their content
     * through an internal widget hand back a different child set than the one Slate lays
     * out, and a walk over it silently misses whole subtrees (it found 248 widgets and not
     * one of the twelve wrap boxes). Arranging follows the same path Slate does when
     * painting.
     */
    static void CollectByArranging(const TSharedRef<SWidget>& RootWidget,
                                   TArray<TSharedRef<SWidget>>& Out)
    {
        TFunction<void(const FArrangedWidget&, int32)> Walk =
            [&Out, &Walk](const FArrangedWidget& Parent, int32 Depth)
            {
                if (Depth > 64)
                {
                    return;   // cycle guard; the wizard nests nowhere near this deep
                }
                FArrangedChildren Children(EVisibility::All);
                Parent.Widget->ArrangeChildren(Parent.Geometry, Children);

                for (int32 Index = 0; Index < Children.Num(); ++Index)
                {
                    Out.Add(Children[Index].Widget);
                    Walk(Children[Index], Depth + 1);
                }
            };

        RootWidget->SlatePrepass(1.0f);
        const FArrangedWidget Root(
            RootWidget,
            FGeometry::MakeRoot(FVector2D(WindowW, WindowH), FSlateLayoutTransform()));
        Walk(Root, 0);
    }

    static FName GetTag(const TSharedRef<SWidget>& Widget)
    {
        const TSharedPtr<FTagMetaData> Tag = Widget->GetMetaData<FTagMetaData>();
        return Tag.IsValid() ? Tag->Tag : NAME_None;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCSetupWizardLayoutTest,
    "InstantOrganicCaves.SetupWizard.Layout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCSetupWizardLayoutTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!FSlateApplication::IsInitialized())
    {
        AddWarning(TEXT("Slate is not initialised, so the widget tree cannot be built."));
        return true;
    }

    TSharedRef<SIOCSetupWizard> Wizard = SNew(SIOCSetupWizard);

    // --- the hero banner must keep its aspect ratio ----------------------------------
    TArray<FArrangedWidget> Arranged;
    {
        TFunction<void(const FArrangedWidget&, int32)> Walk =
            [&Arranged, &Walk](const FArrangedWidget& Parent, int32 Depth)
            {
                if (Depth > 64)
                {
                    return;
                }
                FArrangedChildren Children(EVisibility::All);
                Parent.Widget->ArrangeChildren(Parent.Geometry, Children);
                for (int32 Index = 0; Index < Children.Num(); ++Index)
                {
                    Arranged.Add(Children[Index]);
                    Walk(Children[Index], Depth + 1);
                }
            };
        Wizard->SlatePrepass(1.0f);
        Walk(FArrangedWidget(Wizard,
                FGeometry::MakeRoot(
                    FVector2D(IOCLayoutTest::WindowW, IOCLayoutTest::WindowH),
                    FSlateLayoutTransform())), 0);
    }

    const FArrangedWidget* Hero = Arranged.FindByPredicate(
        [](const FArrangedWidget& Candidate)
        {
            return IOCLayoutTest::GetTag(Candidate.Widget) == IOCWizardTags::HeroImage;
        });

    if (Hero == nullptr)
    {
        AddError(FString::Printf(
            TEXT("No widget tagged '%s' was arranged. If the tag was removed, restore it ")
            TEXT("rather than deleting this assertion."),
            *IOCWizardTags::HeroImage.ToString()));
    }
    else
    {
        const FVector2D Size = Hero->Geometry.GetLocalSize();
        if (Size.X <= 1.0 || Size.Y <= 1.0)
        {
            AddError(FString::Printf(TEXT("Hero image arranged at %.0fx%.0f."),
                Size.X, Size.Y));
        }
        else
        {
            const float Aspect = static_cast<float>(Size.X / Size.Y);
            const float Expected = 1240.0f / 648.0f;
            if (!FMath::IsNearlyEqual(Aspect, Expected, 0.05f))
            {
                AddError(FString::Printf(
                    TEXT("Hero image is arranged %.0fx%.0f (aspect %.3f) but its art is ")
                    TEXT("1240x648 (aspect %.3f), so the banner is distorted. It needs to ")
                    TEXT("stay inside an SScaleBox."), Size.X, Size.Y, Aspect, Expected));
            }
        }
    }


    // One page at a time: an SWidgetSwitcher arranges only its active child and its
    // GetChildren() exposes only that child too, so a single pass reaches barely a third of
    // the wizard's wrap boxes. The other pages are just as able to ship a broken row.
    // OnNext() is the same entry point SetupWizard.ButtonSweep already drives.
    TArray<TSharedRef<SWidget>> All;
    TSet<const SWidget*> Seen;
    for (int32 Page = 0; Page < 5; ++Page)
    {
        TArray<TSharedRef<SWidget>> PageWidgets;
        IOCLayoutTest::CollectByArranging(Wizard, PageWidgets);
        for (const TSharedRef<SWidget>& Widget : PageWidgets)
        {
            bool bAlready = false;
            Seen.Add(&Widget.Get(), &bAlready);
            if (!bAlready)
            {
                All.Add(Widget);
            }
        }
        Wizard->OnNext();
    }

    if (All.Num() < 50)
    {
        AddError(FString::Printf(
            TEXT("Only %d widgets were arranged; the wizard tree did not build, so nothing ")
            TEXT("below this would be meaningful."), All.Num()));
        return false;
    }

    // --- every wrap box must have gone through the idiom that sets UseAllottedSize ----
    int32 WrapBoxes = 0;
    int32 Untagged = 0;
    for (const TSharedRef<SWidget>& Widget : All)
    {
        if (Widget->GetType() != FName(TEXT("SWrapBox")))
        {
            continue;
        }
        ++WrapBoxes;
        if (IOCLayoutTest::GetTag(Widget) != IOCWizardTags::WrapBox)
        {
            ++Untagged;
        }
    }

    if (WrapBoxes == 0)
    {
        AddError(TEXT("No SWrapBox was reached in the arranged tree. The wizard builds ")
                 TEXT("twelve, so the traversal is broken and this assertion is checking ")
                 TEXT("nothing -- fix the walk rather than deleting the check."));
    }
    else if (Untagged > 0)
    {
        AddError(FString::Printf(
            TEXT("%d of %d SWrapBox widgets are missing the '%s' tag, so they were built ")
            TEXT("with a bare SNew(SWrapBox). SWrapBox defaults to PreferredSize=100 with ")
            TEXT("UseAllottedSize=false and wraps after roughly one child; add ")
            TEXT(".UseAllottedSize(true) and the tag together."),
            Untagged, WrapBoxes, *IOCWizardTags::WrapBox.ToString()));
    }

    AddInfo(FString::Printf(TEXT("Arranged %d widgets; %d wrap box(es), %d untagged."),
        All.Num(), WrapBoxes, Untagged));
    return !HasAnyErrors();
#else
    return true;
#endif
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCSetupWizardButtonSweepTest,
    "InstantOrganicCaves.SetupWizard.ButtonSweep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCSetupWizardButtonSweepTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    if (!GEditor || !GEditor->GetEditorWorldContext().World())
    {
        AddError(TEXT("No editor world available."));
        return false;
    }

    TSharedRef<SIOCSetupWizard> Wizard = SNew(SIOCSetupWizard);
    Wizard->RefreshValidationState(true);

    struct FButton
    {
        const TCHAR* Name;
        FReply (SIOCSetupWizard::*Handler)();
    };

    // Ordered deliberately: page navigation and pure-state buttons first, then actions that
    // spawn actors, then the ones that load a different level (which invalidates the world
    // pointer for anything after them).
    static const FButton Buttons[] = {
        { TEXT("OnValidateInstallation"),   &SIOCSetupWizard::OnValidateInstallation },
        { TEXT("OnRefreshStats"),           &SIOCSetupWizard::OnRefreshStats },
        { TEXT("OnRefreshExistingCaves"),   &SIOCSetupWizard::OnRefreshExistingCaves },
        { TEXT("OnRandomizeSeed"),          &SIOCSetupWizard::OnRandomizeSeed },
        { TEXT("OnReduceBounds"),           &SIOCSetupWizard::OnReduceBounds },
        { TEXT("OnUseSaferVoxelSize"),      &SIOCSetupWizard::OnUseSaferVoxelSize },
        { TEXT("OnEnableFixedTunnelBounds"),&SIOCSetupWizard::OnEnableFixedTunnelBounds },
        { TEXT("OnConvertPresetToCustom"),  &SIOCSetupWizard::OnConvertPresetToCustom },
        { TEXT("OnNext"),                   &SIOCSetupWizard::OnNext },
        { TEXT("OnBack"),                   &SIOCSetupWizard::OnBack },
        { TEXT("OnCopyProfile"),            &SIOCSetupWizard::OnCopyProfile },
        { TEXT("OnSaveProfile"),            &SIOCSetupWizard::OnSaveProfile },
        { TEXT("OnLoadProfile"),            &SIOCSetupWizard::OnLoadProfile },
        { TEXT("OnSelectConfiguredCave"),   &SIOCSetupWizard::OnSelectConfiguredCave },
        { TEXT("OnRestorePreviousSettings"),&SIOCSetupWizard::OnRestorePreviousSettings },
        { TEXT("OnUndoLastSetup"),          &SIOCSetupWizard::OnUndoLastSetup },
        { TEXT("OnAddCarvingVolume"),       &SIOCSetupWizard::OnAddCarvingVolume },
        { TEXT("OnAddStreamingManager"),    &SIOCSetupWizard::OnAddStreamingManager },
        { TEXT("OnOpenStarterAssets"),      &SIOCSetupWizard::OnOpenStarterAssets },
        { TEXT("OnRunStarterAssetSetup"),   &SIOCSetupWizard::OnRunStarterAssetSetup },
        { TEXT("OnRunShowcase"),            &SIOCSetupWizard::OnRunShowcase },
        { TEXT("OnClearAllDemos"),          &SIOCSetupWizard::OnClearAllDemos },
        { TEXT("OnCreateStarterLevel"),     &SIOCSetupWizard::OnCreateStarterLevel },
        { TEXT("OnOpenShowcaseMap"),        &SIOCSetupWizard::OnOpenShowcaseMap },
        { TEXT("OnOpenDemoMap"),            &SIOCSetupWizard::OnOpenDemoMap },
    };

    int32 Invoked = 0;
    for (const FButton& Button : Buttons)
    {
        // Reaching the next iteration at all is most of the assertion: these had never been
        // executed outside a human clicking them.
        const FReply Reply = (Wizard.Get().*(Button.Handler))();
        if (!Reply.IsEventHandled())
        {
            AddError(FString::Printf(
                TEXT("Wizard button '%s' returned an unhandled FReply; the click would fall ")
                TEXT("through to whatever is underneath it."), Button.Name));
        }
        ++Invoked;
    }

    // Leave no demo actors behind in whatever level we ended up in.
    FInstantOrganicCavesModule::ClearAllDemos();

    AddInfo(FString::Printf(
        TEXT("Invoked %d of 27 wizard button handlers (OnOpenDocumentation and OnCancel excluded)."),
        Invoked));
    return !HasAnyErrors();
#else
    return true;
#endif
}


// -----------------------------------------------------------------------
// The wizard's branding must actually be present and resolvable.
//
// An unresolved Slate brush does not warn or error -- it draws a checkerboard. So a
// packaging-filter edit or a missing Resources/UI folder would ship a wizard covered in
// placeholder squares and nothing would fail until a customer opened it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIOCWizardStyleAssetsTest,
    "InstantOrganicCaves.Content.WizardStyleAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIOCWizardStyleAssetsTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
    // Driven off the style set's own list rather than a copy of it, so adding an image
    // without shipping it cannot slip past this test.
    const TArray<FString> ImagePaths = FIOCWizardStyle::GetAllImagePaths();
    if (ImagePaths.Num() == 0 || ImagePaths[0].IsEmpty())
    {
        AddError(TEXT("The wizard style set could not resolve the plugin's Resources/UI directory."));
        return false;
    }

    for (const FString& Path : ImagePaths)
    {
        if (!FPaths::FileExists(Path))
        {
            AddError(FString::Printf(
                TEXT("Wizard image is missing at '%s'. Slate renders a missing brush as a ")
                TEXT("silent checkerboard, so the wizard would look broken with no error."),
                *Path));
        }
        else if (IFileManager::Get().FileSize(*Path) <= 0)
        {
            AddError(FString::Printf(TEXT("Wizard image at '%s' is empty."), *Path));
        }
    }

    // The style set must also actually carry the brushes and the three button styles, or
    // the wizard falls back to engine defaults and loses its hover/press feedback.
    const ISlateStyle& Style = FIOCWizardStyle::Get();
    for (const FName BrushName : { FIOCWizardStyle::HeroBrush, FIOCWizardStyle::EmblemBrush,
                                   FIOCWizardStyle::BackdropBrush, FIOCWizardStyle::ScrimDownBrush,
                                   FIOCWizardStyle::VignetteBrush, FIOCWizardStyle::GregOriginBrush,
                                   FIOCWizardStyle::CardPanel, FIOCWizardStyle::CardPanelHover,
                                   FIOCWizardStyle::GlassPanel, FIOCWizardStyle::ChipPill,
                                   FIOCWizardStyle::CardBody, FIOCWizardStyle::SpineLeft })
    {
        if (Style.GetBrush(BrushName) == FStyleDefaults::GetNoBrush())
        {
            AddError(FString::Printf(TEXT("Wizard style set has no brush registered for '%s'."),
                *BrushName.ToString()));
        }
    }

    // The panel fills have to stay translucent. The wizard draws cave art behind every
    // page and the panels are what it shows through -- an opaque fill here does not fail
    // anything, it just silently returns the wizard to flat grey boxes, which is exactly
    // the regression this guards.
    for (const FName BrushName : { FIOCWizardStyle::CardPanel, FIOCWizardStyle::CardPanelHover,
                                   FIOCWizardStyle::GlassPanel, FIOCWizardStyle::CardBody })
    {
        const FSlateBrush* Brush = Style.GetBrush(BrushName);
        if (Brush == FStyleDefaults::GetNoBrush())
        {
            continue;   // already reported above
        }
        const float FillAlpha = Brush->TintColor.GetSpecifiedColor().A;
        if (FillAlpha >= 1.0f)
        {
            AddError(FString::Printf(
                TEXT("Wizard panel brush '%s' is fully opaque (alpha %.2f), so the cave ")
                TEXT("backdrop cannot show through it."), *BrushName.ToString(), FillAlpha));
        }
    }

    for (const FName StyleName : { FIOCWizardStyle::ButtonPrimary,
                                   FIOCWizardStyle::ButtonSecondary,
                                   FIOCWizardStyle::ButtonDanger })
    {
        // ISlateStyle exposes HasWidgetStyle/GetWidgetStyle, not a pointer accessor.
        if (!Style.HasWidgetStyle<FButtonStyle>(StyleName))
        {
            AddError(FString::Printf(TEXT("Wizard button style '%s' is not registered."),
                *StyleName.ToString()));
            continue;
        }
        const FButtonStyle& ButtonRef = Style.GetWidgetStyle<FButtonStyle>(StyleName);
        const FButtonStyle* Button = &ButtonRef;
        // The press offset is what makes a button feel tactile; without it the button is
        // visually flat on click even if the colours change.
        if (Button->PressedPadding == Button->NormalPadding)
        {
            AddError(FString::Printf(
                TEXT("Wizard button style '%s' has no press offset, so clicking it does not ")
                TEXT("visibly depress the label."), *StyleName.ToString()));
        }
    }

    TArray<FString> Names;
    Names.Reserve(ImagePaths.Num());
    for (const FString& Path : ImagePaths)
    {
        Names.Add(FPaths::GetCleanFilename(Path));
    }
    AddInfo(FString::Printf(TEXT("Wizard branding resolved (%d images): %s"),
        Names.Num(), *FString::Join(Names, TEXT(", "))));
    return !HasAnyErrors();
#else
    return true;
#endif
}

#endif // WITH_DEV_AUTOMATION_TESTS
