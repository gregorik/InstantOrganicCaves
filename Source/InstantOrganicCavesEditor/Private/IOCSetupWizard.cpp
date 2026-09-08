// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "IOCSetupWizard.h"
#include "IOCWizardStyle.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Types/ISlateMetaData.h"

#if WITH_EDITOR

#include "InstantOrganicCavesModule.h"
#include "InstantOrganicCavesEditorModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Interfaces/IPluginManager.h"
#include "PluginDescriptor.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "SEditorViewport.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "PreviewScene.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformApplicationMisc.h"
#include "FileHelpers.h"
#include "LevelEditorSubsystem.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "IOCCharacter.h"
#include "IOCCarvingComponent.h"
#include "IOCShowcaseLauncher.h"
#include "IOCStreamingManager.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/PointLight.h"
#include "Components/PointLightComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Containers/Ticker.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Serialization/BufferArchive.h"
#include <initializer_list>

#define LOCTEXT_NAMESPACE "SIOCSetupWizard"

namespace IOCWizard
{
    static TWeakPtr<SWindow> ActiveWindow;

    // Sampled from the shipped brand art (Resources/UI) rather than chosen independently,
    // so the chrome and the imagery read as one thing. The wizard was previously amber on
    // near-black, which fought the teal/moss artwork it now sits beside.
    static const FLinearColor AccentColor(0.230f, 0.720f, 0.800f);   // IOC1 highlight cyan
    static const FLinearColor AccentDim(0.064f, 0.298f, 0.394f);     // IOC1 mid teal
    static const FLinearColor MossAccent(0.300f, 0.470f, 0.150f);    // the logo mark's green
    // Alpha < 1 so the cave backdrop bleeds through the chrome instead of the wizard being
    // a stack of opaque slabs. Kept dark enough that body text still reads cleanly on top.
    //
    // PanelBg covers the entire window, so its alpha sets how much backdrop survives
    // anywhere: at 0.82 the art underneath contributed a few percent and the wizard was
    // flat navy no matter what the panels above did. Mirrors IOCStyleColors in
    // IOCWizardStyle.cpp, which builds the rounded panel brushes from the same values.
    static const FLinearColor PanelBg(0.008f, 0.020f, 0.028f, 0.55f);   // IOC2 cave dark
    static const FLinearColor CardBg(0.018f, 0.038f, 0.050f, 0.72f);
    static const FLinearColor CardBgHover(0.034f, 0.070f, 0.090f, 0.86f);
    static const FLinearColor GlassBg(0.020f, 0.045f, 0.058f, 0.55f);   // most translucent
    // Badges used to sit on a neutral grey that belonged to no part of the palette; that
    // grey was most of what made the wizard look like unstyled editor furniture.
    static const FLinearColor BadgeBg(0.064f, 0.298f, 0.394f, 0.30f);
    static const FLinearColor TextPrimary(0.90f, 0.95f, 0.97f);
    static const FLinearColor TextSecondary(0.55f, 0.66f, 0.70f);
    static const FLinearColor TextDim(0.35f, 0.45f, 0.50f);


    struct FPresetInfo
    {
        EIOCCavePreset Preset;
        FString Name;
        FString Icon;
        FString Description;
        FString Specs;
        FLinearColor Color;
        float RecommendedRadius;
        float RecommendedFogDensity;
        bool bRecommendsSkyLight;
        bool bRecommendsPointLights;
    };

    static const FPresetInfo Presets[] = {
        { EIOCCavePreset::LargeTunnel, TEXT("Large Tunnel"),
          TEXT("[A]"),
          TEXT("Wide, gentle tunnels for vehicle passages and spacious corridors."),
          TEXT("Radius 450 cm | Voxel 40 cm | Rock debris + crystal accents"),
          FLinearColor(1.0f, 0.7f, 0.3f), 450.0f, 0.015f, true, false },
        { EIOCCavePreset::TightCrawl, TEXT("Tight Crawl"),
          TEXT("[V]"),
          TEXT("Narrow, winding passages for tense exploration and claustrophobic environments."),
          TEXT("Radius 150 cm | Voxel 20 cm | Sparse clutter"),
          FLinearColor(0.8f, 0.55f, 0.35f), 150.0f, 0.04f, false, true },
        { EIOCCavePreset::OpenCavern, TEXT("Open Cavern"),
          TEXT("[O]"),
          TEXT("Massive open chambers for boss arenas, underground lakes, and gathering spaces."),
          TEXT("Radius 1200 cm | Voxel 60 cm | Large boulders"),
          FLinearColor(0.35f, 0.6f, 1.0f), 1200.0f, 0.008f, true, false },
        { EIOCCavePreset::AlienHive, TEXT("Alien Hive"),
          TEXT("[*]"),
          TEXT("Domain-warped organic shapes for sci-fi biomes and alien environments."),
          TEXT("Warp 200 | Voxel 30 cm | Wall crystal growth"),
          FLinearColor(0.3f, 0.9f, 0.45f), 300.0f, 0.025f, false, true },
        { EIOCCavePreset::CanyonStrata, TEXT("Canyon Strata"),
          TEXT("[#]"),
          TEXT("Terraced geological layers for mines, ravines, and exposed rock shelves."),
          TEXT("Terrace 150 cm | Voxel 50 cm | Shelf rocks + geodes"),
          FLinearColor(1.0f, 0.5f, 0.15f), 500.0f, 0.02f, true, false },
    };

    static constexpr int32 CustomIndex = 5;
    static const FString CustomName(TEXT("Custom"));
    static const FString CustomIcon(TEXT("[+]"));
    static const FString CustomDesc(TEXT("Full control over every parameter. Configure noise, tunnels, and more."));
    static const FString CustomSpecs(TEXT("User-defined | All parameters exposed"));
    static const FLinearColor CustomColor(0.55f, 0.55f, 0.65f);
    static const TCHAR* StarterBlueprintObjectPath = TEXT("/InstantOrganicCaves/BP_IOC_Cave.BP_IOC_Cave");
    static const TCHAR* StarterMaterialInstanceObjectPath = TEXT("/InstantOrganicCaves/MI_IOC_SmartCave_Inst.MI_IOC_SmartCave_Inst");
    static const TCHAR* StarterLevelPackagePath = TEXT("/Game/IOC_Showcase");

    /** The demo map that ships inside the plugin. Unlike the starter level above, this one
     *  always exists, so it is the entry point that cannot dead-end on a fresh install. */
    static const TCHAR* ShippedDemoMapPackagePath = TEXT("/InstantOrganicCaves/Maps/IOC_DemoMap");

    static const TCHAR* ConfigSection = TEXT("IOCSetupWizard");
    static const FString ConfigFile(GGameIni);

    static UWorld* GetEditorWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    static bool ShouldGenerateImmediately(UWorld* World)
    {
        return World && !World->IsPlayInEditor() && !World->IsGameWorld();
    }

    // Feature 5: Tooltip database
    static const TMap<FString, FString>& GetParamTooltips()
    {
        static TMap<FString, FString> Tooltips;
        if (Tooltips.IsEmpty())
        {
            Tooltips.Add(TEXT("Seed"), TEXT("Random seed for cave generation. Same seed = same cave. Try different values for variety."));
            Tooltips.Add(TEXT("Voxel Size (cm)"), TEXT("Resolution of the voxel grid. Smaller = more detail but slower. 20-60 cm typical."));
            Tooltips.Add(TEXT("Noise Frequency"), TEXT("Controls Perlin noise scale. Higher = tighter/noisier patterns. 0.001-0.05 range."));
            Tooltips.Add(TEXT("Smooth Iterations"), TEXT("Post-processing smoothing passes. More = rounder walls. 0-20 range, 3-5 typical."));
            Tooltips.Add(TEXT("Tunnel Radius (cm)"), TEXT("Radius of carved tunnel paths. Larger = wider passages. Only in tunnel mode."));
            Tooltips.Add(TEXT("Wall Thickness (cm)"), TEXT("Thickness of cave walls around tunnel paths. Larger values make sturdier shell geometry."));
            Tooltips.Add(TEXT("Generation Bounds"), TEXT("Maximum generated volume for open caves and bounded tunnel generation."));
            Tooltips.Add(TEXT("Tunnel Start/End"), TEXT("Local-space endpoints for a direct tunnel. Use spline mode later for hand-shaped paths."));
            Tooltips.Add(TEXT("Domain Warp"), TEXT("Adds large organic deformation. Higher values create alien, twisted surfaces."));
            Tooltips.Add(TEXT("Terrace Steps"), TEXT("Creates stepped geological strata. 0 disables terracing."));
            Tooltips.Add(TEXT("Texture Tiling"), TEXT("Material UV scale. Lower values make rock texture appear larger."));
            Tooltips.Add(TEXT("LOD"), TEXT("Build a cheaper secondary mesh for distant viewing."));
            Tooltips.Add(TEXT("Tunnel mode"), TEXT("Generate a carved tunnel between two points. Uncheck for open cave volumes."));
        }
        return Tooltips;
    }

    struct FStepMeta
    {
        const TCHAR* Name;
        const TCHAR* Subtitle;
    };

    static const FStepMeta StepMeta[] = {
        { TEXT("Welcome"), TEXT("overview") },
        { TEXT("Choose Cave"), TEXT("preset and preview") },
        { TEXT("Environment"), TEXT("lighting and playtest") },
        { TEXT("Review"), TEXT("readiness and cost") },
        { TEXT("Finish"), TEXT("next actions") }
    };

    static FLinearColor GetRiskTint(const FString& RiskLabel)
    {
        if (RiskLabel == TEXT("Very high"))
        {
            return FLinearColor(0.96f, 0.39f, 0.29f);
        }
        if (RiskLabel == TEXT("High"))
        {
            return FLinearColor(0.96f, 0.69f, 0.23f);
        }
        if (RiskLabel == TEXT("Moderate"))
        {
            return FLinearColor(0.89f, 0.81f, 0.35f);
        }
        return FLinearColor(0.45f, 0.84f, 0.56f);
    }

    static FLinearColor GetRiskSurface(const FString& RiskLabel)
    {
        if (RiskLabel == TEXT("Very high"))
        {
            return FLinearColor(0.17f, 0.06f, 0.05f);
        }
        if (RiskLabel == TEXT("High"))
        {
            return FLinearColor(0.16f, 0.10f, 0.04f);
        }
        if (RiskLabel == TEXT("Moderate"))
        {
            return FLinearColor(0.11f, 0.10f, 0.04f);
        }
        return FLinearColor(0.05f, 0.08f, 0.06f);
    }

    static TSharedRef<SWidget> MakeBadge(
        const TAttribute<FText>& Text,
        const FLinearColor& Background,
        const FSlateColor& Foreground,
        int32 FontSize = 9,
        const FMargin& Padding = FMargin(10.0f, 4.0f))
    {
        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(Background)
            .Padding(Padding)
            [
                SNew(STextBlock)
                .Text(Text)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", FontSize))
                .ColorAndOpacity(Foreground)
            ];
    }

    /**
     * A content card: translucent, hairline-outlined, and fronted by a coloured spine.
     *
     * The spine is the wizard's one repeated identifying mark. It is what makes a screenful
     * of panels read as this product's panels rather than as default editor chrome, and it
     * doubles as a per-card status colour.
     *
     * It is a separate widget rather than a border on the card because the two pieces carry
     * complementary corner radii -- see FIOCWizardStyle::SpineLeft.
     */
    static TSharedRef<SWidget> MakeAccentPanel(
        TSharedRef<SWidget> Content,
        const FLinearColor& Spine = AccentColor,
        const FMargin& Padding = FMargin(16.0f, 14.0f))
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            [
                SNew(SBox)
                .WidthOverride(3.0f)
                [
                    SNew(SImage)
                    .Image(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::SpineLeft))
                    .ColorAndOpacity(Spine)
                ]
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(SBorder)
                .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::CardBody))
                .Padding(Padding)
                [
                    Content
                ]
            ];
    }

    /** Version string from the plugin descriptor, so the footer can never drift from it. */
    static FString GetPluginVersionName()
    {
        static const FString Cached = []() -> FString
        {
            const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("InstantOrganicCaves"));
            return Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : FString();
        }();
        return Cached;
    }

    /**
     * The publisher strip along the bottom of every page.
     *
     * Deliberately quiet: a tinted-down lockup and the version, under a hairline. It is
     * there to sign the tool, not to advertise -- at full strength it would compete with
     * the primary action sitting a few pixels above it.
     */
    static TSharedRef<SWidget> MakeBrandFooter()
    {
        const FString Version = GetPluginVersionName();
        const FText Right = Version.IsEmpty()
            ? INVTEXT("Instant Organic Caves")
            : FText::FromString(FString::Printf(TEXT("Instant Organic Caves  %s"), *Version));

        return SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SBox)
            .HeightOverride(1.0f)
            [
                SNew(SImage)
                .Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .ColorAndOpacity(FLinearColor(0.10f, 0.28f, 0.34f, 0.40f))
            ]
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(FMargin(0.0f, 10.0f, 0.0f, 0.0f))
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SBox)
                .WidthOverride(117.0f)     // 595x71 source, held to aspect
                .HeightOverride(14.0f)
                .ToolTipText(INVTEXT("Instant Organic Caves is published by GregOrigin."))
                [
                    SNew(SImage)
                    .Image(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::GregOriginBrush))
                    .ColorAndOpacity(FLinearColor(0.34f, 0.55f, 0.62f, 0.62f))
                ]
            ]

            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            .HAlign(HAlign_Right)
            [
                SNew(STextBlock)
                .Text(Right)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
                .ColorAndOpacity(FLinearColor(0.30f, 0.42f, 0.47f, 1.0f))
            ]
        ];
    }

    /** A capsule label. Reads as brand colour, not as the flat grey slab it replaced. */
    static TSharedRef<SWidget> MakeChip(const TAttribute<FText>& Text)
    {
        return SNew(SBorder)
            .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::ChipPill))
            .Padding(FMargin(12.0f, 5.0f))
            [
                SNew(STextBlock)
                .Text(Text)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                .ColorAndOpacity(FLinearColor(0.62f, 0.86f, 0.92f))
            ];
    }

    /**
     * A section title: accent tick, label, then a hairline rule running to the right margin.
     *
     * The rule is what gives a long scrolling page a sense of structure; bare bold text at
     * the same size as everything else does not separate one section from the next.
     */
    static TSharedRef<SWidget> MakeSectionHeader(const FText& Title, int32 FontSize = 14)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SBox)
                .WidthOverride(FontSize >= 16 ? 4.0f : 3.0f)
                .HeightOverride(FontSize + 3.0f)
                [
                    SNew(SImage)
                    .Image(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::SpineLeft))
                    .ColorAndOpacity(AccentColor)
                ]
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(FMargin(10.0f, 0.0f, 0.0f, 0.0f))
            [
                SNew(STextBlock)
                .Text(Title)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", FontSize))
                .ColorAndOpacity(TextPrimary)
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            .Padding(FMargin(14.0f, 0.0f, 0.0f, 0.0f))
            [
                SNew(SBox)
                .HeightOverride(1.0f)
                [
                    SNew(SImage)
                    .Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .ColorAndOpacity(FLinearColor(0.10f, 0.28f, 0.34f, 0.55f))
                ]
            ];
    }

    /**
     * The label inside a button.
     *
     * This used to paint its own flat SBorder background, which is why every button looked
     * and felt inert: the SButton itself used FCoreStyle "NoBorder", so there was no hover
     * and no press state anywhere in the wizard. The fill, outline, hover and press-offset
     * now come from the FButtonStyle in FIOCWizardStyle, and this only supplies the text.
     */
    static TSharedRef<SWidget> MakeActionLabel(
        const TAttribute<FText>& Text,
        const FSlateColor& Foreground,
        bool bBold = false,
        const FMargin& Padding = FMargin(14.0f, 8.0f))
    {
        return SNew(SBox)
            .Padding(Padding)
            .VAlign(VAlign_Center)
            .HAlign(HAlign_Center)
            [
                SNew(STextBlock)
                .Text(Text)
                .Font(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", 10))
                .ColorAndOpacity(Foreground)
            ];
    }

    static TSharedRef<SWidget> MakeMetricCard(
        const TAttribute<FText>& Label,
        const TAttribute<FText>& Value,
        const TAttribute<FText>& Caption,
        const FLinearColor& Tint)
    {
        // Tint already coloured the value; carrying it onto the spine turns four
        // interchangeable boxes into a row you can read at a glance.
        return MakeAccentPanel(
            SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(Label)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    .ColorAndOpacity(IOCWizard::TextDim)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
                [
                    SNew(STextBlock)
                    .Text(Value)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
                    .ColorAndOpacity(Tint)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
                [
                    SNew(STextBlock)
                    .Text(Caption)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(IOCWizard::TextDim)
                    .WrapTextAt(210.0f)
                ],
            Tint,
            FMargin(14.0f, 12.0f));
    }
}

#define SelectedPresetIndex WizardSettings.SelectedPresetIndex
#define bIsCustom WizardSettings.bIsCustom
#define bCustomTunnelMode WizardSettings.bCustomTunnelMode
#define bCustomUseSpline WizardSettings.bCustomUseSpline
#define CustomSeed WizardSettings.CustomSeed
#define CustomBounds WizardSettings.CustomBounds
#define CustomTunnelStart WizardSettings.CustomTunnelStart
#define CustomTunnelEnd WizardSettings.CustomTunnelEnd
#define CustomVoxelSize WizardSettings.CustomVoxelSize
#define CustomTunnelRadius WizardSettings.CustomTunnelRadius
#define CustomWallThickness WizardSettings.CustomWallThickness
#define CustomNoiseFrequency WizardSettings.CustomNoiseFrequency
#define CustomSmoothIterations WizardSettings.CustomSmoothIterations
#define CustomDomainWarpIntensity WizardSettings.CustomDomainWarpIntensity
#define CustomTerraceSteps WizardSettings.CustomTerraceSteps
#define CustomTextureTiling WizardSettings.CustomTextureTiling
#define CustomLODDistance WizardSettings.CustomLODDistance
#define CustomLODMultiplier WizardSettings.CustomLODMultiplier
#define bPreviewFullFidelity WizardSettings.bPreviewFullFidelity
#define bAutoFocusConfiguredCave WizardSettings.bAutoFocusConfiguredCave
#define bAddLighting WizardSettings.bAddLighting
#define bAddPlayer WizardSettings.bAddPlayer
#define bReconfigureExisting WizardSettings.bReconfigureExisting
#define SelectedExistingIndex WizardSettings.SelectedExistingIndex
#define ProfileName WizardSettings.ProfileName

#define bStarterAssetsPrepared ViewModel.bStarterAssetsPrepared
#define bSetupAttempted ViewModel.bSetupAttempted
#define bGenerationDone ViewModel.bGenerationDone
#define bIsGenerating ViewModel.bIsGenerating
#define GenerationSummary ViewModel.GenerationSummary
#define SetupError ViewModel.SetupError
#define StarterAssetStatus ViewModel.StarterAssetStatus
#define StarterLevelStatus ViewModel.StarterLevelStatus
#define InstallationStatus ViewModel.InstallationStatus
#define ProfileStatus ViewModel.ProfileStatus
#define PreviewStatus ViewModel.PreviewStatus
#define GenerationStats ViewModel.GenerationStats
#define LastPreviewBuiltAt ViewModel.LastPreviewBuiltAt
#define LastConfiguredCave ViewModel.LastConfiguredCave
#define PreviewActor ViewModel.PreviewActor
#define ExistingCaves ViewModel.ExistingCaves
#define GenerationProgress ViewModel.GenerationProgress

namespace
{
    struct FIOCStarterAsset
    {
        const TCHAR* Label;
        const TCHAR* ObjectPath;
    };

    static void IOC_LoadStarterAssets(TArray<UObject*>& OutAssets, TArray<FString>* OutMissingLabels = nullptr)
    {
        static const FIOCStarterAsset StarterAssets[] = {
            { TEXT("BP_IOC_Cave"), IOCWizard::StarterBlueprintObjectPath },
            { TEXT("MI_IOC_SmartCave_Inst"), IOCWizard::StarterMaterialInstanceObjectPath }
        };

        for (const FIOCStarterAsset& StarterAsset : StarterAssets)
        {
            if (UObject* Asset = LoadObject<UObject>(nullptr, StarterAsset.ObjectPath))
            {
                OutAssets.Add(Asset);
            }
            else if (OutMissingLabels)
            {
                OutMissingLabels->Add(StarterAsset.Label);
            }
        }
    }

    static EIOCCavePreset IOC_GetStylePresetForSelection(int32 PresetCardIndex)
    {
        if (PresetCardIndex >= 0 && PresetCardIndex < IOCWizard::CustomIndex)
        {
            return IOCWizard::Presets[PresetCardIndex].Preset;
        }

        return EIOCCavePreset::Custom;
    }

    static float IOC_GetRecommendedTextureTiling(EIOCCavePreset Preset)
    {
        switch (Preset)
        {
        case EIOCCavePreset::LargeTunnel:
            return 0.0035f;
        case EIOCCavePreset::TightCrawl:
            return 0.0059f;
        case EIOCCavePreset::OpenCavern:
            return 0.0017f;
        case EIOCCavePreset::AlienHive:
            return 0.0029f;
        case EIOCCavePreset::CanyonStrata:
            return 0.0024f;
        case EIOCCavePreset::Custom:
        default:
            return 0.0050f;
        }
    }

    static int32 IOC_GetRecommendedScatterLayerCount(EIOCCavePreset Preset)
    {
        switch (Preset)
        {
        case EIOCCavePreset::LargeTunnel:
            return 3;
        case EIOCCavePreset::TightCrawl:
            return 2;
        case EIOCCavePreset::OpenCavern:
            return 3;
        case EIOCCavePreset::AlienHive:
            return 3;
        case EIOCCavePreset::CanyonStrata:
            return 3;
        case EIOCCavePreset::Custom:
        default:
            return 0;
        }
    }

    static UStaticMesh* IOC_LoadFirstStaticMesh(std::initializer_list<const TCHAR*> ObjectPaths)
    {
        for (const TCHAR* ObjectPath : ObjectPaths)
        {
            if (!ObjectPath || ObjectPath[0] == TEXT('\0'))
            {
                continue;
            }

            if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, ObjectPath))
            {
                return Mesh;
            }
        }

        return nullptr;
    }

    static void IOC_AddScatterLayer(
        TArray<FIOCScatterLayer>& OutLayers,
        std::initializer_list<const TCHAR*> MeshPaths,
        float Density,
        const FVector2D& ScaleRange,
        bool bAlignToNormal,
        float MinSlopeZ,
        float MaxSlopeZ,
        float RandomPitch,
        float PoissonMinSeparation)
    {
        if (UStaticMesh* Mesh = IOC_LoadFirstStaticMesh(MeshPaths))
        {
            FIOCScatterLayer Layer;
            Layer.Mesh = Mesh;
            Layer.Density = Density;
            Layer.ScaleRange = ScaleRange;
            Layer.bAlignToNormal = bAlignToNormal;
            Layer.MinSlopeZ = MinSlopeZ;
            Layer.MaxSlopeZ = MaxSlopeZ;
            Layer.RandomPitch = RandomPitch;
            Layer.PoissonMinSeparation = PoissonMinSeparation;
            OutLayers.Add(Layer);
        }
    }

    static TArray<FIOCScatterLayer> IOC_BuildPresetDecorationLayers(EIOCCavePreset Preset)
    {
        TArray<FIOCScatterLayer> Layers;

        switch (Preset)
        {
        case EIOCCavePreset::LargeTunnel:
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Rock_B.SM_IOC_Rock_B"),
                  TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Rock_A.SM_IOC_Rock_A"),
                  TEXT("/Engine/BasicShapes/Cube.Cube") },
                0.075f, FVector2D(0.85f, 1.65f), true, 0.60f, 1.0f, 8.0f, 380.0f);
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Rock_C.SM_IOC_Rock_C"),
                  TEXT("/Engine/BasicShapes/Sphere.Sphere") },
                0.12f, FVector2D(0.38f, 0.85f), true, 0.72f, 1.0f, 18.0f, 280.0f);
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Crystal_A.SM_IOC_Crystal_A"),
                  TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Crystal_B.SM_IOC_Crystal_B"),
                  TEXT("/Engine/BasicShapes/Cone.Cone") },
                0.016f, FVector2D(0.55f, 1.05f), true, -0.10f, 0.80f, 14.0f, 520.0f);
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Stalactite.SM_IOC_Stalactite") },
                0.006f, FVector2D(0.60f, 1.30f), true, -1.0f, -0.60f, 0.0f, 620.0f);
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Stalagmite.SM_IOC_Stalagmite") },
                0.010f, FVector2D(0.70f, 1.40f), true, 0.75f, 1.0f, 0.0f, 520.0f);
            break;

        case EIOCCavePreset::TightCrawl:
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Rock_C.SM_IOC_Rock_C"),
                  TEXT("/Engine/BasicShapes/Sphere.Sphere") },
                0.06f, FVector2D(0.20f, 0.48f), true, 0.78f, 1.0f, 16.0f, 190.0f);
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Crystal_B.SM_IOC_Crystal_B"),
                  TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Crystal_A.SM_IOC_Crystal_A"),
                  TEXT("/Engine/BasicShapes/Cone.Cone") },
                0.016f, FVector2D(0.22f, 0.50f), true, -1.0f, 0.55f, 24.0f, 280.0f);
            break;

        case EIOCCavePreset::OpenCavern:
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Rock_A.SM_IOC_Rock_A"),
                  TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Rock_B.SM_IOC_Rock_B"),
                  TEXT("/Engine/BasicShapes/Cube.Cube") },
                0.026f, FVector2D(1.40f, 3.40f), true, 0.55f, 1.0f, 6.0f, 1050.0f);
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Geode.SM_IOC_Geode"),
                  TEXT("/Engine/BasicShapes/Sphere.Sphere") },
                0.008f, FVector2D(1.10f, 2.20f), true, -0.25f, 0.90f, 8.0f, 760.0f);
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Rock_C.SM_IOC_Rock_C"),
                  TEXT("/Engine/BasicShapes/Sphere.Sphere") },
                0.04f, FVector2D(0.65f, 1.20f), true, 0.75f, 1.0f, 16.0f, 400.0f);
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Stalactite.SM_IOC_Stalactite") },
                0.006f, FVector2D(0.60f, 1.30f), true, -1.0f, -0.60f, 0.0f, 620.0f);
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Stalagmite.SM_IOC_Stalagmite") },
                0.010f, FVector2D(0.70f, 1.40f), true, 0.75f, 1.0f, 0.0f, 520.0f);
            break;

        case EIOCCavePreset::AlienHive:
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Crystal_A.SM_IOC_Crystal_A"),
                  TEXT("/Engine/BasicShapes/Cylinder.Cylinder") },
                0.06f, FVector2D(0.75f, 1.65f), true, -1.0f, 0.85f, 30.0f, 320.0f);
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Crystal_B.SM_IOC_Crystal_B"),
                  TEXT("/Engine/BasicShapes/Cone.Cone") },
                0.035f, FVector2D(0.48f, 0.95f), true, -0.85f, 0.60f, 22.0f, 260.0f);
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Geode.SM_IOC_Geode"),
                  TEXT("/Engine/BasicShapes/Sphere.Sphere") },
                0.010f, FVector2D(0.75f, 1.20f), true, -0.40f, 0.55f, 10.0f, 440.0f);
            break;

        case EIOCCavePreset::CanyonStrata:
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Rock_A.SM_IOC_Rock_A"),
                  TEXT("/Engine/BasicShapes/Cube.Cube") },
                0.055f, FVector2D(1.00f, 2.20f), true, 0.20f, 1.0f, 8.0f, 460.0f);
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Rock_B.SM_IOC_Rock_B"),
                  TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Rock_C.SM_IOC_Rock_C"),
                  TEXT("/Engine/BasicShapes/Sphere.Sphere") },
                0.036f, FVector2D(0.72f, 1.15f), true, 0.15f, 0.95f, 12.0f, 320.0f);
            IOC_AddScatterLayer(Layers,
                { TEXT("/InstantOrganicCaves/InstantOrganicCaves/Geometry/SM_IOC_Geode.SM_IOC_Geode"),
                  TEXT("/Engine/BasicShapes/Sphere.Sphere") },
                0.009f, FVector2D(0.78f, 1.25f), true, -0.15f, 0.70f, 8.0f, 560.0f);
            break;

        case EIOCCavePreset::Custom:
        default:
            break;
        }

        return Layers;
    }

    static TArray<FIOCScatterLayer> IOC_BuildPreviewDecorationLayers(EIOCCavePreset Preset)
    {
        TArray<FIOCScatterLayer> Layers = IOC_BuildPresetDecorationLayers(Preset);
        if (Layers.Num() > 2)
        {
            Layers.SetNum(2, EAllowShrinking::No);
        }

        for (int32 LayerIdx = 0; LayerIdx < Layers.Num(); ++LayerIdx)
        {
            FIOCScatterLayer& Layer = Layers[LayerIdx];
            const float DensityScale = (LayerIdx == 0) ? 0.42f : 0.32f;
            Layer.Density *= DensityScale;
            Layer.ScaleRange.X *= 0.88f;
            Layer.ScaleRange.Y *= 0.92f;
            Layer.PoissonMinSeparation = FMath::Max(Layer.PoissonMinSeparation * 1.55f, 260.0f);
        }

        return Layers;
    }

    struct FIOCRenderPreviewCaptureState
    {
        TWeakObjectPtr<UWorld> World;
        TArray<TWeakObjectPtr<AActor>> SpawnedActors;
        FTSTicker::FDelegateHandle ScreenshotHandle;
        FTSTicker::FDelegateHandle ExitHandle;
        TWeakObjectPtr<UTextureRenderTarget2D> CaptureRenderTarget;
        FString OutputPath;
        bool bExitOnComplete = false;

        void Reset()
        {
            World.Reset();
            SpawnedActors.Reset();
            ScreenshotHandle.Reset();
            ExitHandle.Reset();
            CaptureRenderTarget.Reset();
            OutputPath.Reset();
            bExitOnComplete = false;
        }
    };

    static FIOCRenderPreviewCaptureState GRenderPreviewCaptureState;

    template<typename T>
    static T* IOCWizardSpawnTransientActor(UWorld* World, const FTransform& Transform)
    {
        if (!World)
        {
            return nullptr;
        }

        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SpawnParams.ObjectFlags |= RF_Transient;
        return World->SpawnActor<T>(Transform.GetLocation(), Transform.Rotator(), SpawnParams);
    }

    static void IOCWizardCleanupRenderedPreviewActors()
    {
        if (GRenderPreviewCaptureState.ScreenshotHandle.IsValid())
        {
            FTSTicker::GetCoreTicker().RemoveTicker(GRenderPreviewCaptureState.ScreenshotHandle);
        }
        if (GRenderPreviewCaptureState.ExitHandle.IsValid())
        {
            FTSTicker::GetCoreTicker().RemoveTicker(GRenderPreviewCaptureState.ExitHandle);
        }

        for (const TWeakObjectPtr<AActor>& Actor : GRenderPreviewCaptureState.SpawnedActors)
        {
            if (Actor.IsValid())
            {
                Actor->Destroy();
            }
        }

        GRenderPreviewCaptureState.Reset();
    }

    static FLevelEditorViewportClient* IOCWizardFindPerspectiveViewport()
    {
        if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->IsPerspective())
        {
            return GCurrentLevelEditingViewportClient;
        }

        if (!GEditor)
        {
            return nullptr;
        }

        for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
        {
            if (ViewportClient && ViewportClient->IsPerspective())
            {
                return ViewportClient;
            }
        }

        return nullptr;
    }

    static void IOCWizardSetEditorViewport(const FVector& Position, const FRotator& Rotation, float FOV)
    {
        if (FLevelEditorViewportClient* ViewportClient = IOCWizardFindPerspectiveViewport())
        {
            ViewportClient->SetRealtime(true);
            ViewportClient->SetGameView(true);
            ViewportClient->ShowWidget(false);
            ViewportClient->EngineShowFlags.SetGrid(false);
            ViewportClient->ViewFOV = FOV;
            ViewportClient->FOVAngle = FOV;
            ViewportClient->SetViewLocation(Position);
            ViewportClient->SetViewRotation(Rotation);
            ViewportClient->Invalidate();
        }
    }

    static bool IOCWizardTryParsePresetName(const FString& PresetName, EIOCCavePreset& OutPreset)
    {
        const FString Canonical = PresetName.Replace(TEXT(" "), TEXT(""))
            .Replace(TEXT("_"), TEXT(""))
            .ToLower();

        if (Canonical == TEXT("largetunnel") || Canonical == TEXT("tunnel"))
        {
            OutPreset = EIOCCavePreset::LargeTunnel;
            return true;
        }
        if (Canonical == TEXT("tightcrawl") || Canonical == TEXT("crawl"))
        {
            OutPreset = EIOCCavePreset::TightCrawl;
            return true;
        }
        if (Canonical == TEXT("opencavern") || Canonical == TEXT("cavern"))
        {
            OutPreset = EIOCCavePreset::OpenCavern;
            return true;
        }
        if (Canonical == TEXT("alienhive") || Canonical == TEXT("hive"))
        {
            OutPreset = EIOCCavePreset::AlienHive;
            return true;
        }
        if (Canonical == TEXT("canyonstrata") || Canonical == TEXT("strata") || Canonical == TEXT("canyon"))
        {
            OutPreset = EIOCCavePreset::CanyonStrata;
            return true;
        }

        return false;
    }

    static FString IOCWizardGetPresetSlug(EIOCCavePreset Preset)
    {
        switch (Preset)
        {
        case EIOCCavePreset::LargeTunnel:
            return TEXT("LargeTunnel");
        case EIOCCavePreset::TightCrawl:
            return TEXT("TightCrawl");
        case EIOCCavePreset::OpenCavern:
            return TEXT("OpenCavern");
        case EIOCCavePreset::AlienHive:
            return TEXT("AlienHive");
        case EIOCCavePreset::CanyonStrata:
            return TEXT("CanyonStrata");
        case EIOCCavePreset::Custom:
        default:
            return TEXT("Custom");
        }
    }

    static void IOCWizardApplyPresetVisualCaptureSettings(AIOCProceduralActor* Cave, EIOCCavePreset Preset)
    {
        if (!Cave)
        {
            return;
        }

        Cave->CavePreset = Preset;
        Cave->ApplyPresetSettingsOnly();
        Cave->CavePreset = EIOCCavePreset::Custom;
        Cave->bGenerateSmartColors = true;
        Cave->TextureTiling = IOC_GetRecommendedTextureTiling(Preset);
        Cave->DecorationLayers = IOC_BuildPresetDecorationLayers(Preset);
        Cave->bEnableLOD = true;
        Cave->LODDistance = 5000.0f;
        Cave->LODVoxelSizeMultiplier = 3.0f;
        Cave->bUseWorldSpaceNoise = false;
        Cave->bUseFixedBoundsForTunnel = false;
        Cave->bAutoRebuildNavMesh = false;
        Cave->bShowDebugViz = false;

        UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/InstantOrganicCaves/MI_IOC_SmartCave_Inst.MI_IOC_SmartCave_Inst"));
        if (!Mat)
        {
            Mat = LoadObject<UMaterialInterface>(nullptr,
                TEXT("/InstantOrganicCaves/MI_IOC_CaveWalls.MI_IOC_CaveWalls"));
        }
        if (Mat)
        {
            Cave->CaveMaterial = Mat;
        }
    }

    struct FIOCPresetRenderRig
    {
        FVector CameraPosition = FVector::ZeroVector;
        FVector CameraTarget = FVector::ForwardVector;
        FVector KeyLightOffset = FVector(250.0f, -180.0f, 140.0f);
        FVector FillLightOffset = FVector(1200.0f, 120.0f, 260.0f);
        FLinearColor KeyColor = FLinearColor(1.0f, 0.78f, 0.62f);
        FLinearColor FillColor = FLinearColor(0.48f, 0.62f, 0.95f);
        float FOV = 65.0f;
        float KeyIntensity = 3000.0f;
        float FillIntensity = 1700.0f;
        float KeyRadius = 2600.0f;
        float FillRadius = 3200.0f;
    };

    static FIOCPresetRenderRig IOCWizardBuildPresetRenderRig(const AIOCProceduralActor* Cave, EIOCCavePreset Preset)
    {
        FIOCPresetRenderRig Rig;
        if (!Cave)
        {
            return Rig;
        }

        const FVector Start = Cave->TunnelStart;
        const FVector End = Cave->TunnelEnd;
        FVector BoundsOrigin = FVector::ZeroVector;
        FVector BoundsExtent = FVector::ZeroVector;
        if (Cave->MeshComponent)
        {
            BoundsOrigin = Cave->MeshComponent->Bounds.Origin;
            BoundsExtent = Cave->MeshComponent->Bounds.BoxExtent;
        }
        if (BoundsExtent.IsNearlyZero() && Cave->LODMeshComponent)
        {
            BoundsOrigin = Cave->LODMeshComponent->Bounds.Origin;
            BoundsExtent = Cave->LODMeshComponent->Bounds.BoxExtent;
        }
        if (BoundsExtent.IsNearlyZero())
        {
            Cave->GetActorBounds(false, BoundsOrigin, BoundsExtent);
        }

        const FVector Dir = (End - Start).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
        FVector Right = FVector::CrossProduct(FVector::UpVector, Dir).GetSafeNormal();
        if (Right.IsNearlyZero())
        {
            Right = FVector::RightVector;
        }
        const bool bHasUsableBounds = !BoundsExtent.IsNearlyZero();
        const FVector LocalCenter = bHasUsableBounds
            ? (BoundsOrigin - Cave->GetActorLocation())
            : ((Start + End) * 0.5f);
        if (!bHasUsableBounds)
        {
            BoundsExtent = Cave->GenerationBounds * 0.5f;
            if (BoundsExtent.IsNearlyZero())
            {
                BoundsExtent = FVector(2200.0f, 900.0f, 900.0f);
            }
            if (Cave->bGenerateTunnel)
            {
                BoundsExtent.X = FMath::Max(BoundsExtent.X, FVector::Distance(Start, End) * 0.5f);
                const float TunnelWidth = FMath::Max(Cave->TunnelRadius * 1.8f, 260.0f);
                BoundsExtent.Y = FMath::Max(BoundsExtent.Y, TunnelWidth);
                BoundsExtent.Z = FMath::Max(BoundsExtent.Z, TunnelWidth * 0.85f);
            }
        }

        const float Length = FMath::Max3(BoundsExtent.X, BoundsExtent.Y, BoundsExtent.Z);
        const float Width = FMath::Max(BoundsExtent.Y, Cave->TunnelRadius * 1.35f);
        const float Height = FMath::Max(BoundsExtent.Z, Cave->TunnelRadius * 0.95f);

        Rig.KeyLightOffset = (-Right * Width * 0.46f) + (Dir * Length * 0.10f) + FVector(0.0f, 0.0f, Height * 0.38f);
        Rig.FillLightOffset = (Right * Width * 0.82f) - (Dir * Length * 0.22f) + FVector(0.0f, 0.0f, Height * 0.52f);
        Rig.KeyRadius = FMath::Max(2000.0f, (Length + Width) * 1.25f);
        Rig.FillRadius = FMath::Max(2600.0f, (Length + Width) * 1.65f);

        switch (Preset)
        {
        case EIOCCavePreset::LargeTunnel:
            Rig.CameraPosition = LocalCenter - (Dir * Length * 0.72f) - (Right * Width * 0.20f) - FVector(0.0f, 0.0f, Height * 0.18f);
            Rig.CameraTarget = LocalCenter + (Dir * Length * 0.20f) + (Right * Width * 0.03f) - FVector(0.0f, 0.0f, Height * 0.06f);
            Rig.FOV = 62.0f;
            Rig.KeyIntensity = 2600.0f;
            Rig.FillIntensity = 1500.0f;
            break;
        case EIOCCavePreset::TightCrawl:
            Rig.CameraPosition = LocalCenter - (Dir * Length * 0.60f) - (Right * Width * 0.10f) - FVector(0.0f, 0.0f, Height * 0.14f);
            Rig.CameraTarget = LocalCenter + (Dir * Length * 0.18f) + (Right * Width * 0.02f) - FVector(0.0f, 0.0f, Height * 0.02f);
            Rig.FOV = 70.0f;
            Rig.KeyIntensity = 1700.0f;
            Rig.FillIntensity = 980.0f;
            break;
        case EIOCCavePreset::OpenCavern:
            Rig.CameraPosition = LocalCenter - (Dir * Length * 0.44f) - (Right * Width * 0.72f) + FVector(0.0f, 0.0f, Height * 0.20f);
            Rig.CameraTarget = LocalCenter + (Dir * Length * 0.12f) + (Right * Width * 0.08f) - FVector(0.0f, 0.0f, Height * 0.04f);
            Rig.FOV = 78.0f;
            Rig.KeyIntensity = 4800.0f;
            Rig.FillIntensity = 2400.0f;
            Rig.KeyRadius = FMath::Max(Rig.KeyRadius, (Length + Width) * 1.7f);
            Rig.FillRadius = FMath::Max(Rig.FillRadius, (Length + Width) * 2.1f);
            break;
        case EIOCCavePreset::AlienHive:
            Rig.CameraPosition = LocalCenter - (Dir * Length * 0.42f) - (Right * Width * 0.36f) + FVector(0.0f, 0.0f, Height * 0.04f);
            Rig.CameraTarget = LocalCenter + (Dir * Length * 0.14f) + (Right * Width * 0.12f) + FVector(0.0f, 0.0f, Height * 0.03f);
            Rig.FOV = 68.0f;
            Rig.KeyColor = FLinearColor(0.52f, 0.95f, 0.65f);
            Rig.FillColor = FLinearColor(0.24f, 0.54f, 0.98f);
            Rig.KeyIntensity = 3200.0f;
            Rig.FillIntensity = 2100.0f;
            break;
        case EIOCCavePreset::CanyonStrata:
            Rig.CameraPosition = LocalCenter - (Dir * Length * 0.34f) - (Right * Width * 0.82f) + FVector(0.0f, 0.0f, Height * 0.28f);
            Rig.CameraTarget = LocalCenter + (Dir * Length * 0.16f) + FVector(0.0f, 0.0f, -Height * 0.10f);
            Rig.FOV = 76.0f;
            Rig.KeyColor = FLinearColor(1.0f, 0.70f, 0.46f);
            Rig.FillColor = FLinearColor(0.76f, 0.82f, 0.98f);
            Rig.KeyIntensity = 3400.0f;
            Rig.FillIntensity = 1900.0f;
            break;
        case EIOCCavePreset::Custom:
        default:
            Rig.CameraPosition = LocalCenter - (Dir * Length * 0.55f) - (Right * Width * 0.20f) + FVector(0.0f, 0.0f, Height * 0.10f);
            Rig.CameraTarget = LocalCenter + (Dir * Length * 0.12f);
            break;
        }

        return Rig;
    }

    static void IOCWizardSpawnRenderEnvironment(UWorld* World, const FVector& Origin, const FIOCPresetRenderRig& Rig)
    {
        if (!World)
        {
            return;
        }

        if (ADirectionalLight* Sun = IOCWizardSpawnTransientActor<ADirectionalLight>(World, FTransform(FRotator(-55.0f, -32.0f, 0.0f), Origin + FVector(0.0f, 0.0f, 4000.0f))))
        {
            if (UDirectionalLightComponent* LightComponent = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
            {
                LightComponent->SetIntensity(3.2f);
                LightComponent->CastShadows = true;
            }
            GRenderPreviewCaptureState.SpawnedActors.Add(Sun);
        }

        if (ASkyLight* Sky = IOCWizardSpawnTransientActor<ASkyLight>(World, FTransform(FRotator::ZeroRotator, Origin + FVector(0.0f, 0.0f, 2000.0f))))
        {
            if (USkyLightComponent* LightComponent = Sky->GetLightComponent())
            {
                LightComponent->SetRealTimeCapture(true);
                LightComponent->SetIntensity(0.35f);
            }
            GRenderPreviewCaptureState.SpawnedActors.Add(Sky);
        }

        if (AExponentialHeightFog* Fog = IOCWizardSpawnTransientActor<AExponentialHeightFog>(World, FTransform(FRotator::ZeroRotator, Origin)))
        {
            if (UExponentialHeightFogComponent* FogComponent = Fog->GetComponent())
            {
                FogComponent->SetFogDensity(0.012f);
                FogComponent->SetFogHeightFalloff(0.18f);
                FogComponent->SetFogInscatteringColor(FLinearColor(0.08f, 0.09f, 0.12f));
            }
            GRenderPreviewCaptureState.SpawnedActors.Add(Fog);
        }

        if (APostProcessVolume* PPV = IOCWizardSpawnTransientActor<APostProcessVolume>(World, FTransform(FRotator::ZeroRotator, Origin)))
        {
            PPV->bUnbound = true;
            PPV->Settings.bOverride_AutoExposureMinBrightness = true;
            PPV->Settings.AutoExposureMinBrightness = 0.6f;
            PPV->Settings.bOverride_AutoExposureMaxBrightness = true;
            PPV->Settings.AutoExposureMaxBrightness = 0.6f;
            PPV->Settings.bOverride_BloomIntensity = true;
            PPV->Settings.BloomIntensity = 0.55f;
            GRenderPreviewCaptureState.SpawnedActors.Add(PPV);
        }

        if (APointLight* KeyLight = IOCWizardSpawnTransientActor<APointLight>(World, FTransform(FRotator::ZeroRotator, Origin + Rig.KeyLightOffset)))
        {
            if (UPointLightComponent* LightComponent = Cast<UPointLightComponent>(KeyLight->GetLightComponent()))
            {
                LightComponent->SetIntensity(Rig.KeyIntensity);
                LightComponent->SetAttenuationRadius(Rig.KeyRadius);
                LightComponent->SetLightColor(Rig.KeyColor);
            }
            GRenderPreviewCaptureState.SpawnedActors.Add(KeyLight);
        }

        if (APointLight* FillLight = IOCWizardSpawnTransientActor<APointLight>(World, FTransform(FRotator::ZeroRotator, Origin + Rig.FillLightOffset)))
        {
            if (UPointLightComponent* LightComponent = Cast<UPointLightComponent>(FillLight->GetLightComponent()))
            {
                LightComponent->SetIntensity(Rig.FillIntensity);
                LightComponent->SetAttenuationRadius(Rig.FillRadius);
                LightComponent->SetLightColor(Rig.FillColor);
            }
            GRenderPreviewCaptureState.SpawnedActors.Add(FillLight);
        }
    }

    static ASceneCapture2D* IOCWizardSpawnSceneCapture(
        UWorld* World,
        AActor* FocusActor,
        const FVector& CameraWorldPos,
        const FRotator& CameraRotation,
        const FIOCPresetRenderRig& Rig)
    {
        ASceneCapture2D* CaptureActor = IOCWizardSpawnTransientActor<ASceneCapture2D>(
            World,
            FTransform(CameraRotation, CameraWorldPos));
        if (!CaptureActor)
        {
            return nullptr;
        }

        USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D();
        if (!CaptureComponent)
        {
            CaptureActor->Destroy();
            return nullptr;
        }

        UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(CaptureActor);
        if (!RenderTarget)
        {
            CaptureActor->Destroy();
            return nullptr;
        }

        RenderTarget->ClearColor = FLinearColor::Black;
        RenderTarget->InitCustomFormat(1920, 1080, PF_B8G8R8A8, false);
        RenderTarget->UpdateResourceImmediate(true);

        CaptureComponent->TextureTarget = RenderTarget;
        CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
        CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
        CaptureComponent->ShowOnlyActors.Reset();
        if (FocusActor)
        {
            CaptureComponent->ShowOnlyActors.Add(FocusActor);
            CaptureComponent->ShowOnlyActorComponents(FocusActor, true);
        }
        CaptureComponent->bCaptureEveryFrame = false;
        CaptureComponent->bCaptureOnMovement = false;
        CaptureComponent->FOVAngle = Rig.FOV;
        CaptureComponent->ShowFlags.SetAtmosphere(false);
        CaptureComponent->ShowFlags.SetFog(false);
        CaptureComponent->ShowFlags.SetCloud(false);
        CaptureComponent->ShowFlags.SetGrid(false);
        CaptureComponent->ShowFlags.SetVolumetricFog(false);

        GRenderPreviewCaptureState.CaptureRenderTarget = RenderTarget;
        return CaptureActor;
    }

    static bool IOCWizardExportRenderTargetToPng(const FString& OutputPath)
    {
        UTextureRenderTarget2D* RenderTarget = GRenderPreviewCaptureState.CaptureRenderTarget.Get();
        if (!RenderTarget || OutputPath.IsEmpty())
        {
            return false;
        }

        FBufferArchive Buffer;
        const bool bExported = FImageUtils::ExportRenderTarget2DAsPNG(RenderTarget, Buffer);
        const bool bSaved = bExported && FFileHelper::SaveArrayToFile(Buffer, *OutputPath);
        Buffer.FlushCache();
        Buffer.Empty();
        return bSaved;
    }
}

// ============================================================================
// Feature 1: Minimal editor viewport for preview
// ============================================================================
class SIOCPreviewViewport : public SEditorViewport
{
public:
    SLATE_BEGIN_ARGS(SIOCPreviewViewport) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        PreviewScene = MakeUnique<FPreviewScene>(FPreviewScene::ConstructionValues());
        SEditorViewport::Construct(SEditorViewport::FArguments());
    }

    UWorld* GetPreviewWorld() const
    {
        return PreviewScene ? PreviewScene->GetWorld() : nullptr;
    }

    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
    {
        TSharedPtr<FEditorViewportClient> ViewportClient = MakeShareable(
            new FEditorViewportClient(nullptr, PreviewScene.Get()));
        ViewportClient->SetViewMode(VMI_Lit);
        ViewportClient->ViewportType = LVT_Perspective;
        ViewportClient->SetViewLocation(FVector(-2200.0f, -2600.0f, 1400.0f));
        ViewportClient->SetViewRotation(FRotator(-18.0f, 42.0f, 0.0f));
        ViewportClient->SetRealtime(true);
        return ViewportClient.ToSharedRef();
    }

private:
    TUniquePtr<FPreviewScene> PreviewScene;
};

// ============================================================================
// Open / Register
// ============================================================================
void SIOCSetupWizard::OpenWizard()
{
    if (IOCWizard::ActiveWindow.IsValid())
    {
        IOCWizard::ActiveWindow.Pin()->RequestDestroyWindow();
    }

    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(INVTEXT("Instant Organic Caves - Setup Wizard"))
        .ClientSize(FVector2D(1080, 820))
        .SupportsMinimize(false)
        .SupportsMaximize(false)
        .SizingRule(ESizingRule::UserSized)
        .AutoCenter(EAutoCenter::PreferredWorkArea);

    TSharedRef<SIOCSetupWizard> Wizard = SNew(SIOCSetupWizard)
        .ParentWindow(Window);

    Window->SetContent(Wizard);

    Window->GetOnWindowClosedEvent().AddLambda([](const TSharedRef<SWindow>&) {
        IOCWizard::ActiveWindow.Reset();
    });

    FSlateApplication::Get().AddWindow(Window);
    IOCWizard::ActiveWindow = Window;
}

bool SIOCSetupWizard::CaptureRenderedPresetPreview(const FString& PresetName, bool bExitOnComplete)
{
    UWorld* World = IOCWizard::GetEditorWorld();
    if (!World)
    {
        return false;
    }

    EIOCCavePreset Preset = EIOCCavePreset::Custom;
    if (!IOCWizardTryParsePresetName(PresetName, Preset) || Preset == EIOCCavePreset::Custom)
    {
        UE_LOG(LogIOCEditor, Warning, TEXT("Unsupported setup wizard preset capture request '%s'."), *PresetName);
        return false;
    }

    IOCWizardCleanupRenderedPreviewActors();

    const FVector SpawnOrigin(200000.0f, 0.0f, 0.0f);

    AIOCProceduralActor* Cave = World->SpawnActorDeferred<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(),
        FTransform(FRotator::ZeroRotator, SpawnOrigin),
        nullptr,
        nullptr,
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

    if (!Cave)
    {
        UE_LOG(LogIOCEditor, Warning, TEXT("Failed to spawn rendered preset capture cave for '%s'."), *PresetName);
        return false;
    }

    IOCWizardApplyPresetVisualCaptureSettings(Cave, Preset);
    Cave->FinishSpawning(FTransform(FRotator::ZeroRotator, SpawnOrigin));
    GRenderPreviewCaptureState.World = World;
    GRenderPreviewCaptureState.SpawnedActors.Add(Cave);
    GRenderPreviewCaptureState.bExitOnComplete = bExitOnComplete;

    const FIOCPresetRenderRig Rig = IOCWizardBuildPresetRenderRig(Cave, Preset);
    IOCWizardSpawnRenderEnvironment(World, SpawnOrigin, Rig);

    const FVector CameraWorldPos = SpawnOrigin + Rig.CameraPosition;
    const FVector CameraWorldTarget = SpawnOrigin + Rig.CameraTarget;
    const FRotator CameraRotation = (CameraWorldTarget - CameraWorldPos).Rotation();
    IOCWizardSetEditorViewport(CameraWorldPos, CameraRotation, Rig.FOV);

    const FString OutputDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("IOCPreviewShots"));
    IFileManager::Get().MakeDirectory(*OutputDir, true);
    GRenderPreviewCaptureState.OutputPath = FPaths::Combine(
        OutputDir,
        FString::Printf(TEXT("Wizard_%s.png"), *IOCWizardGetPresetSlug(Preset)));
    UE_LOG(LogIOCEditor, Log, TEXT("Starting rendered setup wizard preset capture '%s' -> %s"),
        *PresetName,
        *GRenderPreviewCaptureState.OutputPath);

    Cave->OnGenerationFinished.AddLambda([Preset](AIOCProceduralActor* Actor, bool bCancelled, bool bWillRegenerate)
    {
        if (!Actor || bCancelled || bWillRegenerate)
        {
            return;
        }

        const FIOCPresetRenderRig Rig = IOCWizardBuildPresetRenderRig(Actor, Preset);
        const FVector SpawnOrigin = Actor->GetActorLocation();
        const FVector CameraWorldPos = SpawnOrigin + Rig.CameraPosition;
        const FVector CameraWorldTarget = SpawnOrigin + Rig.CameraTarget;
        const FRotator CameraRotation = (CameraWorldTarget - CameraWorldPos).Rotation();
        IOCWizardSetEditorViewport(CameraWorldPos, CameraRotation, Rig.FOV);

        GRenderPreviewCaptureState.ScreenshotHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([CameraWorldPos, CameraRotation, Rig](float)
            {
                if (!GRenderPreviewCaptureState.OutputPath.IsEmpty())
                {
                    AIOCProceduralActor* FocusActor = nullptr;
                    for (const TWeakObjectPtr<AActor>& SpawnedActor : GRenderPreviewCaptureState.SpawnedActors)
                    {
                        if (AIOCProceduralActor* CaveActor = Cast<AIOCProceduralActor>(SpawnedActor.Get()))
                        {
                            FocusActor = CaveActor;
                            break;
                        }
                    }

                    if (ASceneCapture2D* CaptureActor = IOCWizardSpawnSceneCapture(
                        GRenderPreviewCaptureState.World.Get(),
                        FocusActor,
                        CameraWorldPos,
                        CameraRotation,
                        Rig))
                    {
                        GRenderPreviewCaptureState.SpawnedActors.Add(CaptureActor);
                        if (USceneCaptureComponent2D* CaptureComponent = CaptureActor->GetCaptureComponent2D())
                        {
                            CaptureComponent->CaptureScene();
                        }
                    }
                }

                GRenderPreviewCaptureState.ExitHandle = FTSTicker::GetCoreTicker().AddTicker(
                    FTickerDelegate::CreateLambda([](float)
                    {
                        const bool bSaved = IOCWizardExportRenderTargetToPng(GRenderPreviewCaptureState.OutputPath);
                        if (bSaved)
                        {
                            UE_LOG(LogIOCEditor, Log, TEXT("Rendered preset capture saved -> %s"),
                                *GRenderPreviewCaptureState.OutputPath);
                        }
                        else
                        {
                            UE_LOG(LogIOCEditor, Warning, TEXT("Rendered preset capture failed -> %s"),
                                *GRenderPreviewCaptureState.OutputPath);
                        }
                        if (GRenderPreviewCaptureState.bExitOnComplete)
                        {
                            FPlatformMisc::RequestExit(false);
                        }
                        return false;
                    }),
                    0.55f);

                return false;
            }),
            0.85f);
    });

    Cave->GenerateCave();
    return true;
}

void SIOCSetupWizard::AddReferencedObjects(FReferenceCollector& Collector)
{
    // FIOCSetupWizardRollbackSnapshot is a plain struct on a Slate widget, so its UObject
    // pointers are invisible to the GC. Report them for as long as the snapshot is live.
    if (!RollbackSnapshot.bValid)
    {
        return;
    }

    if (RollbackSnapshot.CaveMaterial)
    {
        Collector.AddReferencedObject(RollbackSnapshot.CaveMaterial);
    }

    for (FIOCScatterLayer& Layer : RollbackSnapshot.DecorationLayers)
    {
        if (Layer.Mesh)
        {
            Collector.AddReferencedObject(Layer.Mesh);
        }
    }
}

SIOCSetupWizard::~SIOCSetupWizard()
{
    UnbindPreviewActorDelegates();
    UnbindConfiguredCaveDelegates();
    if (WorldCleanupHandle.IsValid())
    {
        FWorldDelegates::OnPostWorldCleanup.Remove(WorldCleanupHandle);
        WorldCleanupHandle.Reset();
    }

    DestroyPreviewActor();
}

// ============================================================================
// Construct
// ============================================================================
void SIOCSetupWizard::Construct(const FArguments& InArgs)
{
    ParentWindow = InArgs._ParentWindow;

    // Feature 11: Load persisted settings
    LoadSettings();

    // Feature 7: Detect existing actors
    DetectExistingCaves();

    // Feature 10: Register cleanup
    RegisterCleanupHandler();

    // The whole wizard sits on a dim cave backdrop. Without something behind them, making
    // the panels translucent would only reveal flat colour -- the backdrop is what the
    // translucency is for.
    //
    // Three layers, bottom to top: the blurred art, a vignette that pulls the window edges
    // down so the stepper and the nav row are never fighting a bright patch, then the
    // translucent PanelBg that every page sits on. The art is drawn near-opaque here and
    // knocked back by those two layers rather than being faded itself -- fading it first
    // and then covering it left nothing visible at all.
    ChildSlot
    [
        SNew(SOverlay)

        + SOverlay::Slot()
        .HAlign(HAlign_Fill)
        .VAlign(VAlign_Fill)
        [
            SNew(SScaleBox)
            .Stretch(EStretch::ScaleToFill)
            .StretchDirection(EStretchDirection::Both)
            .Clipping(EWidgetClipping::ClipToBounds)
            [
                SNew(SImage)
                .Image(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::BackdropBrush))
                .ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.85f))
            ]
        ]

        + SOverlay::Slot()
        .HAlign(HAlign_Fill)
        .VAlign(VAlign_Fill)
        [
            SNew(SImage)
            .Image(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::VignetteBrush))
            .ColorAndOpacity(FLinearColor(0.006f, 0.016f, 0.022f, 1.0f))
        ]

        + SOverlay::Slot()
        .HAlign(HAlign_Fill)
        .VAlign(VAlign_Fill)
        [
        SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(IOCWizard::AccentColor)
            .Padding(FMargin(0, 3, 0, 0))
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(IOCWizard::PanelBg)
                .Padding(0)
            ]
        ]

        + SVerticalBox::Slot()
        .FillHeight(1.0f)
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(IOCWizard::PanelBg)
            .Padding(0)
            [
                SNew(SVerticalBox)

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(36, 22, 36, 0))
                [
                    BuildStepIndicator()
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(36, 14, 36, 0))
                [
                    SNew(SSeparator)
                    .SeparatorImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .ColorAndOpacity(FLinearColor(0.12f, 0.12f, 0.15f, 1.0f))
                ]

                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                .Padding(FMargin(36, 20, 36, 20))
                [
                    SNew(SScrollBox)
                    .ScrollBarStyle(FCoreStyle::Get(), "ScrollBar")
                    + SScrollBox::Slot()
                    [
                        SAssignNew(PageSwitcher, SWidgetSwitcher)
                        + SWidgetSwitcher::Slot() [ BuildWelcomePage() ]
                        + SWidgetSwitcher::Slot() [ BuildPresetPage() ]
                        + SWidgetSwitcher::Slot() [ BuildEnvironmentPage() ]
                        + SWidgetSwitcher::Slot() [ BuildAdvancedPage() ]
                        + SWidgetSwitcher::Slot() [ BuildFinishPage() ]
                    ]
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(FMargin(36, 0, 36, 22))
                [
                    BuildNavigation()
                ]
            ]
        ]
        ]   // close the content slot of the backdrop overlay
    ];

    ActiveTimerHandle = RegisterActiveTimer(0.1f,
        FWidgetActiveTimerDelegate::CreateSP(this, &SIOCSetupWizard::HandleActiveTimer));
    RequestPreviewRefresh(0.0);
    RequestValidationRefresh(0.0);
    RequestExistingCaveRefresh(0.0);
}

void SIOCSetupWizard::RequestPreviewRefresh(double DelaySeconds)
{
    ViewModel.bPendingPreviewRefresh = true;
    ViewModel.NextPreviewRefreshTime = FPlatformTime::Seconds() + FMath::Max(0.0, DelaySeconds);
    PreviewStatus = TEXT("Preview refresh queued...");
}

void SIOCSetupWizard::RequestValidationRefresh(double DelaySeconds)
{
    ViewModel.bValidationDirty = true;
    ViewModel.NextValidationRefreshTime = FPlatformTime::Seconds() + FMath::Max(0.0, DelaySeconds);
}

void SIOCSetupWizard::RequestExistingCaveRefresh(double DelaySeconds)
{
    ViewModel.bPendingExistingCaveRefresh = true;
    ViewModel.NextExistingCaveRefreshTime = FPlatformTime::Seconds() + FMath::Max(0.0, DelaySeconds);
}

uint32 SIOCSetupWizard::BuildPreviewStateSignature() const
{
    uint32 Hash = 0;
    auto Add = [&Hash](uint32 Value)
    {
        Hash = HashCombine(Hash, Value);
    };

    Add(GetTypeHash(SelectedPresetIndex));
    Add(GetTypeHash(bIsCustom));
    Add(GetTypeHash(bCustomTunnelMode));
    Add(GetTypeHash(bCustomUseSpline));
    Add(GetTypeHash(CustomSeed));
    Add(GetTypeHash(CustomBounds));
    Add(GetTypeHash(CustomTunnelStart));
    Add(GetTypeHash(CustomTunnelEnd));
    Add(GetTypeHash((float)CustomVoxelSize));
    Add(GetTypeHash(CustomTunnelRadius));
    Add(GetTypeHash(CustomWallThickness));
    Add(GetTypeHash((float)CustomNoiseFrequency));
    Add(GetTypeHash(CustomSmoothIterations));
    Add(GetTypeHash(CustomDomainWarpIntensity));
    Add(GetTypeHash(CustomTerraceSteps));
    Add(GetTypeHash(CustomTextureTiling));
    Add(GetTypeHash(WizardSettings.bEnableLOD));
    Add(GetTypeHash(CustomLODDistance));
    Add(GetTypeHash((float)CustomLODMultiplier));
    Add(GetTypeHash(WizardSettings.bUseWorldSpaceNoise));
    Add(GetTypeHash(WizardSettings.bUseFixedBoundsForTunnel));
    Add(GetTypeHash(bPreviewFullFidelity));
    Add(GetTypeHash(WizardSettings.bGenerateSmartColors));
    Add(GetTypeHash(bReconfigureExisting));
    Add(GetTypeHash(SelectedExistingIndex));

    if (bReconfigureExisting && ExistingCaves.IsValidIndex(SelectedExistingIndex) && ExistingCaves[SelectedExistingIndex].IsValid())
    {
        const AIOCProceduralActor* Existing = ExistingCaves[SelectedExistingIndex].Get();
        Add(GetTypeHash(Existing->GetUniqueID()));
        Add(GetTypeHash(Existing->GetActorLocation()));
        Add(GetTypeHash(Existing->DecorationLayers.Num()));
    }

    return Hash;
}

uint32 SIOCSetupWizard::BuildValidationSignature() const
{
    uint32 Hash = BuildExistingCaveSignature();
    auto Add = [&Hash](uint32 Value)
    {
        Hash = HashCombine(Hash, Value);
    };

    const TSharedPtr<IPlugin> IOCPlugin = IPluginManager::Get().FindPlugin(TEXT("InstantOrganicCaves"));
    const TSharedPtr<IPlugin> PythonPlugin = IPluginManager::Get().FindPlugin(TEXT("PythonScriptPlugin"));
    const TSharedPtr<IPlugin> PCGPlugin = IPluginManager::Get().FindPlugin(TEXT("PCG"));

    Add(GetTypeHash(IOCPlugin.IsValid() && IOCPlugin->IsEnabled()));
    Add(GetTypeHash(PythonPlugin.IsValid() && PythonPlugin->IsEnabled()));
    Add(GetTypeHash(PCGPlugin.IsValid() && PCGPlugin->IsEnabled()));
    Add(GetTypeHash(FPackageName::DoesPackageExist(IOCWizard::StarterLevelPackagePath)));
    Add(GetTypeHash(FPackageName::DoesPackageExist(TEXT("/InstantOrganicCaves/MI_IOC_CaveWalls"))));
    Add(GetTypeHash(LoadObject<UObject>(nullptr, IOCWizard::StarterBlueprintObjectPath) != nullptr));
    Add(GetTypeHash(LoadObject<UObject>(nullptr, IOCWizard::StarterMaterialInstanceObjectPath) != nullptr));
    return Hash;
}

uint32 SIOCSetupWizard::BuildExistingCaveSignature() const
{
    UWorld* World = IOCWizard::GetEditorWorld();
    if (!World)
    {
        return 0;
    }

    uint32 Hash = 0;
    for (TActorIterator<AIOCProceduralActor> It(World); It; ++It)
    {
        Hash = HashCombine(Hash, GetTypeHash(It->GetUniqueID()));
        Hash = HashCombine(Hash, GetTypeHash(It->GetActorLocation()));
    }
    return Hash;
}

void SIOCSetupWizard::RefreshValidationState(bool bForce)
{
    if (!bForce && CurrentPage != 3)
    {
        return;
    }

    bool bAllOk = false;
    InstallationStatus = BuildInstallationValidationReport(bAllOk);
    ViewModel.bValidationPassed = bAllOk;
    ViewModel.bValidationDirty = false;
}

bool SIOCSetupWizard::CanRunSetup() const
{
    return !bIsGenerating && ViewModel.bValidationPassed;
}

bool SIOCSetupWizard::CanAdvanceFromCurrentPage() const
{
    if (bIsGenerating && CurrentPage != TotalPages - 1)
    {
        return false;
    }

    if (CurrentPage == 3)
    {
        return CanRunSetup();
    }

    return true;
}

void SIOCSetupWizard::SetCurrentPage(int32 NewPage)
{
    const int32 ClampedPage = FMath::Clamp(NewPage, 0, TotalPages - 1);
    if (bIsGenerating && ClampedPage != TotalPages - 1)
    {
        return;
    }

    if (CurrentPage == ClampedPage && PageSwitcher.IsValid() &&
        PageSwitcher->GetActiveWidgetIndex() == ClampedPage)
    {
        return;
    }

    CurrentPage = ClampedPage;
    if (PageSwitcher.IsValid())
    {
        PageSwitcher->SetActiveWidgetIndex(CurrentPage);
    }

    if (CurrentPage == 1)
    {
        RequestPreviewRefresh(0.0);
    }
    else if (CurrentPage == 3)
    {
        RefreshValidationState(true);
    }
}

FIOCPreflightEstimate SIOCSetupWizard::BuildPreflightEstimate() const
{
    FIOCPreflightEstimate Estimate;
    FVector EstimateBounds = CustomBounds;
    double EstimateVoxelSize = FMath::Max(CustomVoxelSize, 1.0);
    const EIOCCavePreset VisualPreset = IOC_GetStylePresetForSelection(SelectedPresetIndex);

    if (!bIsCustom && SelectedPresetIndex < IOCWizard::CustomIndex)
    {
        switch (SelectedPresetIndex)
        {
        case 0: EstimateBounds = FVector(5200.0f, 1300.0f, 1300.0f); EstimateVoxelSize = 40.0; break;
        case 1: EstimateBounds = FVector(3600.0f, 520.0f, 520.0f); EstimateVoxelSize = 20.0; break;
        case 2: EstimateBounds = FVector(5200.0f, 3200.0f, 2600.0f); EstimateVoxelSize = 60.0; break;
        case 3: EstimateBounds = FVector(4400.0f, 1300.0f, 1300.0f); EstimateVoxelSize = 30.0; break;
        case 4: EstimateBounds = FVector(5200.0f, 1900.0f, 1700.0f); EstimateVoxelSize = 50.0; break;
        default: break;
        }
    }
    else if (bCustomTunnelMode && !WizardSettings.bUseFixedBoundsForTunnel)
    {
        Estimate.PathLength = FMath::Max((CustomTunnelEnd - CustomTunnelStart).Size(), 1000.0);
        const double Width = FMath::Max((CustomTunnelRadius + CustomWallThickness) * 2.6f, 500.0f);
        EstimateBounds = FVector(Estimate.PathLength + Width, Width, Width);
    }

    auto CellsForAxis = [EstimateVoxelSize](double AxisSize) -> int64
    {
        const double Cells = FMath::Clamp(AxisSize / EstimateVoxelSize, 1.0, 1000000.0);
        return (int64)FMath::CeilToDouble(Cells);
    };

    Estimate.EstimatedVoxels = CellsForAxis(EstimateBounds.X) * CellsForAxis(EstimateBounds.Y) * CellsForAxis(EstimateBounds.Z);
    Estimate.EstimatedMillions = (double)Estimate.EstimatedVoxels / 1000000.0;
    Estimate.ScatterLayerCount = IOC_GetRecommendedScatterLayerCount(VisualPreset);

    if (bReconfigureExisting && ExistingCaves.IsValidIndex(SelectedExistingIndex) && ExistingCaves[SelectedExistingIndex].IsValid())
    {
        const AIOCProceduralActor* Existing = ExistingCaves[SelectedExistingIndex].Get();
        Estimate.ScatterLayerCount = FMath::Max(Estimate.ScatterLayerCount, Existing->DecorationLayers.Num());
        if (Existing->LastScatterInstanceCount > 0)
        {
            Estimate.ComplexityMultiplier += FMath::Clamp((double)Existing->LastScatterInstanceCount / 5000.0, 0.0, 0.35);
        }
    }

    Estimate.ComplexityMultiplier += FMath::Clamp((double)CustomSmoothIterations * 0.08, 0.0, 0.8);
    Estimate.ComplexityMultiplier += WizardSettings.bEnableLOD ? FMath::Clamp((double)CustomLODMultiplier * 0.12, 0.0, 0.75) : 0.0;
    Estimate.ComplexityMultiplier += bPreviewFullFidelity ? 0.15 : 0.0;
    Estimate.ComplexityMultiplier += WizardSettings.bUseWorldSpaceNoise ? 0.05 : 0.0;
    Estimate.ComplexityMultiplier += (CustomDomainWarpIntensity > 0.0f) ? FMath::Clamp((double)CustomDomainWarpIntensity / 300.0, 0.0, 0.45) : 0.0;
    Estimate.ComplexityMultiplier += (CustomTerraceSteps > 0.0f) ? 0.12 : 0.0;
    Estimate.ComplexityMultiplier += Estimate.PathLength > 0.0 ? FMath::Clamp(Estimate.PathLength / 8000.0, 0.0, 0.35) : 0.0;
    Estimate.ComplexityMultiplier += Estimate.ScatterLayerCount * 0.08;
    Estimate.ComplexityScore = (double)Estimate.EstimatedVoxels * Estimate.ComplexityMultiplier;

    if (Estimate.ComplexityScore > 9000000.0)
    {
        Estimate.RiskLabel = TEXT("Very high");
    }
    else if (Estimate.ComplexityScore > 3500000.0)
    {
        Estimate.RiskLabel = TEXT("High");
    }
    else if (Estimate.ComplexityScore > 1200000.0)
    {
        Estimate.RiskLabel = TEXT("Moderate");
    }
    else
    {
        Estimate.RiskLabel = TEXT("Low");
    }

    return Estimate;
}

void SIOCSetupWizard::BuildGenerationSummary()
{
    GenerationSummary = FString::Printf(
        TEXT("%s has been applied using the %s style.\n\n"),
        ViewModel.bLastSetupReconfiguredExisting ? TEXT("An existing cave actor") : TEXT("A new cave actor"),
        *GetSelectedPresetName());

    GenerationSummary += FString::Printf(TEXT("Lighting setup: %s\n"),
        bAddLighting ? TEXT("enabled") : TEXT("skipped"));
    GenerationSummary += FString::Printf(TEXT("Playtest character: %s\n"),
        bAddPlayer ? TEXT("spawned") : TEXT("skipped"));
    GenerationSummary += FString::Printf(TEXT("Smart colors: %s\n"),
        WizardSettings.bGenerateSmartColors ? TEXT("enabled") : TEXT("disabled"));
    GenerationSummary += FString::Printf(TEXT("LOD mesh: %s\n"),
        WizardSettings.bEnableLOD ? TEXT("enabled") : TEXT("disabled"));

    if (bIsCustom)
    {
        GenerationSummary += FString::Printf(TEXT("Custom bounds: %s\n"), *CustomBounds.ToString());
    }

    if (LastConfiguredCave.IsValid())
    {
        GenerationSummary += bGenerationDone
            ? TEXT("\nThe cave is ready in the level. Use the quick actions below to keep shaping gameplay and streaming around it.")
            : TEXT("\nThe cave actor is configured in the level. Generation is still running asynchronously, and the wizard will update as soon as final metrics land.");
    }
}

EActiveTimerReturnType SIOCSetupWizard::HandleActiveTimer(double InCurrentTime, float InDeltaTime)
{
    const double Now = FPlatformTime::Seconds();
    const uint32 PreviewSignature = BuildPreviewStateSignature();
    if (PreviewSignature != ViewModel.LastPreviewSettingsSignature)
    {
        ViewModel.LastPreviewSettingsSignature = PreviewSignature;
        RequestPreviewRefresh(0.25);
    }

    const uint32 ExistingSignature = BuildExistingCaveSignature();
    if (ExistingSignature != ViewModel.LastExistingCaveSignature)
    {
        ViewModel.LastExistingCaveSignature = ExistingSignature;
        RequestExistingCaveRefresh(0.0);
        RequestValidationRefresh(0.0);
    }

    if (CurrentPage == 3)
    {
        const uint32 ValidationSignature = BuildValidationSignature();
        if (ValidationSignature != ViewModel.LastValidationSignature)
        {
            ViewModel.LastValidationSignature = ValidationSignature;
            RequestValidationRefresh(0.0);
        }
    }

    if (ViewModel.bPendingExistingCaveRefresh && Now >= ViewModel.NextExistingCaveRefreshTime)
    {
        DetectExistingCaves();
        ViewModel.bPendingExistingCaveRefresh = false;
    }

    if (ViewModel.bValidationDirty && Now >= ViewModel.NextValidationRefreshTime)
    {
        RefreshValidationState(CurrentPage == 3);
    }

    if (ViewModel.bPendingPreviewRefresh && CurrentPage == 1 && Now >= ViewModel.NextPreviewRefreshTime)
    {
        ViewModel.bPendingPreviewRefresh = false;
        UpdatePreviewActor();
    }

    return EActiveTimerReturnType::Continue;
}

// ============================================================================
// Step indicator
// ============================================================================
TSharedRef<SWidget> SIOCSetupWizard::BuildStepIndicator()
{
    TSharedPtr<SVerticalBox> Root;
    SAssignNew(Root, SVerticalBox);

    Root->AddSlot().AutoHeight()
    [
        SNew(SHorizontalBox)
        // The arch mark rides alongside the step title on every page, so branding carries
        // through the wizard without repeating the full banner from the Welcome page.
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(FMargin(0, 0, 12, 0))
        [
            SNew(SBox)
            .WidthOverride(44.0f)
            .HeightOverride(44.0f)
            [
                SNew(SImage)
                .Image(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::EmblemBrush))
            ]
        ]
        + SHorizontalBox::Slot()
        .VAlign(VAlign_Center)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock)
                .Text(INVTEXT("Setup Progress"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
                .ColorAndOpacity(IOCWizard::TextPrimary)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 4, 0, 0))
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText {
                    return FText::FromString(FString::Printf(
                        TEXT("%s: %s"),
                        IOCWizard::StepMeta[FMath::Clamp(CurrentPage, 0, TotalPages - 1)].Name,
                        IOCWizard::StepMeta[FMath::Clamp(CurrentPage, 0, TotalPages - 1)].Subtitle));
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                .ColorAndOpacity(IOCWizard::TextDim)
            ]
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [
            IOCWizard::MakeBadge(
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return FText::FromString(FString::Printf(TEXT("Step %d / %d"), CurrentPage + 1, TotalPages));
                }),
                FLinearColor(0.07f, 0.09f, 0.12f),
                IOCWizard::AccentColor,
                10,
                FMargin(12.0f, 6.0f))
        ]
    ];

    TSharedPtr<SHorizontalBox> HBox;
    SAssignNew(HBox, SHorizontalBox);

    for (int32 i = 0; i < TotalPages; i++)
    {
        if (i > 0)
        {
            HBox->AddSlot()
            .FillWidth(1.0f)
            .Padding(FMargin(8, 0, 8, 0))
            .VAlign(VAlign_Center)
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this, i]() -> FLinearColor {
                    return (i <= CurrentPage) ? FLinearColor(0.38f, 0.33f, 0.22f) : FLinearColor(0.12f, 0.12f, 0.15f);
                })
                .Padding(FMargin(0, 1, 0, 0))
            ];
        }

        HBox->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
            SNew(SButton)
            .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
            .OnClicked_Lambda([this, i]() -> FReply {
                SetCurrentPage(i);
                return FReply::Handled();
            })
            .ToolTipText(FText::Format(INVTEXT("Jump to step {0}: {1}"), FText::AsNumber(i + 1), FText::FromString(IOCWizard::StepMeta[i].Name)))
            .Content()
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 8, 0))
                    [
                        SNew(SBorder)
                        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                        .BorderBackgroundColor_Lambda([this, i]() -> FLinearColor {
                            if (i == CurrentPage) return IOCWizard::AccentColor;
                            if (i < CurrentPage) return FLinearColor(0.34f, 0.66f, 0.43f);
                            return FLinearColor(0.15f, 0.15f, 0.2f);
                        })
                        .Padding(0)
                        [
                            SNew(SBox)
                            .WidthOverride(26)
                            .HeightOverride(26)
                            .HAlign(HAlign_Center)
                            .VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                .Text_Lambda([this, i]() -> FText {
                                    if (i < CurrentPage) return INVTEXT("Done");
                                    return FText::AsNumber(i + 1);
                                })
                                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                                .ColorAndOpacity_Lambda([this, i]() -> FSlateColor {
                                    return (i <= CurrentPage) ? FLinearColor::White : FLinearColor(0.42f, 0.42f, 0.48f);
                                })
                            ]
                        ]
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(IOCWizard::StepMeta[i].Name))
                            .Font(FCoreStyle::GetDefaultFontStyle(i == CurrentPage ? "Bold" : "Regular", 11))
                            .ColorAndOpacity_Lambda([this, i]() -> FSlateColor {
                                if (i == CurrentPage) return IOCWizard::TextPrimary;
                                if (i < CurrentPage) return IOCWizard::TextSecondary;
                                return IOCWizard::TextDim;
                            })
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 2, 0, 0))
                        [
                            SNew(STextBlock)
                            .Text(FText::FromString(IOCWizard::StepMeta[i].Subtitle))
                            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                            .ColorAndOpacity_Lambda([this, i]() -> FSlateColor {
                                return (i == CurrentPage) ? IOCWizard::AccentDim : IOCWizard::TextDim;
                            })
                        ]
                    ]
                ]
            ]
        ];
    }

    Root->AddSlot().AutoHeight().Padding(FMargin(0, 12, 0, 0))
    [
        HBox.ToSharedRef()
    ];

    return Root.ToSharedRef();
}

// ============================================================================
// Navigation bar (Feature 12: keyboard support is via OnKeyDown)
// ============================================================================
TSharedRef<SWidget> SIOCSetupWizard::BuildNavigation()
{
    // Built as a local so the action row below keeps its own indentation; the publisher
    // strip is stacked under it rather than squeezed into the same row, where it would
    // read as one more control.
    const TSharedRef<SWidget> Actions =
    SNew(SHorizontalBox)
    + SHorizontalBox::Slot()
    .AutoWidth()
    .VAlign(VAlign_Center)
    [
        SNew(SButton)
        .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
        .OnClicked(this, &SIOCSetupWizard::OnCancel)
        .Visibility_Lambda([this]() -> EVisibility {
            return CurrentPage < TotalPages - 1 ? EVisibility::Visible : EVisibility::Collapsed;
        })
        .Content()
        [
            IOCWizard::MakeActionLabel(INVTEXT("Cancel"), IOCWizard::TextSecondary)
        ]
    ]

    // Feature 3: Open Documentation
    + SHorizontalBox::Slot()
    .AutoWidth()
    .Padding(FMargin(8, 0, 0, 0))
    .VAlign(VAlign_Center)
    [
        SNew(SButton)
        .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
        .OnClicked(this, &SIOCSetupWizard::OnOpenDocumentation)
        .Content()
        [
            IOCWizard::MakeActionLabel(INVTEXT("Open Docs"), IOCWizard::TextSecondary)
        ]
    ]

    // Feature 10: Undo last setup
    + SHorizontalBox::Slot()
    .AutoWidth()
    .Padding(FMargin(8, 0, 0, 0))
    .VAlign(VAlign_Center)
    [
        SNew(SBorder)
        .Visibility_Lambda([this]() -> EVisibility {
            return SpawnedActors.Num() > 0 ? EVisibility::Visible : EVisibility::Hidden;
        })
        [
            SNew(SButton)
            .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonDanger))
            .OnClicked(this, &SIOCSetupWizard::OnUndoLastSetup)
            .Content()
            [
                IOCWizard::MakeActionLabel(INVTEXT("Undo Added Actors"), FLinearColor(0.8f, 0.5f, 0.5f))
            ]
        ]
    ]

    + SHorizontalBox::Slot()
    .FillWidth(1.0f)
    .Padding(FMargin(16, 0, 16, 0))
    .VAlign(VAlign_Center)
    [
        SNew(SBorder)
        .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::CardPanel))
        .Padding(FMargin(14, 10))
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .VAlign(VAlign_Center)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() -> FText {
                        return FText::FromString(IOCWizard::StepMeta[FMath::Clamp(CurrentPage, 0, TotalPages - 1)].Name);
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                    .ColorAndOpacity(IOCWizard::TextPrimary)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 3, 0, 0))
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() -> FText {
                        if (bIsGenerating)
                        {
                            return INVTEXT("Generation is running in the editor and the wizard will keep updating.");
                        }
                        if (CurrentPage == 3 && !ViewModel.bValidationPassed)
                        {
                            return INVTEXT("Resolve the readiness issues on this page before generation can start.");
                        }
                        return FText::FromString(IOCWizard::StepMeta[FMath::Clamp(CurrentPage, 0, TotalPages - 1)].Subtitle);
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(IOCWizard::TextDim)
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(16, 0, 0, 0))
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this]() -> FLinearColor {
                    if (bIsGenerating) return FLinearColor(0.08f, 0.07f, 0.03f);
                    if (CurrentPage == 3 && !ViewModel.bValidationPassed) return FLinearColor(0.14f, 0.055f, 0.045f);
                    return FLinearColor(0.05f, 0.08f, 0.06f);
                })
                .Padding(FMargin(10, 5))
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() -> FText {
                        return bIsGenerating
                            ? INVTEXT("Working")
                            : (CurrentPage == 3 && !ViewModel.bValidationPassed ? INVTEXT("Needs fixes") : INVTEXT("Ready"));
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    .ColorAndOpacity_Lambda([this]() -> FSlateColor {
                        if (bIsGenerating) return IOCWizard::AccentColor;
                        if (CurrentPage == 3 && !ViewModel.bValidationPassed) return FLinearColor(0.95f, 0.52f, 0.36f);
                        return FLinearColor(0.45f, 0.84f, 0.56f);
                    })
                ]
            ]
        ]
    ]

    + SHorizontalBox::Slot()
    .AutoWidth()
    .Padding(FMargin(8, 0, 0, 0))
    .VAlign(VAlign_Center)
    [
        SNew(SButton)
        .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
        .OnClicked(this, &SIOCSetupWizard::OnBack)
        .Visibility_Lambda([this]() -> EVisibility {
            return CurrentPage > 0 ? EVisibility::Visible : EVisibility::Hidden;
        })
        .Content()
        [
            IOCWizard::MakeActionLabel(INVTEXT("Back"), IOCWizard::TextSecondary)
        ]
    ]

    + SHorizontalBox::Slot()
    .AutoWidth()
    .Padding(FMargin(8, 0, 0, 0))
    .VAlign(VAlign_Center)
    [
        SNew(SButton)
        .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonPrimary))
        .IsEnabled_Lambda([this]() -> bool {
            return CanAdvanceFromCurrentPage();
        })
        .OnClicked(this, &SIOCSetupWizard::OnNext)
        .Content()
        [
            IOCWizard::MakeActionLabel(
                TAttribute<FText>::CreateSP(this, &SIOCSetupWizard::GetNextButtonText),
                FLinearColor(0.95f, 0.99f, 1.0f),
                true,
                FMargin(18.0f, 9.0f))
        ]
    ];

    return SNew(SVerticalBox)

    + SVerticalBox::Slot()
    .AutoHeight()
    [
        Actions
    ]

    + SVerticalBox::Slot()
    .AutoHeight()
    .Padding(FMargin(0.0f, 16.0f, 0.0f, 0.0f))
    [
        IOCWizard::MakeBrandFooter()
    ];
}

// ============================================================================
// Welcome Page (with Feature 7: existing actor detection)
// ============================================================================
TSharedRef<SWidget> SIOCSetupWizard::BuildWelcomePage()
{
    TSharedPtr<SVerticalBox> VBox;
    SAssignNew(VBox, SVerticalBox);

    // The hero art already carries the product name and tagline, so it leads the page and
    // the old text badge sits on top of it rather than repeating it. Aspect is preserved
    // (1240x648) so the banner never distorts as the window resizes.
    VBox->AddSlot().AutoHeight()
    [
        SNew(SBox)
        .HeightOverride(196.0f)
        [
            SNew(SOverlay)
            + SOverlay::Slot()
            .HAlign(HAlign_Fill)
            .VAlign(VAlign_Fill)
            [
                // ScaleToFill keeps the 1240x648 aspect and crops the overflow. A bare
                // SImage in a fixed-height slot stretches the art to the slot's shape.
                SNew(SScaleBox)
                .Stretch(EStretch::ScaleToFill)
                .StretchDirection(EStretchDirection::Both)
                .Clipping(EWidgetClipping::ClipToBounds)
                [
                    // Tagged so SetupWizard.Layout can find it and assert that it is still
                    // arranged at the banner's aspect ratio. Removing the SScaleBox above
                    // is what stretched the hero once already, and nothing caught it.
                    SNew(SImage)
                    .Image(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::HeroBrush))
                    .AddMetaData(MakeShared<FTagMetaData>(IOCWizardTags::HeroImage))
                ]
            ]
            // Fade the bottom of the banner into the panel colour below it, so the art
            // dissolves into the content rather than being cut off by a hard edge. This
            // also conceals the crop that ScaleToFill necessarily makes -- which is what
            // was slicing the tagline through the middle.
            + SOverlay::Slot()
            .HAlign(HAlign_Fill)
            .VAlign(VAlign_Bottom)
            [
                SNew(SBox)
                .HeightOverride(110.0f)
                [
                    SNew(SImage)
                    .Image(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::ScrimDownBrush))
                    .ColorAndOpacity(FLinearColor(0.008f, 0.020f, 0.028f, 1.0f))
                ]
            ]
            // A fade alone still leaves the eye hunting for where the banner ends, because
            // the colour it fades to cannot match the translucent page beneath it exactly.
            // An accent rule declares the boundary instead of trying to hide it.
            + SOverlay::Slot()
            .HAlign(HAlign_Fill)
            .VAlign(VAlign_Bottom)
            [
                SNew(SBox)
                .HeightOverride(2.0f)
                [
                    SNew(SImage)
                    .Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .ColorAndOpacity(IOCWizard::AccentColor)
                ]
            ]
            // Badge sits above the fade so it stays legible over the busiest art.
            + SOverlay::Slot()
            .HAlign(HAlign_Left)
            .VAlign(VAlign_Bottom)
            .Padding(FMargin(24, 0, 0, 16))
            [
                IOCWizard::MakeBadge(
                    INVTEXT("Guided Setup"),
                    FLinearColor(0.0f, 0.0f, 0.0f, 0.55f),
                    IOCWizard::AccentColor,
                    9,
                    FMargin(10.0f, 5.0f))
            ]
        ]
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 16, 0, 0))
    [
        IOCWizard::MakeAccentPanel(
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock)
                .Text(INVTEXT("Instant Organic Caves"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 30))
                .ColorAndOpacity(IOCWizard::AccentColor)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
            [
                SNew(STextBlock)
                .Text(INVTEXT("Build a production-ready cave setup with live preview, readiness checks, and post-generation actions in one guided pass."))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                .ColorAndOpacity(IOCWizard::TextPrimary)
                .WrapTextAt(960)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 16, 0, 0))
            [
                SNew(SWrapBox)
                .UseAllottedSize(true)
                .AddMetaData(MakeShared<FTagMetaData>(IOCWizardTags::WrapBox))
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    IOCWizard::MakeChip(INVTEXT("Art-directed presets"))
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    IOCWizard::MakeChip(INVTEXT("Live preview"))
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    IOCWizard::MakeChip(INVTEXT("PCG and streaming ready"))
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    IOCWizard::MakeChip(INVTEXT("Runtime carving"))
                ]
            ],
            IOCWizard::AccentColor,
            FMargin(28, 24))
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 22, 0, 0))
    [
        IOCWizard::MakeSectionHeader(INVTEXT("Project Snapshot"))
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
    [
        SNew(SWrapBox)
                .UseAllottedSize(true)
                .AddMetaData(MakeShared<FTagMetaData>(IOCWizardTags::WrapBox))
        + SWrapBox::Slot().Padding(FMargin(0, 0, 10, 10))
        [
            SNew(SBorder)
            .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::CardPanel))
            .Padding(FMargin(14.0f, 12.0f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock)
                    .Text(INVTEXT("VALIDATION"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                    .ColorAndOpacity(IOCWizard::TextDim)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 6.0f, 0.0f, 0.0f))
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() -> FText {
                        if (InstallationStatus.IsEmpty() || ViewModel.bValidationDirty)
                        {
                            return INVTEXT("Pending");
                        }
                        return ViewModel.bValidationPassed ? INVTEXT("Passing") : INVTEXT("Needs fixes");
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
                    .ColorAndOpacity_Lambda([this]() -> FSlateColor {
                        if (InstallationStatus.IsEmpty() || ViewModel.bValidationDirty)
                        {
                            return IOCWizard::AccentColor;
                        }
                        return ViewModel.bValidationPassed ? FLinearColor(0.48f, 0.86f, 0.58f) : FLinearColor(0.96f, 0.39f, 0.29f);
                    })
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.0f, 4.0f, 0.0f, 0.0f))
                [
                    SNew(STextBlock)
                    .Text(INVTEXT("Live checks update while the wizard is open."))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(IOCWizard::TextDim)
                    .WrapTextAt(210.0f)
                ]
            ]
        ]
        + SWrapBox::Slot().Padding(FMargin(0, 0, 10, 10))
        [
            IOCWizard::MakeMetricCard(
                INVTEXT("EXISTING CAVES"),
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return FText::AsNumber(ExistingCaves.Num());
                }),
                INVTEXT("Detected IOC cave actors in the current level."),
                FLinearColor(0.53f, 0.79f, 0.98f))
        ]
        + SWrapBox::Slot().Padding(FMargin(0, 0, 10, 10))
        [
            IOCWizard::MakeMetricCard(
                INVTEXT("WORKLOAD"),
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return FText::FromString(FString::Printf(TEXT("%.2fM voxels"), BuildPreflightEstimate().EstimatedMillions));
                }),
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return FText::FromString(FString::Printf(TEXT("Risk: %s"), *BuildPreflightEstimate().RiskLabel));
                }),
                IOCWizard::GetRiskTint(BuildPreflightEstimate().RiskLabel))
        ]
        + SWrapBox::Slot().Padding(FMargin(0, 0, 10, 10))
        [
            IOCWizard::MakeMetricCard(
                INVTEXT("TARGET"),
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return bReconfigureExisting ? INVTEXT("Reconfigure existing") : INVTEXT("Spawn new actor");
                }),
                INVTEXT("You can switch this later on the cave style page."),
                IOCWizard::AccentColor)
        ]
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 18, 0, 0))
    [
        SNew(SBorder)
        .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::CardPanel))
        .Padding(FMargin(18, 16))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock)
                .Text(INVTEXT("Quick Start Actions"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                .ColorAndOpacity(IOCWizard::TextPrimary)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 6, 0, 0))
            [
                SNew(STextBlock)
                .Text(INVTEXT("Handle the most common first-run tasks here before you move deeper into cave tuning."))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                .ColorAndOpacity(IOCWizard::TextDim)
                .WrapTextAt(960)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 12, 0, 0))
            [
                SNew(SWrapBox)
                .UseAllottedSize(true)
                .AddMetaData(MakeShared<FTagMetaData>(IOCWizardTags::WrapBox))
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                    .OnClicked(this, &SIOCSetupWizard::OnValidateInstallation)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Validate Now"), IOCWizard::TextPrimary)
                    ]
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonPrimary))
                    .OnClicked(this, &SIOCSetupWizard::OnRunStarterAssetSetup)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Prepare Starter Assets"), IOCWizard::AccentColor)
                    ]
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                    .OnClicked(this, &SIOCSetupWizard::OnOpenStarterAssets)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Open Starter Assets"), IOCWizard::TextSecondary)
                    ]
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonPrimary))
                    .OnClicked(this, &SIOCSetupWizard::OnCreateStarterLevel)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Create Starter Level"), IOCWizard::TextPrimary)
                    ]
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonPrimary))
                    .OnClicked(this, &SIOCSetupWizard::OnOpenDemoMap)
                    .ToolTipText(INVTEXT("Open the demo level that ships with the plugin, then press Play for the guided showcase. Nothing needs setting up first."))
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Open Demo Map"), IOCWizard::AccentColor)
                    ]
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                    .OnClicked(this, &SIOCSetupWizard::OnOpenShowcaseMap)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Open Starter Level"), IOCWizard::TextPrimary)
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText {
                    if (!StarterAssetStatus.IsEmpty())
                    {
                        return FText::FromString(StarterAssetStatus);
                    }
                    if (!StarterLevelStatus.IsEmpty())
                    {
                        return FText::FromString(StarterLevelStatus);
                    }
                    return INVTEXT("Starter assets and showcase level helpers stay available again on the Review page.");
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                .ColorAndOpacity(IOCWizard::TextDim)
                .WrapTextAt(960)
            ]
        ]
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 22, 0, 0))
    [
        IOCWizard::MakeSectionHeader(INVTEXT("What this pass covers"))
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
    [
        SNew(SWrapBox)
                .UseAllottedSize(true)
                .AddMetaData(MakeShared<FTagMetaData>(IOCWizardTags::WrapBox))
        + SWrapBox::Slot().Padding(FMargin(0, 0, 10, 10))
        [
            IOCWizard::MakeMetricCard(
                INVTEXT("STEP 1"),
                INVTEXT("Choose a cave style"),
                INVTEXT("Start from a tuned preset or switch to Custom for full generation control."),
                IOCWizard::AccentColor)
        ]
        + SWrapBox::Slot().Padding(FMargin(0, 0, 10, 10))
        [
            IOCWizard::MakeMetricCard(
                INVTEXT("STEP 2"),
                INVTEXT("Review cost and readiness"),
                INVTEXT("Validation, existing cave detection, and workload estimates update while you work."),
                FLinearColor(0.53f, 0.79f, 0.98f))
        ]
        + SWrapBox::Slot().Padding(FMargin(0, 0, 10, 10))
        [
            IOCWizard::MakeMetricCard(
                INVTEXT("STEP 3"),
                INVTEXT("Generate and iterate"),
                INVTEXT("The finish page stays live so you can select the cave, add carving, and continue shaping."),
                FLinearColor(0.48f, 0.86f, 0.58f))
        ]
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 18, 0, 0))
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor_Lambda([this]() -> FLinearColor {
            return ExistingCaves.Num() > 0
                ? FLinearColor(0.05f, 0.06f, 0.09f)
                : FLinearColor(0.04f, 0.045f, 0.06f);
        })
        .Padding(FMargin(18, 14))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 8, 0))
                [
                    IOCWizard::MakeBadge(
                        TAttribute<FText>::CreateLambda([this]() -> FText {
                            return ExistingCaves.Num() > 0 ? INVTEXT("Existing cave detected") : INVTEXT("Fresh level flow");
                        }),
                        IOCWizard::BadgeBg,
                        ExistingCaves.Num() > 0 ? IOCWizard::AccentColor : IOCWizard::TextSecondary)
                ]
                + SHorizontalBox::Slot().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() -> FText {
                        if (ExistingCaves.Num() > 0)
                        {
                            return FText::FromString(FString::Printf(
                                TEXT("Found %d IOC cave actor%s in this level. You can reconfigure one in place or spawn a new setup alongside it."),
                                ExistingCaves.Num(),
                                ExistingCaves.Num() == 1 ? TEXT("") : TEXT("s")));
                        }
                        return INVTEXT("No IOC cave actors were found in the current level. The wizard will guide a clean first setup.");
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                    .ColorAndOpacity(IOCWizard::TextSecondary)
                    .WrapTextAt(900)
                ]
            ]
        ]
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 18, 0, 0))
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(IOCWizard::CardBgHover)
        .Padding(FMargin(20, 16))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock)
                .Text(INVTEXT("Recommended first pass"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                .ColorAndOpacity(IOCWizard::TextPrimary)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 6, 0, 0))
            [
                SNew(STextBlock)
                .Text(INVTEXT("Use a preset first, review the workload on the Review page, then generate. The finish page will stay live for immediate follow-up actions."))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                .ColorAndOpacity(IOCWizard::TextDim)
                .WrapTextAt(960)
            ]
        ]
    ];

    return VBox.ToSharedRef();
}

// ============================================================================
// Preset Page (Feature 1: preview, Feature 5: tooltips, Feature 6: comparison)
// ============================================================================
TSharedRef<SWidget> SIOCSetupWizard::BuildPresetPage()
{
    TSharedPtr<SVerticalBox> VBox;
    SAssignNew(VBox, SVerticalBox);

    VBox->AddSlot().AutoHeight()
    [
        IOCWizard::MakeSectionHeader(INVTEXT("Choose a Cave Style"), 18)
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 6, 0, 10))
    [
        SNew(STextBlock)
        .Text(INVTEXT("Select a preset for a fast first pass, or switch to Custom if you need exact control over generation, bounds, and production settings."))
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
        .ColorAndOpacity(IOCWizard::TextSecondary)
        .WrapTextAt(980)
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 0, 0, 10))
    [
        SNew(SWrapBox)
                .UseAllottedSize(true)
                .AddMetaData(MakeShared<FTagMetaData>(IOCWizardTags::WrapBox))
        + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
        [
            IOCWizard::MakeBadge(
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return FText::FromString(FString::Printf(TEXT("Selected: %s"), *GetSelectedPresetName()));
                }),
                IOCWizard::BadgeBg,
                IOCWizard::AccentColor)
        ]
        + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
        [
            IOCWizard::MakeBadge(
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return bReconfigureExisting ? INVTEXT("Target: existing actor") : INVTEXT("Target: spawn new actor");
                }),
                FLinearColor(0.05f, 0.08f, 0.06f),
                bReconfigureExisting ? FLinearColor(0.45f, 0.84f, 0.56f) : IOCWizard::TextSecondary)
        ]
        + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
        [
            IOCWizard::MakeBadge(
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return bPreviewFullFidelity ? INVTEXT("Preview: full geometry") : INVTEXT("Preview: fast reduced geometry");
                }),
                FLinearColor(0.06f, 0.08f, 0.12f),
                IOCWizard::TextSecondary)
        ]
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 0, 0, 10))
    [
        SNew(SBorder)
        .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::CardPanel))
        .Padding(FMargin(18, 16))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(INVTEXT("Preset Workflow"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                        .ColorAndOpacity(IOCWizard::TextPrimary)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 6, 0, 0))
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() -> FText {
                            if (bIsCustom)
                            {
                                return SelectedPresetIndex < IOCWizard::CustomIndex
                                    ? FText::FromString(FString::Printf(TEXT("You are editing custom values derived from %s. Refresh that baseline, randomize the seed, or make the first pass safer right here."),
                                        *IOCWizard::Presets[SelectedPresetIndex].Name))
                                    : INVTEXT("You are in fully custom mode. Randomize the seed or reduce workload here before moving to review.");
                            }

                            return INVTEXT("Preview a tuned preset first, then turn it into editable custom values only if you need exact production control.");
                        })
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                        .ColorAndOpacity(IOCWizard::TextDim)
                        .WrapTextAt(740)
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(16, 0, 0, 0))
                [
                    SNew(SWrapBox)
                .UseAllottedSize(true)
                .AddMetaData(MakeShared<FTagMetaData>(IOCWizardTags::WrapBox))
                    + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                    [
                        SNew(SButton)
                        .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                        .Visibility_Lambda([this]() -> EVisibility {
                            return SelectedPresetIndex < IOCWizard::CustomIndex ? EVisibility::Visible : EVisibility::Collapsed;
                        })
                        .OnClicked(this, &SIOCSetupWizard::OnConvertPresetToCustom)
                        .Content()
                        [
                            IOCWizard::MakeActionLabel(INVTEXT("Use Preset As Custom"), IOCWizard::AccentColor)
                        ]
                    ]
                    + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                    [
                        SNew(SButton)
                        .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                        .OnClicked(this, &SIOCSetupWizard::OnRandomizeSeed)
                        .Content()
                        [
                            IOCWizard::MakeActionLabel(INVTEXT("Randomize Seed"), IOCWizard::TextPrimary)
                        ]
                    ]
                    + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                    [
                        SNew(SButton)
                        .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                        .OnClicked(this, &SIOCSetupWizard::OnUseSaferVoxelSize)
                        .Content()
                        [
                            IOCWizard::MakeActionLabel(INVTEXT("Safer First Pass"), IOCWizard::TextPrimary)
                        ]
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 12, 0, 0))
            [
                SNew(SWrapBox)
                .UseAllottedSize(true)
                .AddMetaData(MakeShared<FTagMetaData>(IOCWizardTags::WrapBox))
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    IOCWizard::MakeBadge(
                        TAttribute<FText>::CreateLambda([this]() -> FText {
                            return FText::FromString(FString::Printf(TEXT("Workload: %.2fM voxels"), BuildPreflightEstimate().EstimatedMillions));
                        }),
                        FLinearColor(0.05f, 0.08f, 0.06f),
                        IOCWizard::GetRiskTint(BuildPreflightEstimate().RiskLabel))
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    IOCWizard::MakeBadge(
                        TAttribute<FText>::CreateLambda([this]() -> FText {
                            return FText::FromString(FString::Printf(TEXT("Risk: %s"), *BuildPreflightEstimate().RiskLabel));
                        }),
                        IOCWizard::GetRiskSurface(BuildPreflightEstimate().RiskLabel),
                        IOCWizard::GetRiskTint(BuildPreflightEstimate().RiskLabel))
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    IOCWizard::MakeBadge(
                        TAttribute<FText>::CreateLambda([this]() -> FText {
                            return FText::FromString(FString::Printf(TEXT("Seed: %d"), CustomSeed));
                        }),
                        FLinearColor(0.06f, 0.08f, 0.12f),
                        IOCWizard::TextSecondary)
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    IOCWizard::MakeBadge(
                        TAttribute<FText>::CreateLambda([this]() -> FText {
                            return bPreviewFullFidelity ? INVTEXT("Preview uses full geometry") : INVTEXT("Preview uses fast reduced geometry");
                        }),
                        IOCWizard::BadgeBg,
                        IOCWizard::TextSecondary)
                ]
            ]
        ]
    ];

    // Feature 7: Reconfigure existing actor option
    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 0, 0, 8))
    [
        BuildExistingActorSelector()
    ];

    // Preset cards + Feature 1: Preview viewport side by side
    VBox->AddSlot().AutoHeight()
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(0.65f).Padding(FMargin(0, 0, 12, 0))
        [
            SAssignNew(PresetCardsContainer, SVerticalBox)
        ]
        + SHorizontalBox::Slot().FillWidth(0.35f)
        [
            // Feature 1: Live 3D Preview
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 0, 0, 8))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(INVTEXT("Live Preview"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
                    .ColorAndOpacity(IOCWizard::TextPrimary)
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    IOCWizard::MakeBadge(INVTEXT("Auto-refresh"), IOCWizard::BadgeBg, IOCWizard::TextSecondary)
                ]
            ]
            + SVerticalBox::Slot().FillHeight(1.0f).MaxHeight(260)
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(IOCWizard::PanelBg)
                .Padding(2)
                [
                    SAssignNew(PreviewViewport, SIOCPreviewViewport)
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 6, 0, 0))
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText {
                    if (bIsCustom) return INVTEXT("Custom workflow is active. Adjust the parameters below and the preview will keep up.");
                    int32 Idx = FMath::Clamp(SelectedPresetIndex, 0, 4);
                    return FText::FromString(IOCWizard::Presets[Idx].Description);
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 10))
                .ColorAndOpacity(IOCWizard::TextDim)
                .WrapTextAt(300)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
            [
                SNew(SWrapBox)
                .UseAllottedSize(true)
                .AddMetaData(MakeShared<FTagMetaData>(IOCWizardTags::WrapBox))
                + SWrapBox::Slot().Padding(FMargin(0, 0, 6, 6))
                [
                    IOCWizard::MakeBadge(
                        TAttribute<FText>::CreateLambda([this]() -> FText {
                            return bCustomTunnelMode ? INVTEXT("Mode: tunnel") : INVTEXT("Mode: chamber volume");
                        }),
                        FLinearColor(0.05f, 0.08f, 0.06f),
                        IOCWizard::TextSecondary)
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 6, 6))
                [
                    IOCWizard::MakeBadge(
                        TAttribute<FText>::CreateLambda([this]() -> FText {
                            return bReconfigureExisting ? INVTEXT("Applying onto existing cave") : INVTEXT("Spawning a fresh cave actor");
                        }),
                        FLinearColor(0.06f, 0.08f, 0.12f),
                        IOCWizard::TextSecondary)
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 6, 6))
                [
                    IOCWizard::MakeBadge(
                        TAttribute<FText>::CreateLambda([this]() -> FText {
                            return FText::FromString(FString::Printf(TEXT("Risk: %s"), *BuildPreflightEstimate().RiskLabel));
                        }),
                        IOCWizard::GetRiskSurface(BuildPreflightEstimate().RiskLabel),
                        IOCWizard::GetRiskTint(BuildPreflightEstimate().RiskLabel))
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 6, 6))
                [
                    IOCWizard::MakeBadge(
                        TAttribute<FText>::CreateLambda([this]() -> FText {
                            return FText::FromString(FString::Printf(TEXT("%.2fM voxels"), BuildPreflightEstimate().EstimatedMillions));
                        }),
                        FLinearColor(0.05f, 0.08f, 0.06f),
                        IOCWizard::GetRiskTint(BuildPreflightEstimate().RiskLabel))
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 8, 0))
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() -> ECheckBoxState {
                        return bPreviewFullFidelity ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
                        bPreviewFullFidelity = (State == ECheckBoxState::Checked);
                        RequestPreviewRefresh();
                    })
                    .ToolTip(SNew(SToolTip).Text(INVTEXT("Use the real generation settings in the preview. This can be slow for large bounds or small voxels.")))
                ]
                + SHorizontalBox::Slot().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(INVTEXT("Full geometry preview"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                    .ColorAndOpacity(IOCWizard::TextSecondary)
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText {
                    return PreviewStatus.IsEmpty()
                        ? INVTEXT("Preview will rebuild automatically after changes.")
                        : FText::FromString(PreviewStatus);
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                .ColorAndOpacity(IOCWizard::TextDim)
                .WrapTextAt(300)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 4, 0, 0))
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText {
                    return LastPreviewBuiltAt.GetTicks() > 0
                        ? FText::FromString(FString::Printf(TEXT("Last built: %s"), *LastPreviewBuiltAt.ToString(TEXT("%H:%M:%S"))))
                        : INVTEXT("Last built: pending");
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
                .ColorAndOpacity(IOCWizard::TextDim)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
            [
                SNew(SButton)
                .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                .OnClicked_Lambda([this]() -> FReply {
                    RequestPreviewRefresh(0.0);
                    return FReply::Handled();
                })
                .Content()
                [
                    IOCWizard::MakeActionLabel(INVTEXT("Refresh Preview Now"), IOCWizard::TextSecondary)
                ]
            ]
        ]
    ];

    // Fill preset cards
    auto AddRow = [&](int32 StartIndex, int32 Count) {
        TSharedPtr<SHorizontalBox> HBox;
        SAssignNew(HBox, SHorizontalBox);
        for (int32 i = StartIndex; i < StartIndex + Count; i++)
        {
            HBox->AddSlot()
            .FillWidth(1.0f)
            .Padding(FMargin(0, 0, (i < StartIndex + Count - 1) ? 8 : 0, 0))
            [
                MakePresetCard(i,
                    (i < IOCWizard::CustomIndex) ? IOCWizard::Presets[i].Name : IOCWizard::CustomName,
                    (i < IOCWizard::CustomIndex) ? IOCWizard::Presets[i].Icon : IOCWizard::CustomIcon,
                    (i < IOCWizard::CustomIndex) ? IOCWizard::Presets[i].Description : IOCWizard::CustomDesc,
                    (i < IOCWizard::CustomIndex) ? IOCWizard::Presets[i].Specs : IOCWizard::CustomSpecs,
                    (i < IOCWizard::CustomIndex) ? IOCWizard::Presets[i].Color : IOCWizard::CustomColor)
            ];
        }
        PresetCardsContainer->AddSlot().AutoHeight().Padding(FMargin(0, 0, 0, 8)) [ HBox.ToSharedRef() ];
    };

    AddRow(0, 3);
    AddRow(3, 3);

    // Custom parameters panel (Feature 5: tooltips on controls)
    TSharedPtr<SVerticalBox> CustomBox;
    SAssignNew(CustomBox, SVerticalBox);

    auto AddSection = [&CustomBox](const FString& Title)
    {
        CustomBox->AddSlot().AutoHeight().Padding(FMargin(0, 14, 0, 6))
        [
            SNew(STextBlock)
            .Text(FText::FromString(Title))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
            .ColorAndOpacity(IOCWizard::TextPrimary)
        ];
    };

    auto MakeLabel = [](const FString& Label, const FString& Tip) -> TSharedRef<SWidget>
    {
        return SNew(STextBlock)
            .Text(FText::FromString(Label))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
            .ColorAndOpacity(IOCWizard::TextDim)
            .ToolTip(SNew(SToolTip).Text(FText::FromString(Tip)));
    };

    CustomBox->AddSlot().AutoHeight()
    [
        IOCWizard::MakeSectionHeader(INVTEXT("Custom Parameters"), 13)
    ];

    CustomBox->AddSlot().AutoHeight().Padding(FMargin(0, 4, 0, 0))
    [
        SNew(STextBlock)
        .Text(INVTEXT("These settings map to the cave actor's main generation, appearance, and production options."))
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
        .ColorAndOpacity(IOCWizard::TextDim)
        .WrapTextAt(920)
    ];

    AddSection(TEXT("Generation"));

    CustomBox->AddSlot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() -> ECheckBoxState {
                return bCustomTunnelMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
                bCustomTunnelMode = (State == ECheckBoxState::Checked);
            })
            .ToolTip(SNew(SToolTip).Text(INVTEXT("Generate a carved tunnel between two points. Uncheck for open cave volumes.")))
        ]
        + SHorizontalBox::Slot().VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(INVTEXT("Tunnel mode"))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
            .ColorAndOpacity(IOCWizard::TextSecondary)
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(22, 0, 12, 0))
        [
            SNew(SCheckBox)
            .IsEnabled_Lambda([this]() -> bool { return bCustomTunnelMode; })
            .IsChecked_Lambda([this]() -> ECheckBoxState {
                return bCustomUseSpline ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
                bCustomUseSpline = (State == ECheckBoxState::Checked);
            })
            .ToolTip(SNew(SToolTip).Text(INVTEXT("Enable spline mode on the generated actor. You can edit spline points after setup.")))
        ]
        + SHorizontalBox::Slot().VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(INVTEXT("Use spline path"))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
            .ColorAndOpacity(IOCWizard::TextSecondary)
        ]
    ];

    CustomBox->AddSlot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(0.34f).Padding(0, 0, 10, 0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                MakeLabel(TEXT("Seed"), IOCWizard::GetParamTooltips().FindRef(TEXT("Seed")))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SSpinBox<int32>)
                .Value_Lambda([this]() -> int32 { return CustomSeed; })
                .OnValueChanged_Lambda([this](int32 v) { CustomSeed = v; })
                .MinValue(0).MaxValue(999999).MinSliderValue(0).MaxSliderValue(999999).Delta(1)
            ]
        ]
        + SHorizontalBox::Slot().FillWidth(0.33f).Padding(0, 0, 10, 0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                MakeLabel(TEXT("Voxel Size (cm)"), IOCWizard::GetParamTooltips().FindRef(TEXT("Voxel Size (cm)")))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SSpinBox<double>)
                .Value_Lambda([this]() -> double { return CustomVoxelSize; })
                .OnValueChanged_Lambda([this](double v) { CustomVoxelSize = v; })
                .MinValue(10.0).MaxValue(400.0).MinSliderValue(10.0).MaxSliderValue(400.0).Delta(5.0)
            ]
        ]
        + SHorizontalBox::Slot().FillWidth(0.33f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                MakeLabel(TEXT("Noise Frequency"), IOCWizard::GetParamTooltips().FindRef(TEXT("Noise Frequency")))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SSpinBox<float>)
                .Value_Lambda([this]() -> float { return CustomNoiseFrequency; })
                .OnValueChanged_Lambda([this](float v) { CustomNoiseFrequency = v; })
                .MinValue(0.001f).MaxValue(0.05f).MinSliderValue(0.001f).MaxSliderValue(0.05f).Delta(0.001f)
            ]
        ]
    ];

    CustomBox->AddSlot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(0.34f).Padding(0, 0, 10, 0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                MakeLabel(TEXT("Smooth Iterations"), IOCWizard::GetParamTooltips().FindRef(TEXT("Smooth Iterations")))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SSpinBox<int32>)
                .Value_Lambda([this]() -> int32 { return CustomSmoothIterations; })
                .OnValueChanged_Lambda([this](int32 v) { CustomSmoothIterations = v; })
                .MinValue(0).MaxValue(20).MinSliderValue(0).MaxSliderValue(20).Delta(1)
            ]
        ]
        + SHorizontalBox::Slot().FillWidth(0.33f).Padding(0, 0, 10, 0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                MakeLabel(TEXT("Domain Warp"), IOCWizard::GetParamTooltips().FindRef(TEXT("Domain Warp")))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SSpinBox<float>)
                .Value_Lambda([this]() -> float { return CustomDomainWarpIntensity; })
                .OnValueChanged_Lambda([this](float v) { CustomDomainWarpIntensity = v; })
                .MinValue(0.0f).MaxValue(500.0f).MinSliderValue(0.0f).MaxSliderValue(500.0f).Delta(10.0f)
            ]
        ]
        + SHorizontalBox::Slot().FillWidth(0.33f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                MakeLabel(TEXT("Terrace Steps"), IOCWizard::GetParamTooltips().FindRef(TEXT("Terrace Steps")))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SSpinBox<float>)
                .Value_Lambda([this]() -> float { return CustomTerraceSteps; })
                .OnValueChanged_Lambda([this](float v) { CustomTerraceSteps = v; })
                .MinValue(0.0f).MaxValue(500.0f).MinSliderValue(0.0f).MaxSliderValue(500.0f).Delta(10.0f)
            ]
        ]
    ];

    AddSection(TEXT("Bounds and Path"));

    CustomBox->AddSlot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
        [
            MakeLabel(TEXT("Generation Bounds X / Y / Z"), IOCWizard::GetParamTooltips().FindRef(TEXT("Generation Bounds")))
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
            [
                SNew(SSpinBox<float>)
                .Value_Lambda([this]() -> float { return (float)CustomBounds.X; })
                .OnValueChanged_Lambda([this](float v) { CustomBounds.X = v; })
                .MinValue(500.0f).MaxValue(50000.0f).MinSliderValue(500.0f).MaxSliderValue(12000.0f).Delta(100.0f)
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
            [
                SNew(SSpinBox<float>)
                .Value_Lambda([this]() -> float { return (float)CustomBounds.Y; })
                .OnValueChanged_Lambda([this](float v) { CustomBounds.Y = v; })
                .MinValue(500.0f).MaxValue(50000.0f).MinSliderValue(500.0f).MaxSliderValue(12000.0f).Delta(100.0f)
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SSpinBox<float>)
                .Value_Lambda([this]() -> float { return (float)CustomBounds.Z; })
                .OnValueChanged_Lambda([this](float v) { CustomBounds.Z = v; })
                .MinValue(500.0f).MaxValue(50000.0f).MinSliderValue(500.0f).MaxSliderValue(8000.0f).Delta(100.0f)
            ]
        ]
    ];

    CustomBox->AddSlot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
    [
        SNew(SBorder)
        .Visibility_Lambda([this]() -> EVisibility {
            return bCustomTunnelMode ? EVisibility::Visible : EVisibility::Collapsed;
        })
        .BorderBackgroundColor(FLinearColor::Transparent)
        .Padding(0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(0.5f).Padding(0, 0, 10, 0)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
                    [
                        MakeLabel(TEXT("Tunnel Radius (cm)"), IOCWizard::GetParamTooltips().FindRef(TEXT("Tunnel Radius (cm)")))
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SSpinBox<float>)
                        .Value_Lambda([this]() -> float { return CustomTunnelRadius; })
                        .OnValueChanged_Lambda([this](float v) { CustomTunnelRadius = v; })
                        .MinValue(50.0f).MaxValue(3000.0f).MinSliderValue(50.0f).MaxSliderValue(2000.0f).Delta(25.0f)
                    ]
                ]
                + SHorizontalBox::Slot().FillWidth(0.5f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
                    [
                        MakeLabel(TEXT("Wall Thickness (cm)"), IOCWizard::GetParamTooltips().FindRef(TEXT("Wall Thickness (cm)")))
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SSpinBox<float>)
                        .Value_Lambda([this]() -> float { return CustomWallThickness; })
                        .OnValueChanged_Lambda([this](float v) { CustomWallThickness = v; })
                        .MinValue(10.0f).MaxValue(1000.0f).MinSliderValue(10.0f).MaxSliderValue(400.0f).Delta(10.0f)
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
                [
                    MakeLabel(TEXT("Tunnel Start X / Y / Z"), IOCWizard::GetParamTooltips().FindRef(TEXT("Tunnel Start/End")))
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
                    [
                        SNew(SSpinBox<float>).Value_Lambda([this]() -> float { return (float)CustomTunnelStart.X; })
                        .OnValueChanged_Lambda([this](float v) { CustomTunnelStart.X = v; })
                        .MinValue(-50000.0f).MaxValue(50000.0f).MinSliderValue(-12000.0f).MaxSliderValue(12000.0f).Delta(100.0f)
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
                    [
                        SNew(SSpinBox<float>).Value_Lambda([this]() -> float { return (float)CustomTunnelStart.Y; })
                        .OnValueChanged_Lambda([this](float v) { CustomTunnelStart.Y = v; })
                        .MinValue(-50000.0f).MaxValue(50000.0f).MinSliderValue(-12000.0f).MaxSliderValue(12000.0f).Delta(100.0f)
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.0f)
                    [
                        SNew(SSpinBox<float>).Value_Lambda([this]() -> float { return (float)CustomTunnelStart.Z; })
                        .OnValueChanged_Lambda([this](float v) { CustomTunnelStart.Z = v; })
                        .MinValue(-50000.0f).MaxValue(50000.0f).MinSliderValue(-4000.0f).MaxSliderValue(8000.0f).Delta(100.0f)
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
                [
                    MakeLabel(TEXT("Tunnel End X / Y / Z"), IOCWizard::GetParamTooltips().FindRef(TEXT("Tunnel Start/End")))
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
                    [
                        SNew(SSpinBox<float>).Value_Lambda([this]() -> float { return (float)CustomTunnelEnd.X; })
                        .OnValueChanged_Lambda([this](float v) { CustomTunnelEnd.X = v; })
                        .MinValue(-50000.0f).MaxValue(50000.0f).MinSliderValue(-12000.0f).MaxSliderValue(12000.0f).Delta(100.0f)
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
                    [
                        SNew(SSpinBox<float>).Value_Lambda([this]() -> float { return (float)CustomTunnelEnd.Y; })
                        .OnValueChanged_Lambda([this](float v) { CustomTunnelEnd.Y = v; })
                        .MinValue(-50000.0f).MaxValue(50000.0f).MinSliderValue(-12000.0f).MaxSliderValue(12000.0f).Delta(100.0f)
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.0f)
                    [
                        SNew(SSpinBox<float>).Value_Lambda([this]() -> float { return (float)CustomTunnelEnd.Z; })
                        .OnValueChanged_Lambda([this](float v) { CustomTunnelEnd.Z = v; })
                        .MinValue(-50000.0f).MaxValue(50000.0f).MinSliderValue(-4000.0f).MaxSliderValue(8000.0f).Delta(100.0f)
                    ]
                ]
            ]
        ]
    ];

    AddSection(TEXT("Appearance and Production"));

    CustomBox->AddSlot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(0.34f).Padding(0, 0, 10, 0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                MakeLabel(TEXT("Texture Tiling"), IOCWizard::GetParamTooltips().FindRef(TEXT("Texture Tiling")))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SSpinBox<float>)
                .Value_Lambda([this]() -> float { return CustomTextureTiling; })
                .OnValueChanged_Lambda([this](float v) { CustomTextureTiling = v; })
                .MinValue(0.0001f).MaxValue(1.0f).MinSliderValue(0.001f).MaxSliderValue(0.05f).Delta(0.001f)
            ]
        ]
        + SHorizontalBox::Slot().FillWidth(0.33f).Padding(0, 0, 10, 0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                MakeLabel(TEXT("LOD Distance"), IOCWizard::GetParamTooltips().FindRef(TEXT("LOD")))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SSpinBox<float>)
                .IsEnabled_Lambda([this]() -> bool { return WizardSettings.bEnableLOD; })
                .Value_Lambda([this]() -> float { return CustomLODDistance; })
                .OnValueChanged_Lambda([this](float v) { CustomLODDistance = v; })
                .MinValue(500.0f).MaxValue(100000.0f).MinSliderValue(1000.0f).MaxSliderValue(20000.0f).Delta(250.0f)
            ]
        ]
        + SHorizontalBox::Slot().FillWidth(0.33f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
            [
                MakeLabel(TEXT("LOD Voxel Multiplier"), IOCWizard::GetParamTooltips().FindRef(TEXT("LOD")))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SSpinBox<float>)
                .IsEnabled_Lambda([this]() -> bool { return WizardSettings.bEnableLOD; })
                .Value_Lambda([this]() -> float { return CustomLODMultiplier; })
                .OnValueChanged_Lambda([this](float v) { CustomLODMultiplier = v; })
                .MinValue(1.0f).MaxValue(12.0f).MinSliderValue(1.0f).MaxSliderValue(8.0f).Delta(0.25f)
            ]
        ]
    ];

    CustomBox->AddSlot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() -> ECheckBoxState { return WizardSettings.bGenerateSmartColors ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { WizardSettings.bGenerateSmartColors = (State == ECheckBoxState::Checked); })
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 22, 0)
        [
            SNew(STextBlock).Text(INVTEXT("Smart vertex colors"))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).ColorAndOpacity(IOCWizard::TextSecondary)
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() -> ECheckBoxState { return WizardSettings.bEnableLOD ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { WizardSettings.bEnableLOD = (State == ECheckBoxState::Checked); })
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 22, 0)
        [
            SNew(STextBlock).Text(INVTEXT("Generate LOD mesh"))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).ColorAndOpacity(IOCWizard::TextSecondary)
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() -> ECheckBoxState { return WizardSettings.bUseWorldSpaceNoise ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { WizardSettings.bUseWorldSpaceNoise = (State == ECheckBoxState::Checked); })
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(STextBlock).Text(INVTEXT("World-space noise"))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).ColorAndOpacity(IOCWizard::TextSecondary)
        ]
    ];

    CustomBox->AddSlot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() -> ECheckBoxState { return WizardSettings.bUseFixedBoundsForTunnel ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { WizardSettings.bUseFixedBoundsForTunnel = (State == ECheckBoxState::Checked); })
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 22, 0)
        [
            SNew(STextBlock).Text(INVTEXT("Fixed tunnel bounds"))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).ColorAndOpacity(IOCWizard::TextSecondary)
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() -> ECheckBoxState { return WizardSettings.bAutoRebuildNavMesh ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { WizardSettings.bAutoRebuildNavMesh = (State == ECheckBoxState::Checked); })
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 22, 0)
        [
            SNew(STextBlock).Text(INVTEXT("Rebuild navmesh"))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).ColorAndOpacity(IOCWizard::TextSecondary)
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this]() -> ECheckBoxState { return WizardSettings.bShowDebugViz ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { WizardSettings.bShowDebugViz = (State == ECheckBoxState::Checked); })
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(STextBlock).Text(INVTEXT("Debug bounds"))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).ColorAndOpacity(IOCWizard::TextSecondary)
        ]
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
    [
        SNew(SBorder)
        .Visibility_Lambda([this]() -> EVisibility {
            return bIsCustom ? EVisibility::Visible : EVisibility::Collapsed;
        })
        .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::CardPanel))
        .Padding(FMargin(18, 16))
        [
            CustomBox.ToSharedRef()
        ]
    ];

    // Feature 6: Preset comparison table
    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 16, 0, 0))
    [
        BuildComparisonTable()
    ];

    return VBox.ToSharedRef();
}

// ============================================================================
// Preset card
// ============================================================================
TSharedRef<SWidget> SIOCSetupWizard::MakePresetCard(int32 Index, const FString& Title,
    const FString& Icon, const FString& Desc, const FString& Specs, const FLinearColor& Tint)
{
    return SNew(SButton)
    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
    .OnClicked_Lambda([this, Index]() -> FReply {
        SelectedPresetIndex = Index;
        bIsCustom = (Index == IOCWizard::CustomIndex);
        if (Index < IOCWizard::CustomIndex)
        {
            CustomTextureTiling = IOC_GetRecommendedTextureTiling(IOCWizard::Presets[Index].Preset);
        }
        RequestPreviewRefresh(0.0);
        return FReply::Handled();
    })
    .Content()
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor_Lambda([this, Index, Tint]() -> FLinearColor {
            if (SelectedPresetIndex == Index)
            {
                return Tint * 0.5f + FLinearColor(0.02f, 0.02f, 0.03f);
            }
            return IOCWizard::CardBg;
        })
        .Padding(FMargin(5, 0, 0, 0))
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor_Lambda([this, Index]() -> FLinearColor {
                if (SelectedPresetIndex == Index) return FLinearColor(0.045f, 0.05f, 0.065f);
                return FLinearColor(0.04f, 0.042f, 0.055f);
            })
            .Padding(FMargin(14, 12))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 8, 0))
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(Icon))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
                        .ColorAndOpacity_Lambda([this, Index, Tint]() -> FSlateColor {
                            return (SelectedPresetIndex == Index) ? Tint : IOCWizard::TextDim;
                        })
                    ]
                    + SHorizontalBox::Slot().VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(Title))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
                        .ColorAndOpacity_Lambda([this, Index, Tint]() -> FSlateColor {
                            return (SelectedPresetIndex == Index) ? Tint : IOCWizard::TextPrimary;
                        })
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.0f)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        SNew(SBorder)
                        .Visibility_Lambda([this, Index]() -> EVisibility {
                            return SelectedPresetIndex == Index ? EVisibility::Visible : EVisibility::Collapsed;
                        })
                        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                        .BorderBackgroundColor(Tint * 0.18f + FLinearColor(0.03f, 0.03f, 0.04f))
                        .Padding(FMargin(8, 3))
                        [
                            SNew(STextBlock)
                            .Text(INVTEXT("Selected"))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
                            .ColorAndOpacity(Tint)
                        ]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 6, 0, 0))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Desc))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                    .ColorAndOpacity(IOCWizard::TextDim)
                    .WrapTextAt(250)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor_Lambda([this, Index, Tint]() -> FLinearColor {
                        return (SelectedPresetIndex == Index)
                            ? Tint * 0.15f + FLinearColor(0.03f, 0.03f, 0.04f)
                            : FLinearColor(0.035f, 0.035f, 0.045f);
                    })
                    .Padding(FMargin(8, 4))
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(Specs))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                        .ColorAndOpacity_Lambda([this, Index]() -> FSlateColor {
                            return (SelectedPresetIndex == Index)
                                ? IOCWizard::TextSecondary
                                : FLinearColor(0.3f, 0.32f, 0.36f);
                        })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
                [
                    SNew(STextBlock)
                    .Text_Lambda([this, Index]() -> FText {
                        return SelectedPresetIndex == Index
                            ? INVTEXT("This style is active in the preview.")
                            : INVTEXT("Click to preview and continue with this style.");
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(IOCWizard::TextDim)
                ]
            ]
        ]
    ];
}

// ============================================================================
// Feature 6: Preset Comparison Table
// ============================================================================
TSharedRef<SWidget> SIOCSetupWizard::BuildComparisonTable()
{
    TSharedPtr<SVerticalBox> VBox;
    SAssignNew(VBox, SVerticalBox);

    VBox->AddSlot().AutoHeight()
    [
        IOCWizard::MakeSectionHeader(INVTEXT("Preset Comparison"), 12)
    ];

    auto MakeHeaderCell = [](const FString& Text) -> TSharedRef<SWidget> {
        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(IOCWizard::CardBgHover)
            .Padding(FMargin(6, 4))
            [
                SNew(STextBlock)
                .Text(FText::FromString(Text))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
                .ColorAndOpacity(IOCWizard::TextPrimary)
            ];
    };

    auto MakeCell = [this](const FString& Text, int32 RowIndex) -> TSharedRef<SWidget> {
        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor_Lambda([this, RowIndex]() -> FLinearColor {
                return (!bIsCustom && SelectedPresetIndex == RowIndex)
                    ? FLinearColor(0.055f, 0.07f, 0.09f)
                    : FLinearColor(0.035f, 0.04f, 0.05f);
            })
            .Padding(FMargin(6, 3))
            [
                SNew(STextBlock)
                .Text(FText::FromString(Text))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                .ColorAndOpacity_Lambda([this, RowIndex]() -> FSlateColor {
                    return (!bIsCustom && SelectedPresetIndex == RowIndex)
                        ? IOCWizard::TextPrimary
                        : IOCWizard::TextDim;
                })
            ];
    };

    // Header row
    TSharedPtr<SHorizontalBox> HeaderRow;
    SAssignNew(HeaderRow, SHorizontalBox);
    HeaderRow->AddSlot().FillWidth(0.18f) [ MakeHeaderCell(TEXT("Preset")) ];
    HeaderRow->AddSlot().FillWidth(0.15f) [ MakeHeaderCell(TEXT("Radius")) ];
    HeaderRow->AddSlot().FillWidth(0.15f) [ MakeHeaderCell(TEXT("Voxel")) ];
    HeaderRow->AddSlot().FillWidth(0.17f) [ MakeHeaderCell(TEXT("Best For")) ];
    HeaderRow->AddSlot().FillWidth(0.15f) [ MakeHeaderCell(TEXT("Fog")) ];
    HeaderRow->AddSlot().FillWidth(0.20f) [ MakeHeaderCell(TEXT("Lighting")) ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 6, 0, 0)) [ HeaderRow.ToSharedRef() ];

    static const FString PresetNames[] = { TEXT("Large Tunnel"), TEXT("Tight Crawl"), TEXT("Open Cavern"), TEXT("Alien Hive"), TEXT("Canyon Strata") };
    static const FString Radii[] = { TEXT("450 cm"), TEXT("150 cm"), TEXT("1200 cm"), TEXT("300 cm"), TEXT("500 cm") };
    static const FString Voxels[] = { TEXT("40 cm"), TEXT("20 cm"), TEXT("60 cm"), TEXT("30 cm"), TEXT("50 cm") };
    static const FString BestFor[] = { TEXT("Vehicles"), TEXT("Tension"), TEXT("Boss arenas"), TEXT("Sci-fi"), TEXT("Mines") };
    static const FString Fog[] = { TEXT("Light"), TEXT("Heavy"), TEXT("Minimal"), TEXT("Medium"), TEXT("Medium") };
    static const FString Lighting[] = { TEXT("Sky + Sun"), TEXT("Point lights"), TEXT("Sky + Sun"), TEXT("Point lights"), TEXT("Sky + Sun") };

    for (int32 i = 0; i < 5; i++)
    {
        TSharedPtr<SHorizontalBox> Row;
        SAssignNew(Row, SHorizontalBox);
        Row->AddSlot().FillWidth(0.18f) [ MakeCell(PresetNames[i], i) ];
        Row->AddSlot().FillWidth(0.15f) [ MakeCell(Radii[i], i) ];
        Row->AddSlot().FillWidth(0.15f) [ MakeCell(Voxels[i], i) ];
        Row->AddSlot().FillWidth(0.17f) [ MakeCell(BestFor[i], i) ];
        Row->AddSlot().FillWidth(0.15f) [ MakeCell(Fog[i], i) ];
        Row->AddSlot().FillWidth(0.20f) [ MakeCell(Lighting[i], i) ];
        VBox->AddSlot().AutoHeight() [ Row.ToSharedRef() ];
    }

    return SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(IOCWizard::PanelBg)
        .Padding(1)
        [
            VBox.ToSharedRef()
        ];
}

// ============================================================================
// Feature 7: Existing Actor Selector
// ============================================================================
TSharedRef<SWidget> SIOCSetupWizard::BuildExistingActorSelector()
{
    TSharedPtr<SVerticalBox> VBox;
    SAssignNew(VBox, SVerticalBox);

    VBox->AddSlot().AutoHeight()
    [
        SNew(SBorder)
        .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::CardPanel))
        .Padding(FMargin(14, 10))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 10, 0))
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() -> ECheckBoxState {
                        return bReconfigureExisting ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
                        bReconfigureExisting = (State == ECheckBoxState::Checked);
                        RequestPreviewRefresh(0.0);
                        RequestValidationRefresh(0.0);
                    })
                ]
                + SHorizontalBox::Slot().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(INVTEXT("Reconfigure an existing cave actor instead of spawning a new one"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                    .ColorAndOpacity(IOCWizard::TextPrimary)
                ]
                + SHorizontalBox::Slot().FillWidth(1.0f)
                + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(10, 0, 0, 0))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                    .OnClicked(this, &SIOCSetupWizard::OnRefreshExistingCaves)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Refresh List"), IOCWizard::TextSecondary)
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(28, 6, 0, 0))
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText {
                    return ExistingCaves.Num() > 0
                        ? FText::FromString(FString::Printf(TEXT("%d existing cave actor%s detected. The list refreshes while the wizard is open."),
                            ExistingCaves.Num(), ExistingCaves.Num() == 1 ? TEXT("") : TEXT("s")))
                        : INVTEXT("No existing cave actors are available yet. You can still proceed with a clean spawn, and this list will update automatically if one appears.");
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                .ColorAndOpacity(IOCWizard::TextDim)
                .WrapTextAt(920)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(28, 6, 0, 0))
            [
                SNew(SBorder)
                .Visibility_Lambda([this]() -> EVisibility {
                    return (bReconfigureExisting && ExistingCaves.Num() == 0) ? EVisibility::Visible : EVisibility::Collapsed;
                })
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor(FLinearColor(0.12f, 0.055f, 0.045f))
                .Padding(FMargin(10, 8))
                [
                    SNew(STextBlock)
                    .Text(INVTEXT("Reconfigure mode is on, but there is no compatible cave actor to target yet. Either refresh the list or switch back to spawning a new actor."))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                    .ColorAndOpacity(IOCWizard::TextPrimary)
                    .WrapTextAt(900)
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(28, 8, 0, 0))
            [
                SNew(SBorder)
                .Visibility_Lambda([this]() -> EVisibility {
                    return bReconfigureExisting ? EVisibility::Visible : EVisibility::Collapsed;
                })
                .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::GlassPanel))
                .Padding(FMargin(10, 6))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(INVTEXT("Select which actor to reconfigure:"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                        .ColorAndOpacity(IOCWizard::TextDim)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 4, 0, 0))
                    [
                        SAssignNew(ExistingActorListContainer, SVerticalBox)
                    ]
                ]
            ]
        ]
    ];

    RebuildExistingActorList();
    return VBox.ToSharedRef();
}

// ============================================================================
// Environment Page (Feature 8: environment defaults per preset)
// ============================================================================
TSharedRef<SWidget> SIOCSetupWizard::BuildEnvironmentPage()
{
    return SNew(SVerticalBox)

    + SVerticalBox::Slot().AutoHeight()
    [
        SNew(STextBlock)
        .Text(INVTEXT("Environment Setup"))
        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
        .ColorAndOpacity(IOCWizard::TextPrimary)
    ]

    + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 6, 0, 10))
    [
        SNew(STextBlock)
        .Text(INVTEXT("Tune the immediate playtest setup around the cave so the first run feels intentional instead of bare."))
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
        .ColorAndOpacity(IOCWizard::TextSecondary)
        .WrapTextAt(980)
    ]

    + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 0, 0, 10))
    [
        SNew(SWrapBox)
                .UseAllottedSize(true)
                .AddMetaData(MakeShared<FTagMetaData>(IOCWizardTags::WrapBox))
        + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
        [
            IOCWizard::MakeBadge(
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return FText::FromString(FString::Printf(TEXT("Preset: %s"), *GetSelectedPresetName()));
                }),
                IOCWizard::BadgeBg,
                IOCWizard::AccentColor)
        ]
        + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
        [
            IOCWizard::MakeBadge(
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return bAddLighting ? INVTEXT("Lighting setup enabled") : INVTEXT("Lighting setup skipped");
                }),
                FLinearColor(0.05f, 0.08f, 0.06f),
                bAddLighting ? FLinearColor(0.45f, 0.84f, 0.56f) : IOCWizard::TextSecondary)
        ]
        + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
        [
            IOCWizard::MakeBadge(
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return bAddPlayer ? INVTEXT("Playtest character enabled") : INVTEXT("Playtest character skipped");
                }),
                FLinearColor(0.06f, 0.08f, 0.12f),
                IOCWizard::TextSecondary)
        ]
    ]

    // Feature 8: Recommended settings panel
    + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 0, 0, 12))
    [
        SNew(SBorder)
        .Visibility_Lambda([this]() -> EVisibility {
            return bIsCustom ? EVisibility::Collapsed : EVisibility::Visible;
        })
        .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::CardPanel))
        .Padding(FMargin(14, 10))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 8, 0))
                [
                    IOCWizard::MakeBadge(INVTEXT("Preset guidance"), IOCWizard::BadgeBg, IOCWizard::AccentColor)
                ]
                + SHorizontalBox::Slot().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() -> FText {
                        int32 Idx = FMath::Clamp(SelectedPresetIndex, 0, 4);
                        const auto& P = IOCWizard::Presets[Idx];
                        FString Msg = FString::Printf(TEXT("Recommended for %s: Fog density %.3f, %s"),
                            *P.Name, P.RecommendedFogDensity,
                            P.bRecommendsSkyLight ? TEXT("Sky Light enabled") : TEXT("Point lights recommended"));
                        return FText::FromString(Msg);
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Italic", 10))
                    .ColorAndOpacity(IOCWizard::TextSecondary)
                    .WrapTextAt(800)
                ]
            ]
        ]
    ]

    + SVerticalBox::Slot().AutoHeight()
    [
        SNew(SBorder)
        .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::CardPanel))
        .Padding(FMargin(18, 16))
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() -> ECheckBoxState {
                        return bAddLighting ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
                        bAddLighting = (State == ECheckBoxState::Checked);
                    })
                ]
                + SHorizontalBox::Slot().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(INVTEXT("Set up lighting (Directional Light, Sky Light, Fog, Post-Process Volume)"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                    .ColorAndOpacity(IOCWizard::TextPrimary)
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(30, 5, 0, 14))
            [
                SNew(STextBlock)
                .Text(INVTEXT("Provides proper environment lighting, exposure, and atmospheric fog so caves render correctly."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 10))
                .ColorAndOpacity(IOCWizard::TextDim)
                .WrapTextAt(720)
            ]

            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SSeparator)
                .ColorAndOpacity(FLinearColor(0.1f, 0.1f, 0.13f))
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 14, 0, 0))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() -> ECheckBoxState {
                        return bAddPlayer ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
                        bAddPlayer = (State == ECheckBoxState::Checked);
                    })
                ]
                + SHorizontalBox::Slot().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(INVTEXT("Spawn playable character for testing"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                    .ColorAndOpacity(IOCWizard::TextPrimary)
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(30, 5, 0, 0))
            [
                SNew(STextBlock)
                .Text(INVTEXT("Adds an IOC Character with a flashlight for Play-In-Editor exploration."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 10))
                .ColorAndOpacity(IOCWizard::TextDim)
                .WrapTextAt(720)
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 14, 0, 0))
            [
                SNew(SSeparator)
                .ColorAndOpacity(FLinearColor(0.1f, 0.1f, 0.13f))
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 14, 0, 0))
            [
                SNew(STextBlock)
                .Text(INVTEXT("Production Options"))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                .ColorAndOpacity(IOCWizard::TextPrimary)
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() -> ECheckBoxState { return WizardSettings.bGenerateSmartColors ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { WizardSettings.bGenerateSmartColors = (State == ECheckBoxState::Checked); })
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 22, 0)
                [
                    SNew(STextBlock).Text(INVTEXT("Smart vertex colors"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).ColorAndOpacity(IOCWizard::TextSecondary)
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() -> ECheckBoxState { return WizardSettings.bEnableLOD ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { WizardSettings.bEnableLOD = (State == ECheckBoxState::Checked); })
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 22, 0)
                [
                    SNew(STextBlock).Text(INVTEXT("Generate LOD mesh"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).ColorAndOpacity(IOCWizard::TextSecondary)
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() -> ECheckBoxState { return WizardSettings.bShowDebugViz ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { WizardSettings.bShowDebugViz = (State == ECheckBoxState::Checked); })
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text(INVTEXT("Debug bounds"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11)).ColorAndOpacity(IOCWizard::TextSecondary)
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(0.5f).Padding(0, 0, 10, 0)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
                    [
                        SNew(STextBlock).Text(INVTEXT("LOD Distance"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                        .ColorAndOpacity(IOCWizard::TextDim)
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SSpinBox<float>)
                .IsEnabled_Lambda([this]() -> bool { return WizardSettings.bEnableLOD; })
                        .Value_Lambda([this]() -> float { return CustomLODDistance; })
                        .OnValueChanged_Lambda([this](float v) { CustomLODDistance = v; })
                        .MinValue(500.0f).MaxValue(100000.0f).MinSliderValue(1000.0f).MaxSliderValue(20000.0f).Delta(250.0f)
                    ]
                ]
                + SHorizontalBox::Slot().FillWidth(0.5f)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
                    [
                        SNew(STextBlock).Text(INVTEXT("LOD Voxel Multiplier"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                        .ColorAndOpacity(IOCWizard::TextDim)
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SSpinBox<float>)
                        .IsEnabled_Lambda([this]() -> bool { return WizardSettings.bEnableLOD; })
                        .Value_Lambda([this]() -> float { return CustomLODMultiplier; })
                        .OnValueChanged_Lambda([this](float v) { CustomLODMultiplier = v; })
                        .MinValue(1.0f).MaxValue(12.0f).MinSliderValue(1.0f).MaxSliderValue(8.0f).Delta(0.25f)
                    ]
                ]
            ]

            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() -> ECheckBoxState { return WizardSettings.bAutoRebuildNavMesh ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { WizardSettings.bAutoRebuildNavMesh = (State == ECheckBoxState::Checked); })
                ]
                + SHorizontalBox::Slot().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(INVTEXT("Rebuild navigation after generation"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                    .ColorAndOpacity(IOCWizard::TextSecondary)
                ]
            ]
        ]
    ]

    // Feature 9: Progress bar area
    + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 16, 0, 0))
    [
        SNew(SBorder)
        .Visibility_Lambda([this]() -> EVisibility {
            return bIsGenerating ? EVisibility::Visible : EVisibility::Collapsed;
        })
        .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::CardPanel))
        .Padding(FMargin(16, 12))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SProgressBar)
                .BarFillType(EProgressBarFillType::LeftToRight)
                .Percent_Lambda([this]() -> TOptional<float> { return GenerationProgress; })
                .FillColorAndOpacity(IOCWizard::AccentColor)
                .BorderPadding(FVector2D(2, 2))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 6, 0, 0))
            [
                SNew(STextBlock)
                .Text(INVTEXT("Generating cave..."))
                .Font(FCoreStyle::GetDefaultFontStyle("Italic", 10))
                .ColorAndOpacity(IOCWizard::TextSecondary)
            ]
        ]
    ]

    + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 22, 0, 0))
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor(FLinearColor(0.04f, 0.06f, 0.04f))
        .Padding(FMargin(18, 16))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 6, 0))
                [
                    SNew(STextBlock)
                    .Text(INVTEXT("[v]"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                    .ColorAndOpacity(FLinearColor(0.4f, 0.8f, 0.4f))
                ]
                + SHorizontalBox::Slot().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(INVTEXT("Ready to generate:  "))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                    .ColorAndOpacity(FLinearColor(0.5f, 0.8f, 0.5f))
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor(IOCWizard::AccentColor * 0.2f + FLinearColor(0.03f, 0.03f, 0.04f))
                    .Padding(FMargin(10, 4))
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() -> FText {
                            return FText::FromString(GetSelectedPresetName());
                        })
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                        .ColorAndOpacity(IOCWizard::AccentColor)
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
            [
                SNew(STextBlock)
                .Text(INVTEXT("Move to Review Setup next to confirm readiness, starter content, and generation cost before the cave build begins."))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                .ColorAndOpacity(IOCWizard::TextDim)
            ]
        ]
    ];
}

// ============================================================================
// Review / Advanced Page
// ============================================================================
TSharedRef<SWidget> SIOCSetupWizard::BuildAdvancedPage()
{
    TSharedPtr<SVerticalBox> VBox;
    SAssignNew(VBox, SVerticalBox);

    VBox->AddSlot().AutoHeight()
    [
        IOCWizard::MakeSectionHeader(INVTEXT("Review Setup"), 18)
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 6, 0, 12))
    [
        SNew(STextBlock)
        .Text(INVTEXT("Live readiness checks run here automatically. Resolve blocking issues, review generation cost, and prepare reusable outputs before generation starts."))
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
        .ColorAndOpacity(IOCWizard::TextSecondary)
        .WrapTextAt(980)
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 0, 0, 14))
    [
        SNew(SWrapBox)
                .UseAllottedSize(true)
                .AddMetaData(MakeShared<FTagMetaData>(IOCWizardTags::WrapBox))
        + SWrapBox::Slot().Padding(FMargin(0, 0, 10, 10))
        [
            IOCWizard::MakeMetricCard(
                INVTEXT("Preset"),
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return FText::FromString(GetSelectedPresetName());
                }),
                INVTEXT("Current generation style."),
                IOCWizard::AccentColor)
        ]
        + SWrapBox::Slot().Padding(FMargin(0, 0, 10, 10))
        [
            IOCWizard::MakeMetricCard(
                INVTEXT("Target"),
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return bReconfigureExisting ? INVTEXT("Reconfigure existing") : INVTEXT("Spawn new actor");
                }),
                INVTEXT("Where the setup will be applied."),
                FLinearColor(0.53f, 0.79f, 0.98f))
        ]
        + SWrapBox::Slot().Padding(FMargin(0, 0, 10, 10))
        [
            IOCWizard::MakeMetricCard(
                INVTEXT("Validation"),
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return ViewModel.bValidationPassed ? INVTEXT("Passing") : INVTEXT("Attention needed");
                }),
                INVTEXT("Generate stays locked until validation passes."),
                ViewModel.bValidationPassed ? FLinearColor(0.48f, 0.86f, 0.58f) : FLinearColor(0.96f, 0.39f, 0.29f))
        ]
        + SWrapBox::Slot().Padding(FMargin(0, 0, 10, 10))
        [
            IOCWizard::MakeMetricCard(
                INVTEXT("Workload"),
                TAttribute<FText>::CreateLambda([this]() -> FText {
                    return FText::FromString(FString::Printf(TEXT("%.2fM voxels"), BuildPreflightEstimate().EstimatedMillions));
                }),
                INVTEXT("Live estimate before generation begins."),
                IOCWizard::GetRiskTint(BuildPreflightEstimate().RiskLabel))
        ]
    ];

    VBox->AddSlot().AutoHeight()
    [
        SNew(SExpandableArea)
        .InitiallyCollapsed(false)
        .BorderBackgroundColor(IOCWizard::CardBg)
        .HeaderContent()
        [
            SNew(STextBlock)
            .Text(INVTEXT("Readiness"))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
            .ColorAndOpacity(IOCWizard::TextPrimary)
        ]
        .BodyContent()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this]() -> FLinearColor {
                    return ViewModel.bValidationPassed
                        ? FLinearColor(0.04f, 0.065f, 0.05f)
                        : FLinearColor(0.14f, 0.055f, 0.045f);
                })
                .Padding(FMargin(14, 10))
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() -> FText {
                        return ViewModel.bValidationPassed
                            ? INVTEXT("Ready to generate. Live readiness checks are currently passing.")
                            : INVTEXT("Resolve blocking readiness issues below. Generate & Continue stays disabled until validation passes.");
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                    .ColorAndOpacity(IOCWizard::TextPrimary)
                    .WrapTextAt(950)
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText {
                    return InstallationStatus.IsEmpty()
                        ? INVTEXT("Running validation...")
                        : FText::FromString(InstallationStatus);
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
                .ColorAndOpacity(IOCWizard::TextSecondary)
                .WrapTextAt(950)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 12, 0, 0))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0, 0, 8, 0))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                    .OnClicked(this, &SIOCSetupWizard::OnValidateInstallation)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Validate Now"), IOCWizard::TextPrimary)
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0, 0, 8, 0))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonPrimary))
                    .OnClicked(this, &SIOCSetupWizard::OnRunStarterAssetSetup)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Prepare Starter Assets"), IOCWizard::AccentColor)
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0, 0, 8, 0))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonPrimary))
                    .OnClicked(this, &SIOCSetupWizard::OnCreateStarterLevel)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Create Starter Level"), IOCWizard::TextPrimary)
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0, 0, 8, 0))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                    .OnClicked(this, &SIOCSetupWizard::OnOpenStarterAssets)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Open Starter Assets"), IOCWizard::TextPrimary)
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                    .OnClicked(this, &SIOCSetupWizard::OnOpenShowcaseMap)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Open Starter Level"), IOCWizard::TextPrimary)
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText {
                    return StarterAssetStatus.IsEmpty()
                        ? INVTEXT("Starter assets: not prepared yet.")
                        : FText::FromString(StarterAssetStatus);
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                .ColorAndOpacity(IOCWizard::TextDim)
                .WrapTextAt(950)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 4, 0, 0))
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText {
                    return StarterLevelStatus.IsEmpty()
                        ? INVTEXT("Starter level: not created yet.")
                        : FText::FromString(StarterLevelStatus);
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                .ColorAndOpacity(IOCWizard::TextDim)
                .WrapTextAt(950)
            ]
        ]
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 14, 0, 0))
    [
        SNew(SExpandableArea)
        .InitiallyCollapsed(false)
        .BorderBackgroundColor(IOCWizard::CardBg)
        .HeaderContent()
        [
            SNew(STextBlock)
            .Text(INVTEXT("Generation Cost"))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
            .ColorAndOpacity(IOCWizard::TextPrimary)
        ]
        .BodyContent()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this]() -> FLinearColor {
                    return IOCWizard::GetRiskSurface(BuildPreflightEstimate().RiskLabel);
                })
                .Padding(FMargin(18, 14))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() -> FText {
                            const FIOCPreflightEstimate Estimate = BuildPreflightEstimate();
                            return FText::FromString(FString::Printf(
                                TEXT("Estimated workload: %.2fM voxels | Risk: %s | Complexity x%.2f"),
                                Estimate.EstimatedMillions,
                                *Estimate.RiskLabel,
                                Estimate.ComplexityMultiplier));
                        })
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                        .ColorAndOpacity(IOCWizard::TextPrimary)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() -> FText {
                            return FText::FromString(GetPreflightSummary());
                        })
                        .Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
                        .ColorAndOpacity(IOCWizard::TextSecondary)
                        .WrapTextAt(950)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() -> FText {
                            return FText::FromString(GetRecommendationText());
                        })
                        .Font(FCoreStyle::GetDefaultFontStyle("Italic", 10))
                        .ColorAndOpacity(IOCWizard::AccentColor)
                        .WrapTextAt(950)
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 12, 0, 0))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0, 0, 8, 0))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                    .OnClicked(this, &SIOCSetupWizard::OnUseSaferVoxelSize)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Use Safer Voxel Size"), IOCWizard::TextPrimary)
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0, 0, 8, 0))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                    .OnClicked(this, &SIOCSetupWizard::OnReduceBounds)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Reduce Bounds"), IOCWizard::TextPrimary)
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                    .OnClicked(this, &SIOCSetupWizard::OnEnableFixedTunnelBounds)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Enable Fixed Tunnel Bounds"), IOCWizard::TextPrimary)
                    ]
                ]
            ]
        ]
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 14, 0, 0))
    [
        SNew(SExpandableArea)
        .InitiallyCollapsed(false)
        .BorderBackgroundColor(IOCWizard::CardBg)
        .HeaderContent()
        [
            SNew(STextBlock)
            .Text(INVTEXT("Outputs"))
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
            .ColorAndOpacity(IOCWizard::TextPrimary)
        ]
        .BodyContent()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
            [
                SNew(STextBlock)
                .Text(INVTEXT("Save a reusable setup profile and line up the post-generation workflow you expect to use next."))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                .ColorAndOpacity(IOCWizard::TextDim)
                .WrapTextAt(920)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(FMargin(0, 0, 10, 0))
                [
                    SNew(SEditableTextBox)
                    .Text_Lambda([this]() -> FText {
                        return FText::FromString(ProfileName);
                    })
                    .OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type) {
                        ProfileName = NewText.ToString().TrimStartAndEnd();
                        if (ProfileName.IsEmpty())
                        {
                            ProfileName = TEXT("My Cave Setup");
                        }
                    })
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0, 0, 8, 0))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                    .OnClicked(this, &SIOCSetupWizard::OnSaveProfile)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Save"), IOCWizard::TextPrimary)
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0, 0, 8, 0))
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                    .OnClicked(this, &SIOCSetupWizard::OnLoadProfile)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Load"), IOCWizard::TextPrimary)
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(SButton)
                    .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                    .OnClicked(this, &SIOCSetupWizard::OnCopyProfile)
                    .Content()
                    [
                        IOCWizard::MakeActionLabel(INVTEXT("Copy Export"), IOCWizard::TextPrimary)
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 8, 0, 0))
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText {
                    return ProfileStatus.IsEmpty()
                        ? INVTEXT("Save this setup to reapply it later.")
                        : FText::FromString(ProfileStatus);
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                .ColorAndOpacity(IOCWizard::TextDim)
                .WrapTextAt(920)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 14, 0, 0))
            [
                SNew(STextBlock)
                .Text(INVTEXT("After generation you can add carving volumes for guaranteed rooms, place AIOCStreamingManager for large worlds, or bake the final cave to static mesh once layout is approved."))
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                .ColorAndOpacity(IOCWizard::TextDim)
                .WrapTextAt(920)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 14, 0, 0))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 12, 0))
                [
                    SNew(SCheckBox)
                    .IsChecked_Lambda([this]() -> ECheckBoxState {
                        return bAutoFocusConfiguredCave ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                    })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State) {
                        bAutoFocusConfiguredCave = (State == ECheckBoxState::Checked);
                    })
                ]
                + SHorizontalBox::Slot().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(INVTEXT("After generation, select the cave actor and open the Details panel"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                    .ColorAndOpacity(IOCWizard::TextSecondary)
                ]
            ]
        ]
    ];

    return VBox.ToSharedRef();
}

// ============================================================================
// Finish Page (Feature 2: stats, Feature 4: copy commands)
// ============================================================================
TSharedRef<SWidget> SIOCSetupWizard::BuildFinishPage()
{
    TSharedPtr<SVerticalBox> VBox;
    SAssignNew(VBox, SVerticalBox);

    VBox->AddSlot().AutoHeight()
    [
        SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
        .BorderBackgroundColor_Lambda([this]() -> FLinearColor {
            if (bGenerationDone) return FLinearColor(0.04f, 0.065f, 0.05f);
            if (bIsGenerating) return FLinearColor(0.08f, 0.07f, 0.03f);
            if (!SetupError.IsEmpty()) return FLinearColor(0.14f, 0.055f, 0.045f);
            return FLinearColor(0.04f, 0.045f, 0.06f);
        })
        .Padding(FMargin(18, 14))
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 10, 0))
                [
                    SNew(SBorder)
                    .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                    .BorderBackgroundColor_Lambda([this]() -> FLinearColor {
                        if (bGenerationDone) return FLinearColor(0.05f, 0.08f, 0.06f);
                        if (bIsGenerating) return FLinearColor(0.08f, 0.07f, 0.03f);
                        if (!SetupError.IsEmpty()) return FLinearColor(0.14f, 0.055f, 0.045f);
                        return IOCWizard::GlassBg;
                    })
                    .Padding(FMargin(12, 6))
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() -> FText {
                            if (bGenerationDone) return INVTEXT("Complete");
                            if (bIsGenerating) return INVTEXT("Working");
                            return SetupError.IsEmpty() ? INVTEXT("Idle") : INVTEXT("Blocked");
                        })
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
                        .ColorAndOpacity_Lambda([this]() -> FSlateColor {
                            if (bGenerationDone) return FLinearColor(0.45f, 0.84f, 0.56f);
                            if (bIsGenerating) return IOCWizard::AccentColor;
                            return SetupError.IsEmpty() ? IOCWizard::TextSecondary : FLinearColor(0.95f, 0.52f, 0.36f);
                        })
                    ]
                ]
                + SHorizontalBox::Slot().VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() -> FText {
                        if (bGenerationDone) return INVTEXT("Setup Complete");
                        if (bIsGenerating) return INVTEXT("Setup In Progress");
                        return SetupError.IsEmpty() ? INVTEXT("Setup Not Run") : INVTEXT("Setup Failed");
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
                    .ColorAndOpacity_Lambda([this]() -> FSlateColor {
                        if (bGenerationDone) return FLinearColor(0.4f, 0.85f, 0.4f);
                        if (bIsGenerating) return IOCWizard::AccentColor;
                        return SetupError.IsEmpty()
                            ? IOCWizard::AccentDim
                            : FLinearColor(0.9f, 0.35f, 0.25f);
                    })
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
            [
                SNew(STextBlock)
                .Text_Lambda([this]() -> FText {
                    if (bIsGenerating && SetupError.IsEmpty())
                    {
                        return INVTEXT("The cave actor is configured and generating asynchronously. This page will update when final metrics are ready.");
                    }
                    if (!SetupError.IsEmpty())
                    {
                        return FText::FromString(SetupError);
                    }
                    return INVTEXT("No cave was generated yet. Go back to adjust setup options or run a showcase demo below.");
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                .ColorAndOpacity(IOCWizard::TextSecondary)
                .WrapTextAt(980)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
            [
                SNew(SWrapBox)
                .UseAllottedSize(true)
                .AddMetaData(MakeShared<FTagMetaData>(IOCWizardTags::WrapBox))
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    IOCWizard::MakeBadge(
                        TAttribute<FText>::CreateLambda([this]() -> FText {
                            return FText::FromString(FString::Printf(TEXT("Preset: %s"), *GetSelectedPresetName()));
                        }),
                        IOCWizard::BadgeBg,
                        IOCWizard::AccentColor)
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    IOCWizard::MakeBadge(
                        TAttribute<FText>::CreateLambda([this]() -> FText {
                            return ViewModel.bLastSetupReconfiguredExisting ? INVTEXT("Applied to existing cave") : INVTEXT("Spawned new cave actor");
                        }),
                        FLinearColor(0.05f, 0.08f, 0.06f),
                        IOCWizard::TextSecondary)
                ]
                + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
                [
                    IOCWizard::MakeBadge(
                        TAttribute<FText>::CreateLambda([this]() -> FText {
                            return WizardSettings.bEnableLOD ? INVTEXT("LOD generated") : INVTEXT("LOD skipped");
                        }),
                        FLinearColor(0.06f, 0.08f, 0.12f),
                        IOCWizard::TextSecondary)
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 12, 0, 0))
            [
                SNew(SBorder)
                .Visibility_Lambda([this]() -> EVisibility {
                    return GenerationSummary.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
                })
                .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::GlassPanel))
                .Padding(FMargin(12, 10))
                [
                    SNew(STextBlock)
                    .Text_Lambda([this]() -> FText {
                        return FText::FromString(GenerationSummary);
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
                    .ColorAndOpacity(IOCWizard::TextSecondary)
                    .WrapTextAt(950)
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 12, 0, 0))
            [
                SNew(SProgressBar)
                .Visibility_Lambda([this]() -> EVisibility {
                    return bIsGenerating ? EVisibility::Visible : EVisibility::Collapsed;
                })
                .Percent_Lambda([this]() -> TOptional<float> {
                    return FMath::Clamp(GenerationProgress, 0.0f, 1.0f);
                })
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
            [
                SNew(SBorder)
                .Visibility_Lambda([this]() -> EVisibility {
                    return GenerationStats.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
                })
                .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::GlassPanel))
                .Padding(FMargin(12, 10))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock)
                        .Text(INVTEXT("Generation Statistics"))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
                        .ColorAndOpacity(IOCWizard::AccentColor)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 6, 0, 0))
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]() -> FText {
                            return FText::FromString(GenerationStats);
                        })
                        .Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
                        .ColorAndOpacity(IOCWizard::TextSecondary)
                        .WrapTextAt(950)
                    ]
                ]
            ]
        ]
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 22, 0, 8))
    [
        IOCWizard::MakeSectionHeader(INVTEXT("Quick Actions"))
    ];

    VBox->AddSlot().AutoHeight()
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SWrapBox)
                .UseAllottedSize(true)
                .AddMetaData(MakeShared<FTagMetaData>(IOCWizardTags::WrapBox))
            + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
            [
                SNew(SButton)
                .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                .IsEnabled_Lambda([this]() -> bool { return LastConfiguredCave.IsValid(); })
                .OnClicked(this, &SIOCSetupWizard::OnSelectConfiguredCave)
                .Content()
                [
                    IOCWizard::MakeActionLabel(INVTEXT("Select Cave"), IOCWizard::TextPrimary)
                ]
            ]
            + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
            [
                SNew(SButton)
                .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                .IsEnabled_Lambda([this]() -> bool { return LastConfiguredCave.IsValid(); })
                .OnClicked(this, &SIOCSetupWizard::OnRefreshStats)
                .Content()
                [
                    IOCWizard::MakeActionLabel(INVTEXT("Refresh Stats"), IOCWizard::TextPrimary)
                ]
            ]
            + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
            [
                SNew(SButton)
                .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                .IsEnabled_Lambda([this]() -> bool { return LastConfiguredCave.IsValid(); })
                .OnClicked(this, &SIOCSetupWizard::OnAddCarvingVolume)
                .Content()
                [
                    IOCWizard::MakeActionLabel(INVTEXT("Add Carving Volume"), IOCWizard::TextPrimary)
                ]
            ]
            + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
            [
                SNew(SButton)
                .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                .OnClicked(this, &SIOCSetupWizard::OnAddStreamingManager)
                .Content()
                [
                    IOCWizard::MakeActionLabel(INVTEXT("Add Streaming Manager"), IOCWizard::TextPrimary)
                ]
            ]
            + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
            [
                SNew(SButton)
                .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                .OnClicked(this, &SIOCSetupWizard::OnOpenShowcaseMap)
                .Content()
                [
                    IOCWizard::MakeActionLabel(INVTEXT("Open Starter Level"), IOCWizard::TextPrimary)
                ]
            ]
            + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
            [
                SNew(SButton)
                .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonDanger))
                .Visibility_Lambda([this]() -> EVisibility {
                    return HasRollbackSnapshot() ? EVisibility::Visible : EVisibility::Collapsed;
                })
                .OnClicked(this, &SIOCSetupWizard::OnRestorePreviousSettings)
                .Content()
                [
                    IOCWizard::MakeActionLabel(INVTEXT("Restore Previous Settings"), FLinearColor(0.9f, 0.55f, 0.4f))
                ]
            ]
            + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
            [
                SNew(SButton)
                .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonPrimary))
                .OnClicked(this, &SIOCSetupWizard::OnRunShowcase)
                .Content()
                [
                    IOCWizard::MakeActionLabel(INVTEXT("Run Showcase Demo"), IOCWizard::AccentColor)
                ]
            ]
            + SWrapBox::Slot().Padding(FMargin(0, 0, 8, 8))
            [
                SNew(SButton)
                .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonDanger))
                .OnClicked(this, &SIOCSetupWizard::OnClearAllDemos)
                .ToolTipText(INVTEXT("Remove every actor the demos spawned, putting the level back as you found it."))
                .Content()
                [
                    IOCWizard::MakeActionLabel(INVTEXT("Clear All Demos"), IOCWizard::TextPrimary)
                ]
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(FMargin(0, 10, 0, 0))
        [
            SNew(STextBlock)
            .Text(INVTEXT("Use the cave actor Details panel to regenerate, decorate, and bake once the layout is approved."))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
            .ColorAndOpacity(IOCWizard::TextSecondary)
            .WrapTextAt(980)
        ]
    ];

    VBox->AddSlot().AutoHeight().Padding(FMargin(0, 18, 0, 6))
    [
        SNew(STextBlock)
        .Text(INVTEXT("Console Commands"))
        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
        .ColorAndOpacity(IOCWizard::TextPrimary)
    ];

    struct FCmdEntry { FString Cmd; FString Desc; };
    static const FCmdEntry Commands[] = {
        { TEXT("IOC.SpawnTunnelDemo"), TEXT("Playable tunnel with character and lighting") },
        { TEXT("IOC.SpawnSpectacular"), TEXT("Crystal cave demo with bloom and scatter") },
        { TEXT("IOC.SpawnShowcase"), TEXT("8-section automated cinematic flythrough") },
        { TEXT("IOC.ClearShowcase"), TEXT("Remove the showcase and restore the camera") },
        { TEXT("IOC.ClearAllDemos"), TEXT("Remove every demo actor, including the tunnel and spectacular demos") },
        { TEXT("IOC.ValidateInstallation"), TEXT("Write installation diagnostics to the Output Log") },
        { TEXT("IOC.OpenSetupWizard"), TEXT("Reopen this wizard from the console") },
    };

    TSharedPtr<SVerticalBox> CmdBox;
    SAssignNew(CmdBox, SVerticalBox);
    for (int32 i = 0; i < UE_ARRAY_COUNT(Commands); i++)
    {
        CmdBox->AddSlot().AutoHeight().Padding(0, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth()
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Commands[i].Cmd))
                    .Font(FCoreStyle::GetDefaultFontStyle("Mono", 10))
                    .ColorAndOpacity(IOCWizard::AccentColor)
                ]
                + SHorizontalBox::Slot().Padding(FMargin(12, 0, 0, 0))
                [
                    SNew(STextBlock)
                    .Text(FText::FromString(Commands[i].Desc))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
                    .ColorAndOpacity(IOCWizard::TextDim)
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(FMargin(8, 0, 0, 0))
            [
                SNew(SButton)
                .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
                .OnClicked_Lambda([Cmd = Commands[i].Cmd]() -> FReply {
                    FPlatformApplicationMisc::ClipboardCopy(*Cmd);
                    return FReply::Handled();
                })
                .Content()
                [
                    SNew(STextBlock)
                    .Text(INVTEXT("Copy"))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
                    .ColorAndOpacity(IOCWizard::TextSecondary)
                ]
            ]
        ];
    }

    VBox->AddSlot().AutoHeight()
    [
        SNew(SBorder)
        .BorderImage(FIOCWizardStyle::Get().GetBrush(FIOCWizardStyle::GlassPanel))
        .Padding(FMargin(16, 12))
        [
            CmdBox.ToSharedRef()
        ]
    ];

    return VBox.ToSharedRef();
}

// ============================================================================
// Navigation handlers
// ============================================================================
FReply SIOCSetupWizard::OnNext()
{
    if (CurrentPage == 3)
    {
        if (!CanRunSetup())
        {
            return FReply::Handled();
        }
        PerformSetup();
    }

    if (CurrentPage < TotalPages - 1)
    {
        SetCurrentPage(CurrentPage + 1);
    }
    else
    {
        SaveSettings();
        if (ParentWindow.IsValid())
        {
            ParentWindow.Pin()->RequestDestroyWindow();
        }
    }

    return FReply::Handled();
}

FReply SIOCSetupWizard::OnBack()
{
    if (CurrentPage > 0)
    {
        SetCurrentPage(CurrentPage - 1);
    }
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnCancel()
{
    DestroyPreviewActor();
    if (ParentWindow.IsValid())
    {
        ParentWindow.Pin()->RequestDestroyWindow();
    }
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnRunShowcase()
{
    if (ParentWindow.IsValid())
    {
        ParentWindow.Pin()->RequestDestroyWindow();
    }

    FInstantOrganicCavesModule::SpawnShowcase();

    return FReply::Handled();
}

// Feature 3: Open documentation
FReply SIOCSetupWizard::OnOpenDocumentation()
{
    FInstantOrganicCavesEditorModule::OpenDocumentation();
    return FReply::Handled();
}

// Feature 4: Copy command (used in lambda, but also available as method)
FReply SIOCSetupWizard::OnCopyCommand(const FString& Command)
{
    FPlatformApplicationMisc::ClipboardCopy(*Command);
    return FReply::Handled();
}

// Feature 10: Undo last setup
FReply SIOCSetupWizard::OnUndoLastSetup()
{
    const bool bConfiguredCaveWasSpawned = LastConfiguredCave.IsValid()
        && SpawnedActors.ContainsByPredicate([this](const TWeakObjectPtr<AActor>& Actor) {
            return Actor.IsValid() && Actor.Get() == LastConfiguredCave.Get();
        });

    CleanupSpawnedActors();
    if (bConfiguredCaveWasSpawned)
    {
        bGenerationDone = false;
        GenerationSummary.Empty();
        GenerationStats.Empty();
        LastConfiguredCave.Reset();
    }
    else if (HasRollbackSnapshot())
    {
        RestoreRollbackSnapshot();
    }
    RequestExistingCaveRefresh(0.0);
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnRefreshStats()
{
    if (LastConfiguredCave.IsValid())
    {
        CollectGenerationStats(LastConfiguredCave.Get());
    }
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnSelectConfiguredCave()
{
    FocusConfiguredCave();
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnValidateInstallation()
{
    RefreshValidationState(true);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnRunStarterAssetSetup()
{
    FString Status;
    bStarterAssetsPrepared = PrepareStarterAssets(Status);
    StarterAssetStatus = Status;
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnCreateStarterLevel()
{
    FString Status;
    const bool bOk = CreateStarterLevel(Status);
    StarterLevelStatus = bOk
        ? TEXT("Created or opened /Game/IOC_Showcase. Press Play to run the capture-ready starter level.")
        : Status;
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnOpenStarterAssets()
{
    TArray<UObject*> Objects;
    TArray<FString> MissingAssets;
    IOC_LoadStarterAssets(Objects, &MissingAssets);

    if (Objects.Num() == 0)
    {
        StarterAssetStatus = TEXT("Starter assets were not found in plugin content. Reinstall the plugin and validate the content mount.");
        return FReply::Handled();
    }

    if (GEditor)
    {
        GEditor->SyncBrowserToObjects(Objects);
        StarterAssetStatus = MissingAssets.Num() == 0
            ? TEXT("Selected the shipped starter assets in the Content Browser.")
            : FString::Printf(TEXT("Selected the available starter assets, but these assets are missing from plugin content: %s."),
                *FString::Join(MissingAssets, TEXT(", ")));
    }
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnOpenDemoMap()
{
    const FString MapPackage = IOCWizard::ShippedDemoMapPackagePath;
    if (!FPackageName::DoesPackageExist(MapPackage))
    {
        // Only reachable if the plugin was packaged without its content.
        StarterLevelStatus = TEXT("The shipped demo map is missing from this plugin build.");
        RequestValidationRefresh(0.0);
        return FReply::Handled();
    }

    const FString MapFilename = FPackageName::LongPackageNameToFilename(
        MapPackage, FPackageName::GetMapPackageExtension());
    if (FEditorFileUtils::LoadMap(MapFilename, false, true))
    {
        StarterLevelStatus = TEXT("Opened the IOC demo map. Press Play to start the guided showcase.");
    }
    else
    {
        StarterLevelStatus = TEXT("Failed to open the IOC demo map.");
    }
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnClearAllDemos()
{
    const int32 Removed = FInstantOrganicCavesModule::ClearAllDemos();
    StarterLevelStatus = FString::Printf(
        TEXT("Removed %d demo actor(s) from the level."), Removed);
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnOpenShowcaseMap()
{
    const FString MapPackage = IOCWizard::StarterLevelPackagePath;
    if (!FPackageName::DoesPackageExist(MapPackage))
    {
        StarterLevelStatus = TEXT("/Game/IOC_Showcase does not exist yet. Run Create Starter Level first, "
            "or use Open Demo Map for the shipped demo that needs no setup.");
        return FReply::Handled();
    }

    const FString MapFilename = FPackageName::LongPackageNameToFilename(
        MapPackage, FPackageName::GetMapPackageExtension());
    if (FEditorFileUtils::LoadMap(MapFilename, false, true))
    {
        StarterLevelStatus = TEXT("Opened /Game/IOC_Showcase.");
    }
    else
    {
        StarterLevelStatus = TEXT("Failed to open /Game/IOC_Showcase.");
    }
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnRefreshExistingCaves()
{
    DetectExistingCaves();
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnRestorePreviousSettings()
{
    RestoreRollbackSnapshot();
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnAddCarvingVolume()
{
    if (!LastConfiguredCave.IsValid())
    {
        return FReply::Handled();
    }

    AIOCProceduralActor* Cave = LastConfiguredCave.Get();
    FScopedTransaction Transaction(LOCTEXT("AddIOCCarvingVolume", "Add IOC Carving Volume"));
    Cave->Modify();

    UIOCCarvingComponent* Carve = NewObject<UIOCCarvingComponent>(Cave);
    if (!Carve)
    {
        return FReply::Handled();
    }

    Carve->ShapeType = EIOCCarvingShape::Sphere;
    Carve->SphereRadius = FMath::Max(Cave->TunnelRadius, 250.0f);
    Carve->FalloffRadius = 100.0f;
    Carve->SetupAttachment(Cave->GetRootComponent());
    Carve->RegisterComponent();

    const FVector Midpoint = Cave->bGenerateTunnel
        ? (Cave->TunnelStart + Cave->TunnelEnd) * 0.5f
        : FVector(0.0f, 0.0f, FMath::Max((float)Cave->GenerationBounds.Z * 0.2f, 150.0f));
    Carve->SetRelativeLocation(Midpoint);
    Carve->UpdateComponentToWorld();

    FocusConfiguredCave();
    GenerationSummary = TEXT("Added a new IOC Carving Volume to the configured cave. Adjust its shape in the Details panel, then regenerate the cave actor.");
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnAddStreamingManager()
{
    UWorld* World = IOCWizard::GetEditorWorld();
    if (!World)
    {
        return FReply::Handled();
    }

    FScopedTransaction Transaction(LOCTEXT("AddIOCStreamingManager", "Add IOC Streaming Manager"));
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    const FVector SpawnLocation = LastConfiguredCave.IsValid()
        ? LastConfiguredCave->GetActorLocation()
        : FVector::ZeroVector;

    AIOCStreamingManager* Manager = World->SpawnActor<AIOCStreamingManager>(
        AIOCStreamingManager::StaticClass(), SpawnLocation, FRotator::ZeroRotator, SpawnParams);
    if (!Manager)
    {
        return FReply::Handled();
    }

    if (LastConfiguredCave.IsValid())
    {
        AIOCProceduralActor* Cave = LastConfiguredCave.Get();
        Manager->CavePreset = Cave->CavePreset;
        Manager->BaseSeed = Cave->CaveSeed;
        Manager->VoxelSize = Cave->VoxelSize;
        Manager->NoiseFrequency = Cave->NoiseFrequency;
        Manager->NoiseThreshold = Cave->NoiseThreshold;
        Manager->SmoothIterations = Cave->SmoothIterations;
        Manager->SharedMaterial = Cave->CaveMaterial;
        Manager->TextureTiling = Cave->TextureTiling;
        Manager->SharedDecorationLayers = Cave->DecorationLayers;
        Manager->bGenerateTunnel = Cave->bGenerateTunnel;
        Manager->TunnelRadius = Cave->TunnelRadius;
        Manager->WallThickness = Cave->WallThickness;
        Manager->DomainWarpIntensity = Cave->DomainWarpIntensity;
        Manager->TerraceSteps = Cave->TerraceSteps;
        Manager->bEnableLOD = Cave->bEnableLOD;
        Manager->LODDistance = Cave->LODDistance;
        Manager->LODVoxelSizeMultiplier = Cave->LODVoxelSizeMultiplier;
        Manager->bAutoRebuildNavMesh = Cave->bAutoRebuildNavMesh;
        Manager->ChunkSize = Cave->GenerationBounds.ComponentMax(FVector(2000.0f, 2000.0f, 1500.0f));
    }

#if WITH_EDITOR
    Manager->SetActorLabel(TEXT("IOC_StreamingManager"));
#endif
    SpawnedActors.Add(Manager);
    GenerationSummary = TEXT("Added AIOCStreamingManager with settings copied from the configured cave where possible. Use it for larger streamed cave worlds.");
    RequestExistingCaveRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnConvertPresetToCustom()
{
    if (SelectedPresetIndex >= IOCWizard::CustomIndex)
    {
        return FReply::Handled();
    }

    CopySelectedPresetToCustomSettings();
    const FString PresetName = IOCWizard::Presets[FMath::Clamp(SelectedPresetIndex, 0, IOCWizard::CustomIndex - 1)].Name;
    PreviewStatus = FString::Printf(TEXT("Copied %s into editable custom values. Adjust the parameters below and the preview will rebuild."), *PresetName);
    ProfileStatus = PreviewStatus;
    RequestPreviewRefresh(0.0);
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnRandomizeSeed()
{
    if (!bIsCustom && SelectedPresetIndex < IOCWizard::CustomIndex)
    {
        CopySelectedPresetToCustomSettings();
    }

    CustomSeed = FMath::RandRange(0, 999999);
    PreviewStatus = FString::Printf(TEXT("Randomized custom seed to %d. Preview refresh queued."), CustomSeed);
    ProfileStatus = PreviewStatus;
    RequestPreviewRefresh(0.0);
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnUseSaferVoxelSize()
{
    if (!bIsCustom)
    {
        CopySelectedPresetToCustomSettings();
    }

    if (EstimateVoxelCount() > 8000000)
    {
        CustomVoxelSize = FMath::Max(CustomVoxelSize, 120.0);
    }
    else if (EstimateVoxelCount() > 2000000)
    {
        CustomVoxelSize = FMath::Max(CustomVoxelSize, 80.0);
    }
    else
    {
        CustomVoxelSize = FMath::Max(CustomVoxelSize, 60.0);
    }
    ProfileStatus = TEXT("Applied safer voxel size. Review the updated preflight estimate.");
    PreviewStatus = ProfileStatus;
    RequestPreviewRefresh(0.0);
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnReduceBounds()
{
    if (!bIsCustom)
    {
        CopySelectedPresetToCustomSettings();
    }

    CustomBounds = CustomBounds.ComponentMin(FVector(4000.0f, 4000.0f, 2500.0f));
    if (bCustomTunnelMode)
    {
        const FVector Center = (CustomTunnelStart + CustomTunnelEnd) * 0.5f;
        FVector Direction = (CustomTunnelEnd - CustomTunnelStart).GetSafeNormal();
        if (Direction.IsNearlyZero())
        {
            Direction = FVector::ForwardVector;
        }
        const float HalfLength = 1800.0f;
        CustomTunnelStart = Center - Direction * HalfLength;
        CustomTunnelEnd = Center + Direction * HalfLength;
    }
    ProfileStatus = TEXT("Reduced bounds and tunnel length for a safer first generation.");
    PreviewStatus = ProfileStatus;
    RequestPreviewRefresh(0.0);
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnEnableFixedTunnelBounds()
{
    if (!bIsCustom)
    {
        CopySelectedPresetToCustomSettings();
    }
    bCustomTunnelMode = true;
    WizardSettings.bUseFixedBoundsForTunnel = true;
    CustomBounds = CustomBounds.ComponentMin(FVector(5000.0f, 2400.0f, 1800.0f));
    ProfileStatus = TEXT("Enabled fixed tunnel bounds and capped generation bounds.");
    PreviewStatus = ProfileStatus;
    RequestPreviewRefresh(0.0);
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnSaveProfile()
{
    ProfileName = ProfileName.TrimStartAndEnd();
    if (ProfileName.IsEmpty())
    {
        ProfileName = TEXT("My Cave Setup");
    }

    FString SafeName = ProfileName;
    SafeName.ReplaceInline(TEXT("]"), TEXT("_"));
    SafeName.ReplaceInline(TEXT("["), TEXT("_"));
    const FString SectionName = FString::Printf(TEXT("%s.Profile.%s"), IOCWizard::ConfigSection, *SafeName);

    SaveStateToSection(SectionName);
    GConfig->Flush(false, IOCWizard::ConfigFile);
    ProfileStatus = FString::Printf(TEXT("Saved profile \"%s\"."), *ProfileName);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnLoadProfile()
{
    ProfileName = ProfileName.TrimStartAndEnd();
    if (ProfileName.IsEmpty())
    {
        ProfileName = TEXT("My Cave Setup");
    }

    FString SafeName = ProfileName;
    SafeName.ReplaceInline(TEXT("]"), TEXT("_"));
    SafeName.ReplaceInline(TEXT("["), TEXT("_"));
    const FString SectionName = FString::Printf(TEXT("%s.Profile.%s"), IOCWizard::ConfigSection, *SafeName);

    int32 TestValue = 0;
    if (!GConfig->GetInt(*SectionName, TEXT("SelectedPresetIndex"), TestValue, IOCWizard::ConfigFile))
    {
        ProfileStatus = FString::Printf(TEXT("No saved profile named \"%s\" was found."), *ProfileName);
        return FReply::Handled();
    }

    LoadStateFromSection(SectionName);
    ProfileStatus = FString::Printf(TEXT("Loaded profile \"%s\"."), *ProfileName);
    RequestPreviewRefresh(0.0);
    RequestValidationRefresh(0.0);
    return FReply::Handled();
}

FReply SIOCSetupWizard::OnCopyProfile()
{
    const FString ExportText = ExportProfileText();
    FPlatformApplicationMisc::ClipboardCopy(*ExportText);
    ProfileStatus = TEXT("Copied setup profile export to the clipboard.");
    return FReply::Handled();
}

// Feature 12: Keyboard navigation
FReply SIOCSetupWizard::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Escape)
    {
        return OnCancel();
    }
    if (InKeyEvent.GetKey() == EKeys::Enter)
    {
        return OnNext();
    }
    return FReply::Unhandled();
}

void SIOCSetupWizard::FocusConfiguredCave()
{
    if (GEditor && LastConfiguredCave.IsValid())
    {
        GEditor->SelectNone(false, true, false);
        GEditor->SelectActor(LastConfiguredCave.Get(), true, true);
        GEditor->NoteSelectionChange();
        FGlobalTabmanager::Get()->TryInvokeTab(FTabId(FName(TEXT("LevelEditorSelectionDetails"))));
    }
}

void SIOCSetupWizard::CopySelectedPresetToCustomSettings()
{
    const int32 Idx = FMath::Clamp(SelectedPresetIndex, 0, 4);
    bIsCustom = true;
    bCustomTunnelMode = true;
    bCustomUseSpline = false;
    CustomTextureTiling = IOC_GetRecommendedTextureTiling(IOCWizard::Presets[Idx].Preset);

    switch (Idx)
    {
    case 0:
        CustomTunnelRadius = 450.0f;
        CustomWallThickness = 60.0f;
        CustomVoxelSize = 40.0;
        CustomNoiseFrequency = 0.0025f;
        CustomDomainWarpIntensity = 0.0f;
        CustomTerraceSteps = 0.0f;
        CustomBounds = FVector(5200.0f, 1600.0f, 1400.0f);
        break;
    case 1:
        CustomTunnelRadius = 150.0f;
        CustomWallThickness = 30.0f;
        CustomVoxelSize = 20.0;
        CustomNoiseFrequency = 0.01f;
        CustomDomainWarpIntensity = 0.0f;
        CustomTerraceSteps = 0.0f;
        CustomBounds = FVector(3600.0f, 800.0f, 800.0f);
        break;
    case 2:
        CustomTunnelRadius = 1200.0f;
        CustomWallThickness = 100.0f;
        CustomVoxelSize = 60.0;
        CustomNoiseFrequency = 0.001f;
        CustomDomainWarpIntensity = 0.0f;
        CustomTerraceSteps = 0.0f;
        CustomBounds = FVector(5200.0f, 3200.0f, 2600.0f);
        break;
    case 3:
        CustomTunnelRadius = 400.0f;
        CustomWallThickness = 40.0f;
        CustomVoxelSize = 30.0;
        CustomNoiseFrequency = 0.008f;
        CustomDomainWarpIntensity = 200.0f;
        CustomTerraceSteps = 0.0f;
        CustomBounds = FVector(4400.0f, 1400.0f, 1400.0f);
        break;
    case 4:
    default:
        CustomTunnelRadius = 1000.0f;
        CustomWallThickness = 80.0f;
        CustomVoxelSize = 50.0;
        CustomNoiseFrequency = 0.004f;
        CustomDomainWarpIntensity = 0.0f;
        CustomTerraceSteps = 150.0f;
        CustomBounds = FVector(5200.0f, 2400.0f, 1800.0f);
        break;
    }

    const float HalfLength = 2000.0f;
    CustomTunnelStart = FVector(-HalfLength, 0.0f, CustomTunnelRadius);
    CustomTunnelEnd = FVector(HalfLength, 0.0f, CustomTunnelRadius);
}

bool SIOCSetupWizard::PrepareStarterAssets(FString& OutStatus)
{
    TArray<UObject*> Objects;
    TArray<FString> MissingAssets;
    IOC_LoadStarterAssets(Objects, &MissingAssets);

    if (MissingAssets.Num() > 0)
    {
        OutStatus = FString::Printf(
            TEXT("Missing starter assets in plugin content: %s. Reinstall the plugin or restore its Content folder, then validate the installation again."),
            *FString::Join(MissingAssets, TEXT(", ")));
        return false;
    }

    if (GEditor)
    {
        GEditor->SyncBrowserToObjects(Objects);
    }

    OutStatus = TEXT("Starter assets are already packaged with IOC and ready to use. The Content Browser has been focused on them.");
    return true;
}

bool SIOCSetupWizard::CreateStarterLevel(FString& OutStatus)
{
    if (!GEditor)
    {
        OutStatus = TEXT("Could not create the starter level because the editor is unavailable.");
        return false;
    }

    ULevelEditorSubsystem* LevelSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
    if (!LevelSubsystem)
    {
        OutStatus = TEXT("Could not create the starter level because the Level Editor subsystem is unavailable.");
        return false;
    }

    const FString MapPackage = IOCWizard::StarterLevelPackagePath;
    const bool bMapExists = FPackageName::DoesPackageExist(MapPackage);
    const bool bLoadedOrCreated = bMapExists
        ? LevelSubsystem->LoadLevel(MapPackage)
        : LevelSubsystem->NewLevel(MapPackage, false);

    if (!bLoadedOrCreated)
    {
        OutStatus = bMapExists
            ? TEXT("Failed to open /Game/IOC_Showcase.")
            : TEXT("Failed to create /Game/IOC_Showcase.");
        return false;
    }

    UWorld* World = IOCWizard::GetEditorWorld();
    if (!World)
    {
        OutStatus = TEXT("The starter level loaded, but no editor world is available.");
        return false;
    }

    if (!FApp::CanEverRender())
    {
        if (!LevelSubsystem->SaveCurrentLevel())
        {
            OutStatus = TEXT("Created or opened /Game/IOC_Showcase, but saving the headless starter level failed.");
            return false;
        }

        OutStatus = TEXT("Created or opened /Game/IOC_Showcase. Launcher setup is skipped in headless automation.");
        return true;
    }

    AIOCShowcaseLauncher* Launcher = nullptr;
    for (TActorIterator<AIOCShowcaseLauncher> It(World); It; ++It)
    {
        Launcher = *It;
        break;
    }

    if (!Launcher)
    {
        FActorSpawnParameters SpawnParameters;
        SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SpawnParameters.ObjectFlags |= RF_Transactional;

        Launcher = World->SpawnActor<AIOCShowcaseLauncher>(
            AIOCShowcaseLauncher::StaticClass(),
            FVector::ZeroVector,
            FRotator::ZeroRotator,
            SpawnParameters);
        if (!Launcher)
        {
            OutStatus = TEXT("The starter level opened, but the IOC showcase launcher could not be spawned.");
            return false;
        }

#if WITH_EDITOR
        Launcher->SetActorLabel(TEXT("IOC_ShowcaseLauncher"));
#endif
    }

    Launcher->Modify();
    Launcher->bAutoStart = true;
    Launcher->bCaptureMode = true;
    Launcher->bShowCaptions = true;
    Launcher->StartDelay = 0.5f;

    if (!LevelSubsystem->SaveCurrentLevel())
    {
        OutStatus = TEXT("Created or opened /Game/IOC_Showcase, but saving the level failed.");
        return false;
    }

    OutStatus = TEXT("Created or opened /Game/IOC_Showcase. Press Play to run the capture-ready starter level.");
    return true;
}

void SIOCSetupWizard::HandlePostWorldCleanup(UWorld* World, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
{
    if (PreviewActor.IsValid() && PreviewActor->GetWorld() == World)
    {
        UnbindPreviewActorDelegates();
        PreviewActor.Reset();
    }

    if (LastConfiguredCave.IsValid() && LastConfiguredCave->GetWorld() == World)
    {
        UnbindConfiguredCaveDelegates();
        LastConfiguredCave.Reset();
        GenerationStats.Empty();
    }

    if (RollbackSnapshot.TargetCave.IsValid() && RollbackSnapshot.TargetCave->GetWorld() == World)
    {
        RollbackSnapshot = FIOCSetupWizardRollbackSnapshot();
    }

    ExistingCaves.RemoveAll([World](const TWeakObjectPtr<AIOCProceduralActor>& Cave)
    {
        return !Cave.IsValid() || Cave->GetWorld() == World;
    });

    SpawnedActors.RemoveAll([World](const TWeakObjectPtr<AActor>& Actor)
    {
        return !Actor.IsValid() || Actor->GetWorld() == World;
    });

    RebuildExistingActorList();
}

FString SIOCSetupWizard::BuildInstallationValidationReport(bool& bOutAllOk) const
{
    bOutAllOk = true;
    FString Report;

    auto AddCheck = [&Report, &bOutAllOk](const FString& Label, bool bOk, const FString& Detail)
    {
        Report += FString::Printf(TEXT("%s %s - %s\n"),
            bOk ? TEXT("[OK]") : TEXT("[FAIL]"),
            *Label,
            *Detail);
        bOutAllOk &= bOk;
    };

    const TSharedPtr<IPlugin> IOCPlugin = IPluginManager::Get().FindPlugin(TEXT("InstantOrganicCaves"));
    AddCheck(TEXT("Plugin"), IOCPlugin.IsValid() && IOCPlugin->IsEnabled(),
        IOCPlugin.IsValid() ? IOCPlugin->GetBaseDir() : TEXT("InstantOrganicCaves plugin not found"));

    AddCheck(TEXT("Editor World"), IOCWizard::GetEditorWorld() != nullptr,
        IOCWizard::GetEditorWorld() ? TEXT("Level is open") : TEXT("Open or create a level"));

    AddCheck(TEXT("Plugin Content"), FPackageName::DoesPackageExist(TEXT("/InstantOrganicCaves/MI_IOC_CaveWalls")),
        TEXT("/InstantOrganicCaves content mount"));

    AddCheck(TEXT("Smart Material Instance"),
        LoadObject<UObject>(nullptr, IOCWizard::StarterMaterialInstanceObjectPath) != nullptr,
        TEXT("Starter assets ship with the plugin. Reinstall the plugin if this asset is missing."));

    AddCheck(TEXT("Starter Blueprint"),
        LoadObject<UObject>(nullptr, IOCWizard::StarterBlueprintObjectPath) != nullptr,
        TEXT("Starter assets ship with the plugin. Reinstall the plugin if this asset is missing."));

    AddCheck(TEXT("Cave Actor Class"), AIOCProceduralActor::StaticClass() != nullptr,
        TEXT("/Script/InstantOrganicCaves.IOCProceduralActor"));

    AddCheck(TEXT("Showcase Launcher Class"),
        AIOCShowcaseLauncher::StaticClass() != nullptr,
        TEXT("Required for starter/showcase level"));

    const TSharedPtr<IPlugin> PythonPlugin = IPluginManager::Get().FindPlugin(TEXT("PythonScriptPlugin"));
    AddCheck(TEXT("Python Automation (Optional)"), true,
        PythonPlugin.IsValid() && PythonPlugin->IsEnabled()
            ? TEXT("Enabled for optional Resources/*.py automation helpers.")
            : TEXT("Not required. The setup wizard uses built-in editor setup for starter assets and the showcase map."));

    const TSharedPtr<IPlugin> PCGPlugin = IPluginManager::Get().FindPlugin(TEXT("PCG"));
    AddCheck(TEXT("PCG Plugin"), PCGPlugin.IsValid() && PCGPlugin->IsEnabled(),
        TEXT("Required for IOC Voxel Core PCG workflows"));

    Report += bOutAllOk
        ? TEXT("Installation validation passed.")
        : TEXT("Installation validation found issues. Use the suggested actions above before shipping.");
    return Report;
}

int64 SIOCSetupWizard::EstimateVoxelCount() const
{
    return BuildPreflightEstimate().EstimatedVoxels;
}

FString SIOCSetupWizard::GetPreflightSummary() const
{
    const FIOCPreflightEstimate Estimate = BuildPreflightEstimate();
    return FString::Printf(
        TEXT("Estimated voxel workload: %.2fM (%lld cells)\nRisk level: %s\nComplexity score: %.2fM\nTunnel path length: %.0f cm\nExisting scatter layers: %d\nPreview mode: %s"),
        Estimate.EstimatedMillions,
        Estimate.EstimatedVoxels,
        *Estimate.RiskLabel,
        Estimate.ComplexityScore / 1000000.0,
        Estimate.PathLength,
        Estimate.ScatterLayerCount,
        bPreviewFullFidelity ? TEXT("Full geometry") : TEXT("Fast reduced geometry"));
}

FString SIOCSetupWizard::GetRecommendationText() const
{
    const FIOCPreflightEstimate Estimate = BuildPreflightEstimate();
    FString Recommendation;

    if (Estimate.RiskLabel == TEXT("Very high"))
    {
        Recommendation = TEXT("Recommendation: increase voxel size, reduce bounds, or enable fixed tunnel bounds before generating.");
    }
    else if (Estimate.RiskLabel == TEXT("High"))
    {
        Recommendation = TEXT("Recommendation: acceptable for a deliberate test pass, but save the level first and expect a slower async build.");
    }
    else if (Estimate.RiskLabel == TEXT("Moderate"))
    {
        Recommendation = TEXT("Recommendation: workable for an interactive first pass, but keep an eye on smoothing, LOD multiplier, and scatter complexity.");
    }
    else
    {
        Recommendation = TEXT("Recommendation: this setup is in a comfortable range for an interactive first pass.");
    }

    if (bPreviewFullFidelity && Estimate.ComplexityScore > 2000000.0)
    {
        Recommendation += TEXT(" Full geometry preview may also take noticeable time.");
    }

    if (Estimate.ScatterLayerCount > 0)
    {
        Recommendation += TEXT(" Existing scatter layers are included in the risk estimate.");
    }

    if (!bStarterAssetsPrepared)
    {
        Recommendation += TEXT(" Use Prepare Starter Assets to verify the shipped starter assets if this is the first IOC setup in the project.");
    }

    return Recommendation;
}

FString SIOCSetupWizard::ExportProfileText() const
{
    FString Text;
    Text += FString::Printf(TEXT("IOC Setup Profile: %s\n"), *ProfileName);
    Text += FString::Printf(TEXT("Style: %s\n"), *GetSelectedPresetName());
    Text += FString::Printf(TEXT("Custom: %s\n"), bIsCustom ? TEXT("true") : TEXT("false"));
    Text += FString::Printf(TEXT("Tunnel: %s | Spline: %s\n"), bCustomTunnelMode ? TEXT("true") : TEXT("false"), bCustomUseSpline ? TEXT("true") : TEXT("false"));
    Text += FString::Printf(TEXT("Seed: %d\n"), CustomSeed);
    Text += FString::Printf(TEXT("Bounds: %s\n"), *CustomBounds.ToString());
    Text += FString::Printf(TEXT("TunnelStart: %s\n"), *CustomTunnelStart.ToString());
    Text += FString::Printf(TEXT("TunnelEnd: %s\n"), *CustomTunnelEnd.ToString());
    Text += FString::Printf(TEXT("VoxelSize: %.2f | Radius: %.2f | WallThickness: %.2f\n"), CustomVoxelSize, CustomTunnelRadius, CustomWallThickness);
    Text += FString::Printf(TEXT("NoiseFrequency: %.6f | SmoothIterations: %d\n"), CustomNoiseFrequency, CustomSmoothIterations);
    Text += FString::Printf(TEXT("DomainWarp: %.2f | TerraceSteps: %.2f | TextureTiling: %.6f\n"), CustomDomainWarpIntensity, CustomTerraceSteps, CustomTextureTiling);
    Text += FString::Printf(TEXT("SmartColors: %s | LOD: %s | WorldSpaceNoise: %s\n"),
        WizardSettings.bGenerateSmartColors ? TEXT("true") : TEXT("false"),
        WizardSettings.bEnableLOD ? TEXT("true") : TEXT("false"),
        WizardSettings.bUseWorldSpaceNoise ? TEXT("true") : TEXT("false"));
    Text += GetPreflightSummary();
    return Text;
}

void SIOCSetupWizard::ApplyWizardSettingsToCave(AIOCProceduralActor* Cave, bool bPreview) const
{
    if (!Cave)
    {
        return;
    }

    const EIOCCavePreset VisualPreset = IOC_GetStylePresetForSelection(SelectedPresetIndex);

    if (!bIsCustom && SelectedPresetIndex < IOCWizard::CustomIndex)
    {
        Cave->CavePreset = IOCWizard::Presets[SelectedPresetIndex].Preset;
        Cave->ApplyPresetSettingsOnly();
        Cave->CavePreset = EIOCCavePreset::Custom;
    }
    else
    {
        Cave->CavePreset = EIOCCavePreset::Custom;
        Cave->VoxelSize = CustomVoxelSize;
        Cave->CaveSeed = CustomSeed;
        Cave->GenerationBounds = CustomBounds;
        Cave->bGenerateTunnel = bCustomTunnelMode;
        Cave->bUseSpline = bCustomTunnelMode && bCustomUseSpline;
        Cave->NoiseFrequency = CustomNoiseFrequency;
        Cave->SmoothIterations = CustomSmoothIterations;
        Cave->DomainWarpIntensity = CustomDomainWarpIntensity;
        Cave->TerraceSteps = CustomTerraceSteps;

        if (bCustomTunnelMode)
        {
            Cave->TunnelRadius = CustomTunnelRadius;
            Cave->WallThickness = CustomWallThickness;
            Cave->TunnelStart = CustomTunnelStart;
            Cave->TunnelEnd = CustomTunnelEnd;
        }
    }

    Cave->bGenerateSmartColors = WizardSettings.bGenerateSmartColors;
    Cave->TextureTiling = CustomTextureTiling;
    Cave->bEnableLOD = WizardSettings.bEnableLOD;
    Cave->LODDistance = CustomLODDistance;
    Cave->LODVoxelSizeMultiplier = CustomLODMultiplier;
    Cave->bUseWorldSpaceNoise = WizardSettings.bUseWorldSpaceNoise;
    Cave->bUseFixedBoundsForTunnel = WizardSettings.bUseFixedBoundsForTunnel;
    Cave->bAutoRebuildNavMesh = WizardSettings.bAutoRebuildNavMesh;
    Cave->bShowDebugViz = WizardSettings.bShowDebugViz;

    if (VisualPreset != EIOCCavePreset::Custom)
    {
        Cave->DecorationLayers = IOC_BuildPresetDecorationLayers(VisualPreset);
    }
    else if (!bReconfigureExisting || bPreview || Cave->DecorationLayers.Num() == 0)
    {
        Cave->DecorationLayers.Reset();
    }

    if (bPreview && !bPreviewFullFidelity)
    {
        Cave->bEnableLOD = false;
        Cave->bAutoRebuildNavMesh = false;
        Cave->bShowDebugViz = false;
        Cave->VoxelSize = FMath::Max(Cave->VoxelSize, 90.0);

        if (VisualPreset != EIOCCavePreset::Custom)
        {
            Cave->DecorationLayers = IOC_BuildPreviewDecorationLayers(VisualPreset);
        }
        else
        {
            Cave->DecorationLayers.Reset();
        }

        if (Cave->bGenerateTunnel)
        {
            const FVector RawDirection = (Cave->TunnelEnd - Cave->TunnelStart);
            const FVector Direction = RawDirection.IsNearlyZero()
                ? FVector::ForwardVector
                : RawDirection.GetSafeNormal();
            const float HalfLength = FMath::Clamp(RawDirection.Size() * 0.5f, 700.0f, 1700.0f);
            const FVector Center(0.0f, 0.0f, FMath::Max(Cave->TunnelRadius, 150.0f));
            Cave->TunnelStart = Center - Direction * HalfLength;
            Cave->TunnelEnd = Center + Direction * HalfLength;
        }
        else
        {
            Cave->GenerationBounds = Cave->GenerationBounds.ComponentMin(FVector(1800.0f, 1800.0f, 1200.0f));
        }
    }
    else if (bPreview)
    {
        Cave->bAutoRebuildNavMesh = false;
        Cave->bShowDebugViz = false;
    }

    if (Cave->CaveSpline && Cave->bGenerateTunnel)
    {
        TArray<FVector> Points;
        Points.Add(Cave->TunnelStart);
        Points.Add(Cave->TunnelEnd);
        Cave->CaveSpline->SetSplinePoints(Points, ESplineCoordinateSpace::Local, false);
        Cave->CaveSpline->SetSplinePointType(0, ESplinePointType::Linear, false);
        Cave->CaveSpline->SetSplinePointType(1, ESplinePointType::Linear, true);
    }

    UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/InstantOrganicCaves/MI_IOC_SmartCave_Inst.MI_IOC_SmartCave_Inst"));
    if (!Mat)
    {
        Mat = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/InstantOrganicCaves/MI_IOC_CaveWalls.MI_IOC_CaveWalls"));
    }
    if (Mat)
    {
        Cave->CaveMaterial = Mat;
    }
}

// ============================================================================
// Feature 1: Preview viewport management
// ============================================================================
void SIOCSetupWizard::BindPreviewActorDelegates(AIOCProceduralActor* Cave)
{
    UnbindPreviewActorDelegates();
    if (!Cave)
    {
        return;
    }

    PreviewGenerationStartedHandle = Cave->OnGenerationStarted.AddRaw(this, &SIOCSetupWizard::HandlePreviewGenerationStarted);
    PreviewGenerationFinishedHandle = Cave->OnGenerationFinished.AddRaw(this, &SIOCSetupWizard::HandlePreviewGenerationFinished);
}

void SIOCSetupWizard::UnbindPreviewActorDelegates()
{
    if (PreviewActor.IsValid())
    {
        if (PreviewGenerationStartedHandle.IsValid())
        {
            PreviewActor->OnGenerationStarted.Remove(PreviewGenerationStartedHandle);
            PreviewGenerationStartedHandle.Reset();
        }
        if (PreviewGenerationFinishedHandle.IsValid())
        {
            PreviewActor->OnGenerationFinished.Remove(PreviewGenerationFinishedHandle);
            PreviewGenerationFinishedHandle.Reset();
        }
    }
}

void SIOCSetupWizard::BindConfiguredCaveDelegates(AIOCProceduralActor* Cave)
{
    UnbindConfiguredCaveDelegates();
    if (!Cave)
    {
        return;
    }

    ConfiguredGenerationStartedHandle = Cave->OnGenerationStarted.AddRaw(this, &SIOCSetupWizard::HandleConfiguredGenerationStarted);
    ConfiguredGenerationFinishedHandle = Cave->OnGenerationFinished.AddRaw(this, &SIOCSetupWizard::HandleConfiguredGenerationFinished);
}

void SIOCSetupWizard::UnbindConfiguredCaveDelegates()
{
    if (LastConfiguredCave.IsValid())
    {
        if (ConfiguredGenerationStartedHandle.IsValid())
        {
            LastConfiguredCave->OnGenerationStarted.Remove(ConfiguredGenerationStartedHandle);
            ConfiguredGenerationStartedHandle.Reset();
        }
        if (ConfiguredGenerationFinishedHandle.IsValid())
        {
            LastConfiguredCave->OnGenerationFinished.Remove(ConfiguredGenerationFinishedHandle);
            ConfiguredGenerationFinishedHandle.Reset();
        }
    }
}

void SIOCSetupWizard::HandlePreviewGenerationStarted(AIOCProceduralActor* Cave)
{
    PreviewStatus = TEXT("Preview is rebuilding...");
}

void SIOCSetupWizard::HandlePreviewGenerationFinished(AIOCProceduralActor* Cave, bool bCancelled, bool bWillRegenerate)
{
    if (bWillRegenerate)
    {
        PreviewStatus = TEXT("Preview restarted after a new change...");
        return;
    }

    if (bCancelled)
    {
        PreviewStatus = TEXT("Preview refresh was cancelled.");
        return;
    }

    LastPreviewBuiltAt = FDateTime::Now();
    PreviewStatus = TEXT("Preview is up to date.");
}

void SIOCSetupWizard::HandleConfiguredGenerationStarted(AIOCProceduralActor* Cave)
{
    bIsGenerating = true;
    bGenerationDone = false;
    GenerationProgress = FMath::Max(GenerationProgress, 0.92f);
    SetupError.Empty();
    BuildGenerationSummary();
}

void SIOCSetupWizard::HandleConfiguredGenerationFinished(AIOCProceduralActor* Cave, bool bCancelled, bool bWillRegenerate)
{
    if (!LastConfiguredCave.IsValid() || LastConfiguredCave.Get() != Cave)
    {
        return;
    }

    if (bWillRegenerate)
    {
        bIsGenerating = true;
        GenerationProgress = 0.94f;
        GenerationStats = TEXT("Generation restarted after a parameter change. Waiting for final metrics...");
        return;
    }

    if (bCancelled)
    {
        bIsGenerating = false;
        bGenerationDone = false;
        GenerationProgress = 0.0f;
        GenerationStats = TEXT("Generation was cancelled before completion.");
        return;
    }

    CollectGenerationStats(Cave);
    bIsGenerating = false;
    bGenerationDone = true;
    GenerationProgress = 1.0f;
    BuildGenerationSummary();
    RequestExistingCaveRefresh(0.0);

    if (bAutoFocusConfiguredCave)
    {
        FocusConfiguredCave();
    }
}

void SIOCSetupWizard::UpdatePreviewActor()
{
    DestroyPreviewActor();

    UWorld* World = PreviewViewport.IsValid() ? PreviewViewport->GetPreviewWorld() : nullptr;
    if (!World) return;

    FActorSpawnParameters SP;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SP.bDeferConstruction = true;
    SP.ObjectFlags |= RF_Transient;

    AIOCProceduralActor* Preview = World->SpawnActor<AIOCProceduralActor>(
        AIOCProceduralActor::StaticClass(), FTransform::Identity, SP);
    if (!Preview) return;

    ApplyWizardSettingsToCave(Preview, true);
    Preview->SetActorTickEnabled(false);
    Preview->FinishSpawning(FTransform::Identity);
    PreviewActor = Preview;
    BindPreviewActorDelegates(Preview);
    PreviewStatus = TEXT("Preview is rebuilding...");
    Preview->GenerateCave();
}

void SIOCSetupWizard::DestroyPreviewActor()
{
    UnbindPreviewActorDelegates();
    if (PreviewActor.IsValid())
    {
        PreviewActor->Destroy();
        PreviewActor.Reset();
    }
}

// ============================================================================
// Feature 2: Collect generation stats
// ============================================================================
void SIOCSetupWizard::CollectGenerationStats(AIOCProceduralActor* Cave)
{
    if (!Cave) return;

    if (Cave->bIsGeneratingDisplay &&
        Cave->LastEstimatedVoxelCount == 0 &&
        Cave->LastPrimaryTriangleCount == 0 &&
        Cave->LastLODTriangleCount == 0)
    {
        GenerationStats = TEXT("Generation is running asynchronously. Use Refresh Stats after it completes, or watch the Details panel on the selected cave actor.");
        return;
    }

    GenerationStats = FString::Printf(
        TEXT("Voxels: %lld | Triangles: %d | LOD Tris: %d | Scatter: %d instances\n"),
        Cave->LastEstimatedVoxelCount,
        Cave->LastPrimaryTriangleCount,
        Cave->LastLODTriangleCount,
        Cave->LastScatterInstanceCount);

    if (Cave->LastGenerationTimeSeconds > 0.0)
    {
        GenerationStats += FString::Printf(TEXT("Generation time: %.2f seconds"), Cave->LastGenerationTimeSeconds);
    }
    else
    {
        GenerationStats += TEXT("Generation time: (async - check Details panel)");
    }
}

void SIOCSetupWizard::RebuildExistingActorList()
{
    if (!ExistingActorListContainer.IsValid())
    {
        return;
    }

    ExistingActorListContainer->ClearChildren();
    if (ExistingCaves.Num() == 0)
    {
        ExistingActorListContainer->AddSlot().AutoHeight()
        [
            SNew(STextBlock)
            .Text(INVTEXT("No cave actors are currently available in this level."))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
            .ColorAndOpacity(IOCWizard::TextDim)
        ];
        return;
    }

    for (int32 ActorIndex = 0; ActorIndex < ExistingCaves.Num(); ++ActorIndex)
    {
        ExistingActorListContainer->AddSlot().AutoHeight().Padding(FMargin(0, 2))
        [
            SNew(SButton)
            .ButtonStyle(&FIOCWizardStyle::Get().GetWidgetStyle<FButtonStyle>(FIOCWizardStyle::ButtonSecondary))
            .OnClicked_Lambda([this, ActorIndex]() -> FReply {
                SelectedExistingIndex = ActorIndex;
                RequestPreviewRefresh(0.0);
                RequestValidationRefresh(0.0);
                return FReply::Handled();
            })
            .Content()
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .BorderBackgroundColor_Lambda([this, ActorIndex]() -> FLinearColor {
                    return SelectedExistingIndex == ActorIndex
                        ? FLinearColor(0.055f, 0.07f, 0.09f)
                        : FLinearColor(0.03f, 0.035f, 0.045f);
                })
                .Padding(FMargin(8, 5))
                [
                    SNew(STextBlock)
                    .Text_Lambda([this, ActorIndex]() -> FText {
                        if (!ExistingCaves.IsValidIndex(ActorIndex)) return INVTEXT("(invalid)");
                        AIOCProceduralActor* Existing = ExistingCaves[ActorIndex].Get();
                        if (!Existing) return INVTEXT("(invalid)");
                        return FText::FromString(FString::Printf(TEXT("%s | %s"),
                            *Existing->GetName(), *Existing->GetPerformanceSummary()));
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
                    .ColorAndOpacity_Lambda([this, ActorIndex]() -> FSlateColor {
                        return SelectedExistingIndex == ActorIndex
                            ? IOCWizard::TextPrimary
                            : IOCWizard::TextSecondary;
                    })
                    .WrapTextAt(860)
                ]
            ]
        ];
    }
}

// ============================================================================
// Feature 7: Detect existing IOC actors
// ============================================================================
void SIOCSetupWizard::DetectExistingCaves()
{
    TWeakObjectPtr<AIOCProceduralActor> PreviouslySelected;
    if (ExistingCaves.IsValidIndex(SelectedExistingIndex))
    {
        PreviouslySelected = ExistingCaves[SelectedExistingIndex];
    }

    ExistingCaves.Reset();
    UWorld* World = IOCWizard::GetEditorWorld();
    if (!World)
    {
        RebuildExistingActorList();
        return;
    }

    for (TActorIterator<AIOCProceduralActor> It(World); It; ++It)
    {
        ExistingCaves.Add(TWeakObjectPtr<AIOCProceduralActor>(*It));
    }

    SelectedExistingIndex = INDEX_NONE;
    if (PreviouslySelected.IsValid())
    {
        for (int32 Index = 0; Index < ExistingCaves.Num(); ++Index)
        {
            if (ExistingCaves[Index] == PreviouslySelected)
            {
                SelectedExistingIndex = Index;
                break;
            }
        }
    }

    if (SelectedExistingIndex == INDEX_NONE && ExistingCaves.Num() > 0)
    {
        SelectedExistingIndex = 0;
    }

    RebuildExistingActorList();
}

// ============================================================================
// Feature 8: Apply environment defaults based on selected preset
// ============================================================================
void SIOCSetupWizard::ApplyEnvironmentDefaults()
{
    if (bIsCustom || SelectedPresetIndex >= IOCWizard::CustomIndex) return;

    int32 Idx = FMath::Clamp(SelectedPresetIndex, 0, 4);
    const auto& P = IOCWizard::Presets[Idx];

    if (!P.bRecommendsSkyLight && P.bRecommendsPointLights)
    {
        bAddLighting = true;
    }
}

// ============================================================================
// Feature 10: Undo support - track and cleanup spawned actors
// ============================================================================
void SIOCSetupWizard::RegisterCleanupHandler()
{
    if (!WorldCleanupHandle.IsValid())
    {
        WorldCleanupHandle = FWorldDelegates::OnPostWorldCleanup.AddRaw(this, &SIOCSetupWizard::HandlePostWorldCleanup);
    }
}

void SIOCSetupWizard::CleanupSpawnedActors()
{
    UWorld* World = IOCWizard::GetEditorWorld();
    if (!World) return;

    {
        FScopedTransaction Transaction(
            LOCTEXT("UndoIOCSetup", "Undo IOC Setup Wizard"));
        for (auto& WeakActor : SpawnedActors)
        {
            if (WeakActor.IsValid())
            {
                WeakActor->Destroy();
            }
        }
    }

    SpawnedActors.Reset();
    if (!HasRollbackSnapshot())
    {
        UnbindConfiguredCaveDelegates();
    }
}

void SIOCSetupWizard::CaptureRollbackSnapshot(AIOCProceduralActor* Cave)
{
    RollbackSnapshot = FIOCSetupWizardRollbackSnapshot();
    if (!Cave)
    {
        return;
    }

    RollbackSnapshot.bValid = true;
    RollbackSnapshot.TargetCave = Cave;
    RollbackSnapshot.CavePreset = Cave->CavePreset;
    RollbackSnapshot.bGenerateTunnel = Cave->bGenerateTunnel;
    RollbackSnapshot.bUseSpline = Cave->bUseSpline;
    RollbackSnapshot.TunnelStart = Cave->TunnelStart;
    RollbackSnapshot.TunnelEnd = Cave->TunnelEnd;
    RollbackSnapshot.TunnelRadius = Cave->TunnelRadius;
    RollbackSnapshot.WallThickness = Cave->WallThickness;
    RollbackSnapshot.CaveSeed = Cave->CaveSeed;
    RollbackSnapshot.GenerationBounds = Cave->GenerationBounds;
    RollbackSnapshot.VoxelSize = Cave->VoxelSize;
    RollbackSnapshot.NoiseFrequency = Cave->NoiseFrequency;
    RollbackSnapshot.NoiseThreshold = Cave->NoiseThreshold;
    RollbackSnapshot.SmoothIterations = Cave->SmoothIterations;
    RollbackSnapshot.NoiseOctaves = Cave->NoiseOctaves;
    RollbackSnapshot.NoiseLacunarity = Cave->NoiseLacunarity;
    RollbackSnapshot.NoisePersistence = Cave->NoisePersistence;
    RollbackSnapshot.MacroChamberWeight = Cave->MacroChamberWeight;
    RollbackSnapshot.RidgedDetailWeight = Cave->RidgedDetailWeight;
    RollbackSnapshot.InteriorDensityBias = Cave->InteriorDensityBias;
    RollbackSnapshot.DomainWarpIntensity = Cave->DomainWarpIntensity;
    RollbackSnapshot.TerraceSteps = Cave->TerraceSteps;
    RollbackSnapshot.CaveMaterial = Cave->CaveMaterial;
    RollbackSnapshot.TextureTiling = Cave->TextureTiling;
    RollbackSnapshot.bGenerateSmartColors = Cave->bGenerateSmartColors;
    RollbackSnapshot.DecorationLayers = Cave->DecorationLayers;
    RollbackSnapshot.bEnableLOD = Cave->bEnableLOD;
    RollbackSnapshot.LODDistance = Cave->LODDistance;
    RollbackSnapshot.LODVoxelSizeMultiplier = Cave->LODVoxelSizeMultiplier;
    RollbackSnapshot.bUseWorldSpaceNoise = Cave->bUseWorldSpaceNoise;
    RollbackSnapshot.bUseFixedBoundsForTunnel = Cave->bUseFixedBoundsForTunnel;
    RollbackSnapshot.bAutoRebuildNavMesh = Cave->bAutoRebuildNavMesh;
    RollbackSnapshot.bForcePreset = Cave->bForcePreset;
    RollbackSnapshot.bShowDebugViz = Cave->bShowDebugViz;
    RollbackSnapshot.bLogPresetDebug = Cave->bLogPresetDebug;

    if (Cave->CaveSpline)
    {
        const int32 PointCount = Cave->CaveSpline->GetNumberOfSplinePoints();
        RollbackSnapshot.SplinePoints.Reserve(PointCount);
        for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
        {
            RollbackSnapshot.SplinePoints.Add(
                Cave->CaveSpline->GetLocationAtSplinePoint(PointIndex, ESplineCoordinateSpace::Local));
        }
    }
}

bool SIOCSetupWizard::HasRollbackSnapshot() const
{
    return RollbackSnapshot.bValid && RollbackSnapshot.TargetCave.IsValid();
}

bool SIOCSetupWizard::RestoreRollbackSnapshot()
{
    if (!HasRollbackSnapshot())
    {
        return false;
    }

    AIOCProceduralActor* Cave = RollbackSnapshot.TargetCave.Get();
    FScopedTransaction Transaction(LOCTEXT("RestoreIOCSetupSnapshot", "Restore IOC Cave Settings"));
    Cave->Modify();

    Cave->CavePreset = RollbackSnapshot.CavePreset;
    Cave->bGenerateTunnel = RollbackSnapshot.bGenerateTunnel;
    Cave->bUseSpline = RollbackSnapshot.bUseSpline;
    Cave->TunnelStart = RollbackSnapshot.TunnelStart;
    Cave->TunnelEnd = RollbackSnapshot.TunnelEnd;
    Cave->TunnelRadius = RollbackSnapshot.TunnelRadius;
    Cave->WallThickness = RollbackSnapshot.WallThickness;
    Cave->CaveSeed = RollbackSnapshot.CaveSeed;
    Cave->GenerationBounds = RollbackSnapshot.GenerationBounds;
    Cave->VoxelSize = RollbackSnapshot.VoxelSize;
    Cave->NoiseFrequency = RollbackSnapshot.NoiseFrequency;
    Cave->NoiseThreshold = RollbackSnapshot.NoiseThreshold;
    Cave->SmoothIterations = RollbackSnapshot.SmoothIterations;
    Cave->NoiseOctaves = RollbackSnapshot.NoiseOctaves;
    Cave->NoiseLacunarity = RollbackSnapshot.NoiseLacunarity;
    Cave->NoisePersistence = RollbackSnapshot.NoisePersistence;
    Cave->MacroChamberWeight = RollbackSnapshot.MacroChamberWeight;
    Cave->RidgedDetailWeight = RollbackSnapshot.RidgedDetailWeight;
    Cave->InteriorDensityBias = RollbackSnapshot.InteriorDensityBias;
    Cave->DomainWarpIntensity = RollbackSnapshot.DomainWarpIntensity;
    Cave->TerraceSteps = RollbackSnapshot.TerraceSteps;
    Cave->CaveMaterial = RollbackSnapshot.CaveMaterial;
    Cave->TextureTiling = RollbackSnapshot.TextureTiling;
    Cave->bGenerateSmartColors = RollbackSnapshot.bGenerateSmartColors;
    Cave->DecorationLayers = RollbackSnapshot.DecorationLayers;
    Cave->bEnableLOD = RollbackSnapshot.bEnableLOD;
    Cave->LODDistance = RollbackSnapshot.LODDistance;
    Cave->LODVoxelSizeMultiplier = RollbackSnapshot.LODVoxelSizeMultiplier;
    Cave->bUseWorldSpaceNoise = RollbackSnapshot.bUseWorldSpaceNoise;
    Cave->bUseFixedBoundsForTunnel = RollbackSnapshot.bUseFixedBoundsForTunnel;
    Cave->bAutoRebuildNavMesh = RollbackSnapshot.bAutoRebuildNavMesh;
    Cave->bForcePreset = RollbackSnapshot.bForcePreset;
    Cave->bShowDebugViz = RollbackSnapshot.bShowDebugViz;
    Cave->bLogPresetDebug = RollbackSnapshot.bLogPresetDebug;

    Cave->RerunConstructionScripts();
    if (Cave->CaveSpline && RollbackSnapshot.SplinePoints.Num() > 0)
    {
        Cave->CaveSpline->SetSplinePoints(RollbackSnapshot.SplinePoints, ESplineCoordinateSpace::Local, true);
    }

    LastConfiguredCave = Cave;
    BindConfiguredCaveDelegates(Cave);
    bGenerationDone = false;
    bIsGenerating = true;
    GenerationProgress = 0.92f;
    Cave->GenerateCave();
    GenerationSummary = TEXT("Restored the previous cave settings. The cave is regenerating with the saved snapshot.");
    GenerationStats = TEXT("Restored previous settings. Waiting for regenerated metrics...");
    RollbackSnapshot = FIOCSetupWizardRollbackSnapshot();
    return true;
}

// ============================================================================
// Feature 11: Persist settings via GConfig
// ============================================================================
void SIOCSetupWizard::SaveSettings()
{
    SaveStateToSection(FString(IOCWizard::ConfigSection));
    GConfig->Flush(false, IOCWizard::ConfigFile);
}

void SIOCSetupWizard::LoadSettings()
{
    LoadStateFromSection(FString(IOCWizard::ConfigSection));
}

void SIOCSetupWizard::SaveStateToSection(const FString& SectionName)
{
    GConfig->SetInt(*SectionName, TEXT("SelectedPresetIndex"), SelectedPresetIndex, IOCWizard::ConfigFile);
    GConfig->SetBool(*SectionName, TEXT("bIsCustom"), bIsCustom, IOCWizard::ConfigFile);
    GConfig->SetBool(*SectionName, TEXT("bCustomTunnelMode"), bCustomTunnelMode, IOCWizard::ConfigFile);
    GConfig->SetBool(*SectionName, TEXT("bCustomUseSpline"), bCustomUseSpline, IOCWizard::ConfigFile);
    GConfig->SetInt(*SectionName, TEXT("CustomSeed"), CustomSeed, IOCWizard::ConfigFile);

    FString BoundsStr = FString::Printf(TEXT("%g,%g,%g"), CustomBounds.X, CustomBounds.Y, CustomBounds.Z);
    GConfig->SetString(*SectionName, TEXT("CustomBounds"), *BoundsStr, IOCWizard::ConfigFile);

    FString TunnelStartStr = FString::Printf(TEXT("%g,%g,%g"), CustomTunnelStart.X, CustomTunnelStart.Y, CustomTunnelStart.Z);
    GConfig->SetString(*SectionName, TEXT("CustomTunnelStart"), *TunnelStartStr, IOCWizard::ConfigFile);
    FString TunnelEndStr = FString::Printf(TEXT("%g,%g,%g"), CustomTunnelEnd.X, CustomTunnelEnd.Y, CustomTunnelEnd.Z);
    GConfig->SetString(*SectionName, TEXT("CustomTunnelEnd"), *TunnelEndStr, IOCWizard::ConfigFile);

    GConfig->SetFloat(*SectionName, TEXT("CustomVoxelSize"), (float)CustomVoxelSize, IOCWizard::ConfigFile);
    GConfig->SetFloat(*SectionName, TEXT("CustomTunnelRadius"), CustomTunnelRadius, IOCWizard::ConfigFile);
    GConfig->SetFloat(*SectionName, TEXT("CustomWallThickness"), CustomWallThickness, IOCWizard::ConfigFile);
    GConfig->SetFloat(*SectionName, TEXT("CustomNoiseFrequency"), CustomNoiseFrequency, IOCWizard::ConfigFile);
    GConfig->SetInt(*SectionName, TEXT("CustomSmoothIterations"), CustomSmoothIterations, IOCWizard::ConfigFile);
    GConfig->SetFloat(*SectionName, TEXT("CustomDomainWarpIntensity"), CustomDomainWarpIntensity, IOCWizard::ConfigFile);
    GConfig->SetFloat(*SectionName, TEXT("CustomTerraceSteps"), CustomTerraceSteps, IOCWizard::ConfigFile);
    GConfig->SetFloat(*SectionName, TEXT("CustomTextureTiling"), CustomTextureTiling, IOCWizard::ConfigFile);
    GConfig->SetBool(*SectionName, TEXT("bGenerateSmartColors"), WizardSettings.bGenerateSmartColors, IOCWizard::ConfigFile);
    GConfig->SetBool(*SectionName, TEXT("bEnableLOD"), WizardSettings.bEnableLOD, IOCWizard::ConfigFile);
    GConfig->SetFloat(*SectionName, TEXT("CustomLODDistance"), CustomLODDistance, IOCWizard::ConfigFile);
    GConfig->SetFloat(*SectionName, TEXT("CustomLODMultiplier"), CustomLODMultiplier, IOCWizard::ConfigFile);
    GConfig->SetBool(*SectionName, TEXT("bUseWorldSpaceNoise"), WizardSettings.bUseWorldSpaceNoise, IOCWizard::ConfigFile);
    GConfig->SetBool(*SectionName, TEXT("bUseFixedBoundsForTunnel"), WizardSettings.bUseFixedBoundsForTunnel, IOCWizard::ConfigFile);
    GConfig->SetBool(*SectionName, TEXT("bAutoRebuildNavMesh"), WizardSettings.bAutoRebuildNavMesh, IOCWizard::ConfigFile);
    GConfig->SetBool(*SectionName, TEXT("bShowDebugViz"), WizardSettings.bShowDebugViz, IOCWizard::ConfigFile);
    GConfig->SetBool(*SectionName, TEXT("bPreviewFullFidelity"), bPreviewFullFidelity, IOCWizard::ConfigFile);
    GConfig->SetBool(*SectionName, TEXT("bAutoFocusConfiguredCave"), bAutoFocusConfiguredCave, IOCWizard::ConfigFile);
    GConfig->SetBool(*SectionName, TEXT("bAddLighting"), bAddLighting, IOCWizard::ConfigFile);
    GConfig->SetBool(*SectionName, TEXT("bAddPlayer"), bAddPlayer, IOCWizard::ConfigFile);
    GConfig->SetBool(*SectionName, TEXT("bReconfigureExisting"), bReconfigureExisting, IOCWizard::ConfigFile);
    GConfig->SetInt(*SectionName, TEXT("SelectedExistingIndex"), SelectedExistingIndex, IOCWizard::ConfigFile);
    GConfig->SetString(*SectionName, TEXT("ProfileName"), *ProfileName, IOCWizard::ConfigFile);
}

void SIOCSetupWizard::LoadStateFromSection(const FString& SectionName)
{
    int32 IntVal;
    float FloatVal;
    bool BoolVal;
    FString StringVal;

    auto LoadVector = [&](const TCHAR* Key, FVector& OutValue)
    {
        FString VectorStr;
        if (GConfig->GetString(*SectionName, Key, VectorStr, IOCWizard::ConfigFile))
        {
            TArray<FString> Parts;
            VectorStr.ParseIntoArray(Parts, TEXT(","), true);
            if (Parts.Num() == 3)
            {
                OutValue = FVector(TCString<TCHAR>::Atof(*Parts[0]),
                    TCString<TCHAR>::Atof(*Parts[1]), TCString<TCHAR>::Atof(*Parts[2]));
            }
        }
    };

    if (GConfig->GetInt(*SectionName, TEXT("SelectedPresetIndex"), IntVal, IOCWizard::ConfigFile))
        SelectedPresetIndex = IntVal;
    if (GConfig->GetBool(*SectionName, TEXT("bIsCustom"), BoolVal, IOCWizard::ConfigFile))
        bIsCustom = BoolVal;
    if (GConfig->GetBool(*SectionName, TEXT("bCustomTunnelMode"), BoolVal, IOCWizard::ConfigFile))
        bCustomTunnelMode = BoolVal;
    if (GConfig->GetBool(*SectionName, TEXT("bCustomUseSpline"), BoolVal, IOCWizard::ConfigFile))
        bCustomUseSpline = BoolVal;
    if (GConfig->GetInt(*SectionName, TEXT("CustomSeed"), IntVal, IOCWizard::ConfigFile))
        CustomSeed = IntVal;
    LoadVector(TEXT("CustomBounds"), CustomBounds);
    LoadVector(TEXT("CustomTunnelStart"), CustomTunnelStart);
    LoadVector(TEXT("CustomTunnelEnd"), CustomTunnelEnd);
    if (GConfig->GetFloat(*SectionName, TEXT("CustomVoxelSize"), FloatVal, IOCWizard::ConfigFile))
        CustomVoxelSize = (double)FloatVal;
    if (GConfig->GetFloat(*SectionName, TEXT("CustomTunnelRadius"), FloatVal, IOCWizard::ConfigFile))
        CustomTunnelRadius = FloatVal;
    if (GConfig->GetFloat(*SectionName, TEXT("CustomWallThickness"), FloatVal, IOCWizard::ConfigFile))
        CustomWallThickness = FloatVal;
    if (GConfig->GetFloat(*SectionName, TEXT("CustomNoiseFrequency"), FloatVal, IOCWizard::ConfigFile))
        CustomNoiseFrequency = FloatVal;
    if (GConfig->GetInt(*SectionName, TEXT("CustomSmoothIterations"), IntVal, IOCWizard::ConfigFile))
        CustomSmoothIterations = IntVal;
    if (GConfig->GetFloat(*SectionName, TEXT("CustomDomainWarpIntensity"), FloatVal, IOCWizard::ConfigFile))
        CustomDomainWarpIntensity = FloatVal;
    if (GConfig->GetFloat(*SectionName, TEXT("CustomTerraceSteps"), FloatVal, IOCWizard::ConfigFile))
        CustomTerraceSteps = FloatVal;
    if (GConfig->GetFloat(*SectionName, TEXT("CustomTextureTiling"), FloatVal, IOCWizard::ConfigFile))
        CustomTextureTiling = FloatVal;
    if (GConfig->GetBool(*SectionName, TEXT("bGenerateSmartColors"), BoolVal, IOCWizard::ConfigFile))
        WizardSettings.bGenerateSmartColors = BoolVal;
    if (GConfig->GetBool(*SectionName, TEXT("bEnableLOD"), BoolVal, IOCWizard::ConfigFile))
        WizardSettings.bEnableLOD = BoolVal;
    if (GConfig->GetFloat(*SectionName, TEXT("CustomLODDistance"), FloatVal, IOCWizard::ConfigFile))
        CustomLODDistance = FloatVal;
    if (GConfig->GetFloat(*SectionName, TEXT("CustomLODMultiplier"), FloatVal, IOCWizard::ConfigFile))
        CustomLODMultiplier = FloatVal;
    if (GConfig->GetBool(*SectionName, TEXT("bUseWorldSpaceNoise"), BoolVal, IOCWizard::ConfigFile))
        WizardSettings.bUseWorldSpaceNoise = BoolVal;
    if (GConfig->GetBool(*SectionName, TEXT("bUseFixedBoundsForTunnel"), BoolVal, IOCWizard::ConfigFile))
        WizardSettings.bUseFixedBoundsForTunnel = BoolVal;
    if (GConfig->GetBool(*SectionName, TEXT("bAutoRebuildNavMesh"), BoolVal, IOCWizard::ConfigFile))
        WizardSettings.bAutoRebuildNavMesh = BoolVal;
    if (GConfig->GetBool(*SectionName, TEXT("bShowDebugViz"), BoolVal, IOCWizard::ConfigFile))
        WizardSettings.bShowDebugViz = BoolVal;
    if (GConfig->GetBool(*SectionName, TEXT("bPreviewFullFidelity"), BoolVal, IOCWizard::ConfigFile))
        bPreviewFullFidelity = BoolVal;
    if (GConfig->GetBool(*SectionName, TEXT("bAutoFocusConfiguredCave"), BoolVal, IOCWizard::ConfigFile))
        bAutoFocusConfiguredCave = BoolVal;
    if (GConfig->GetBool(*SectionName, TEXT("bAddLighting"), BoolVal, IOCWizard::ConfigFile))
        bAddLighting = BoolVal;
    if (GConfig->GetBool(*SectionName, TEXT("bAddPlayer"), BoolVal, IOCWizard::ConfigFile))
        bAddPlayer = BoolVal;
    if (GConfig->GetBool(*SectionName, TEXT("bReconfigureExisting"), BoolVal, IOCWizard::ConfigFile))
        bReconfigureExisting = BoolVal;
    if (GConfig->GetInt(*SectionName, TEXT("SelectedExistingIndex"), IntVal, IOCWizard::ConfigFile))
        SelectedExistingIndex = IntVal;
    if (GConfig->GetString(*SectionName, TEXT("ProfileName"), StringVal, IOCWizard::ConfigFile) && !StringVal.IsEmpty())
        ProfileName = StringVal;

    SelectedPresetIndex = FMath::Clamp(SelectedPresetIndex, 0, IOCWizard::CustomIndex);
    if (!bIsCustom && SelectedPresetIndex < IOCWizard::CustomIndex &&
        FMath::IsNearlyEqual(CustomTextureTiling, 0.005f, 0.0001f))
    {
        CustomTextureTiling = IOC_GetRecommendedTextureTiling(IOCWizard::Presets[SelectedPresetIndex].Preset);
    }
}

// ============================================================================
// PerformSetup
// ============================================================================
void SIOCSetupWizard::PerformSetup()
{
    if (bGenerationDone || bIsGenerating) return;

    bSetupAttempted = true;
    SetupError.Empty();
    GenerationSummary.Empty();
    GenerationStats.Empty();
    UnbindConfiguredCaveDelegates();
    LastConfiguredCave.Reset();

    UWorld* World = IOCWizard::GetEditorWorld();
    if (!World)
    {
        SetupError = TEXT("No editor world is available. Open or create a level, then run the wizard again.");
        bIsGenerating = false;
        return;
    }

    // Feature 9: Start progress
    bIsGenerating = true;
    GenerationProgress = 0.1f;

    FActorSpawnParameters SP;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // Feature 10: Use transaction for undo
    FScopedTransaction Transaction(
        LOCTEXT("IOCSetup", "IOC Setup Wizard - Generate Cave"));

    // Feature 8: Apply preset-specific environment defaults
    ApplyEnvironmentDefaults();

    GenerationProgress = 0.15f;

    if (bAddLighting)
    {
        float FogDensity = 0.02f;
        if (!bIsCustom && SelectedPresetIndex < IOCWizard::CustomIndex)
        {
            FogDensity = IOCWizard::Presets[SelectedPresetIndex].RecommendedFogDensity;
        }

        ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-50, -30, 0), SP);
        if (Sun)
        {
            Sun->SetActorRotation(FRotator(-50.0f, -30.0f, 0.0f));
            if (UDirectionalLightComponent* DLC = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
            {
                DLC->SetIntensity(6.0f);
                DLC->CastShadows = true;
            }
            SpawnedActors.Add(Sun);
        }

        ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
        if (Sky)
        {
            Sky->GetLightComponent()->SetRealTimeCapture(true);
            SpawnedActors.Add(Sky);
        }

        AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
        if (Fog)
        {
            Fog->GetComponent()->SetFogDensity(FogDensity);
            Fog->GetComponent()->SetFogHeightFalloff(0.2f);
            SpawnedActors.Add(Fog);
        }

        APostProcessVolume* PPV = World->SpawnActor<APostProcessVolume>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
        if (PPV)
        {
            PPV->bUnbound = true;
            PPV->Settings.bOverride_AutoExposureMinBrightness = true;
            PPV->Settings.AutoExposureMinBrightness = 0.03f;
            PPV->Settings.bOverride_AutoExposureMaxBrightness = true;
            PPV->Settings.AutoExposureMaxBrightness = 2.0f;
            SpawnedActors.Add(PPV);
        }

        const bool bPresetUsesPointLights = !bIsCustom
            && SelectedPresetIndex < IOCWizard::CustomIndex
            && IOCWizard::Presets[SelectedPresetIndex].bRecommendsPointLights;
        if (bPresetUsesPointLights)
        {
            static const FVector PointLightLocations[] = {
                FVector(-1200.0f, -180.0f, 320.0f),
                FVector(0.0f, 220.0f, 360.0f),
                FVector(1200.0f, -160.0f, 320.0f)
            };

            for (const FVector& LightLocation : PointLightLocations)
            {
                APointLight* PointLight = World->SpawnActor<APointLight>(LightLocation, FRotator::ZeroRotator, SP);
                if (PointLight)
                {
                    if (UPointLightComponent* PLC = Cast<UPointLightComponent>(PointLight->GetLightComponent()))
                    {
                        PLC->SetIntensity(3200.0f);
                        PLC->SetAttenuationRadius(900.0f);
                        PLC->SetLightColor(FLinearColor(1.0f, 0.72f, 0.42f));
                        PLC->CastShadows = true;
                    }
                    SpawnedActors.Add(PointLight);
                }
            }
        }
    }

    GenerationProgress = 0.3f;

    // Feature 7: Reconfigure existing actor if requested
    AIOCProceduralActor* Cave = nullptr;
    bool bReconfiguredExisting = false;
    bool bCreatedNewCave = false;
    if (bReconfigureExisting && ExistingCaves.Num() > 0 && SelectedExistingIndex >= 0
        && SelectedExistingIndex < ExistingCaves.Num() && ExistingCaves[SelectedExistingIndex].IsValid())
    {
        Cave = ExistingCaves[SelectedExistingIndex].Get();
        bReconfiguredExisting = true;
        CaptureRollbackSnapshot(Cave);
    }
    else if (bReconfigureExisting)
    {
        SetupError = TEXT("The selected cave actor is no longer available. Go back, refresh the wizard, or choose to spawn a new cave actor.");
        for (auto& WeakActor : SpawnedActors)
        {
            if (WeakActor.IsValid())
            {
                WeakActor->Destroy();
            }
        }
        SpawnedActors.Reset();
        bIsGenerating = false;
        return;
    }

    if (!Cave)
    {
        RollbackSnapshot = FIOCSetupWizardRollbackSnapshot();
        DestroyPreviewActor();

        FTransform SpawnTransform = FTransform::Identity;
        Cave = World->SpawnActorDeferred<AIOCProceduralActor>(
            AIOCProceduralActor::StaticClass(), SpawnTransform, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        bCreatedNewCave = (Cave != nullptr);
    }

    GenerationProgress = 0.4f;

    if (Cave)
    {
        Cave->SetFlags(RF_Transactional);
        Cave->Modify();
        ApplyWizardSettingsToCave(Cave, false);

        if (bCreatedNewCave)
        {
            Cave->FinishSpawning(FTransform::Identity);
        }
        else if (bReconfiguredExisting)
        {
            Cave->RerunConstructionScripts();
        }

        if (IOCWizard::ShouldGenerateImmediately(World))
        {
            GenerationProgress = 0.6f;
            Cave->GenerateCave();
            GenerationProgress = 0.9f;
        }

        if (bCreatedNewCave)
        {
            SpawnedActors.Add(Cave);
        }
        LastConfiguredCave = Cave;
        ViewModel.bLastSetupReconfiguredExisting = bReconfiguredExisting;
        BindConfiguredCaveDelegates(Cave);
    }
    else
    {
        SetupError = TEXT("Failed to create a cave actor. Check the Output Log for spawn errors.");
        for (auto& WeakActor : SpawnedActors)
        {
            if (WeakActor.IsValid())
            {
                WeakActor->Destroy();
            }
        }
        SpawnedActors.Reset();
        bIsGenerating = false;
        return;
    }

    if (bAddPlayer)
    {
        AIOCCharacter* Player = World->SpawnActor<AIOCCharacter>(
            FVector(0, 0, 150), FRotator::ZeroRotator, SP);
        if (Player) SpawnedActors.Add(Player);
    }

    const bool bGeneratedImmediately = IOCWizard::ShouldGenerateImmediately(World);
    bIsGenerating = bGeneratedImmediately && LastConfiguredCave.IsValid() && LastConfiguredCave->bIsGeneratingDisplay;
    bGenerationDone = !bIsGenerating;
    GenerationProgress = bIsGenerating ? 0.92f : 1.0f;
    BuildGenerationSummary();

    if (LastConfiguredCave.IsValid())
    {
        if (bIsGenerating)
        {
            GenerationStats = TEXT("Generation has started. Waiting for final metrics...");
        }
        else
        {
            CollectGenerationStats(LastConfiguredCave.Get());
            if (bAutoFocusConfiguredCave)
            {
                FocusConfiguredCave();
            }
        }
    }

    RequestExistingCaveRefresh(0.0);
    RequestValidationRefresh(0.0);
    SaveSettings();
}

// ============================================================================
// Helpers
// ============================================================================
EIOCCavePreset SIOCSetupWizard::GetSelectedPreset() const
{
    if (bIsCustom || SelectedPresetIndex >= IOCWizard::CustomIndex) return EIOCCavePreset::Custom;
    return IOCWizard::Presets[FMath::Clamp(SelectedPresetIndex, 0, 4)].Preset;
}

FString SIOCSetupWizard::GetSelectedPresetName() const
{
    if (bIsCustom || SelectedPresetIndex >= IOCWizard::CustomIndex) return TEXT("Custom");
    return IOCWizard::Presets[FMath::Clamp(SelectedPresetIndex, 0, 4)].Name;
}

FText SIOCSetupWizard::GetNextButtonText() const
{
    switch (CurrentPage)
    {
        case 0: return INVTEXT("Get Started");
        case 1: return INVTEXT("Environment Options");
        case 2: return INVTEXT("Review Setup");
        case 3: return ViewModel.bValidationPassed ? INVTEXT("Generate Cave") : INVTEXT("Resolve Issues First");
        case 4: return INVTEXT("Finish");
        default: return INVTEXT("Next");
    }
}

#undef SelectedPresetIndex
#undef bIsCustom
#undef bCustomTunnelMode
#undef bCustomUseSpline
#undef CustomSeed
#undef CustomBounds
#undef CustomTunnelStart
#undef CustomTunnelEnd
#undef CustomVoxelSize
#undef CustomTunnelRadius
#undef CustomWallThickness
#undef CustomNoiseFrequency
#undef CustomSmoothIterations
#undef CustomDomainWarpIntensity
#undef CustomTerraceSteps
#undef CustomTextureTiling
#undef CustomLODDistance
#undef CustomLODMultiplier
#undef bPreviewFullFidelity
#undef bAutoFocusConfiguredCave
#undef bAddLighting
#undef bAddPlayer
#undef bReconfigureExisting
#undef SelectedExistingIndex
#undef ProfileName
#undef bStarterAssetsPrepared
#undef bSetupAttempted
#undef bGenerationDone
#undef bIsGenerating
#undef GenerationSummary
#undef SetupError
#undef StarterAssetStatus
#undef StarterLevelStatus
#undef InstallationStatus
#undef ProfileStatus
#undef PreviewStatus
#undef GenerationStats
#undef LastPreviewBuiltAt
#undef LastConfiguredCave
#undef PreviewActor
#undef ExistingCaves
#undef GenerationProgress

#undef LOCTEXT_NAMESPACE

#endif
