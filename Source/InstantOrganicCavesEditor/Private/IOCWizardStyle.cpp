// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "IOCWizardStyle.h"

#include "InstantOrganicCavesEditorModule.h"
#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

TSharedPtr<FSlateStyleSet> FIOCWizardStyle::StyleInstance = nullptr;

const FName FIOCWizardStyle::HeroBrush(TEXT("IOC.Hero"));
const FName FIOCWizardStyle::EmblemBrush(TEXT("IOC.Emblem"));
const FName FIOCWizardStyle::BackdropBrush(TEXT("IOC.Backdrop"));
const FName FIOCWizardStyle::ScrimDownBrush(TEXT("IOC.ScrimDown"));
const FName FIOCWizardStyle::VignetteBrush(TEXT("IOC.Vignette"));
const FName FIOCWizardStyle::GregOriginBrush(TEXT("IOC.GregOrigin"));
const FName FIOCWizardStyle::CardPanel(TEXT("IOC.Panel.Card"));
const FName FIOCWizardStyle::CardPanelHover(TEXT("IOC.Panel.CardHover"));
const FName FIOCWizardStyle::GlassPanel(TEXT("IOC.Panel.Glass"));
const FName FIOCWizardStyle::ChipPill(TEXT("IOC.Chip"));
const FName FIOCWizardStyle::CardBody(TEXT("IOC.Panel.CardBody"));
const FName FIOCWizardStyle::SpineLeft(TEXT("IOC.Panel.SpineLeft"));
const FName FIOCWizardStyle::ButtonPrimary(TEXT("IOC.Button.Primary"));
const FName FIOCWizardStyle::ButtonSecondary(TEXT("IOC.Button.Secondary"));
const FName FIOCWizardStyle::ButtonDanger(TEXT("IOC.Button.Danger"));

namespace IOCStyleColors
{
    // Sampled from the shipped brand art rather than invented, so the chrome sits in the
    // same world as the imagery instead of fighting it. Values are linear.
    //   panel   <- the deep blue-greens of IOC2's cave walls,  rgb(10, 34, 47)
    //   teal    <- the mid water tone of IOC1,                 rgb(73, 147, 167)
    //   cyan    <- IOC1's brightest highlights,                rgb(102, 192, 204)
    //   moss    <- the logo mark's body
    static const FLinearColor Panel(0.008f, 0.020f, 0.028f, 1.0f);
    static const FLinearColor Surface(0.018f, 0.038f, 0.050f, 1.0f);
    static const FLinearColor SurfaceHover(0.034f, 0.070f, 0.090f, 1.0f);
    static const FLinearColor SurfacePressed(0.010f, 0.024f, 0.032f, 1.0f);

    static const FLinearColor Teal(0.064f, 0.298f, 0.394f, 1.0f);
    static const FLinearColor Cyan(0.133f, 0.536f, 0.612f, 1.0f);
    static const FLinearColor CyanBright(0.230f, 0.720f, 0.800f, 1.0f);
    static const FLinearColor Moss(0.180f, 0.300f, 0.090f, 1.0f);
    static const FLinearColor MossBright(0.300f, 0.470f, 0.150f, 1.0f);
    static const FLinearColor Danger(0.320f, 0.070f, 0.040f, 1.0f);
    static const FLinearColor DangerBright(0.480f, 0.120f, 0.060f, 1.0f);

    static const FLinearColor Outline(0.10f, 0.28f, 0.34f, 1.0f);

    // Panel fills are translucent on purpose: the wizard draws cave art behind everything,
    // and these are what it shows through. The alpha is the whole design -- at 1.0 the
    // backdrop is invisible and the wizard is flat grey boxes again.
    //
    // Keep in sync with the IOCWizard:: mirrors in IOCSetupWizard.cpp, which still tint a
    // few plain white borders directly.
    static const FLinearColor CardFill(0.018f, 0.038f, 0.050f, 0.72f);
    static const FLinearColor CardFillHover(0.034f, 0.070f, 0.090f, 0.86f);
    static const FLinearColor GlassFill(0.020f, 0.045f, 0.058f, 0.55f);
    static const FLinearColor ChipFill(0.064f, 0.298f, 0.394f, 0.34f);
    static const FLinearColor OutlineSoft(0.10f, 0.28f, 0.34f, 0.55f);
}

namespace
{
    /**
     * One button style with real hover and press states.
     *
     * The wizard previously used FCoreStyle "NoBorder" for every button and painted a flat
     * SBorder behind the label, which meant no hover response and no press response at all.
     * Here the three brushes differ in fill and outline, and PressedPadding shifts the label
     * a pixel down and right so a click physically depresses the label -- that offset is what
     * actually reads as tactile.
     */
    FButtonStyle MakeIOCButtonStyle(
        const FLinearColor& Fill,
        const FLinearColor& HoverFill,
        const FLinearColor& PressFill,
        const FLinearColor& OutlineHover)
    {
        const FVector4 Radius(6.0f, 6.0f, 6.0f, 6.0f);

        FSlateRoundedBoxBrush Normal(Fill, Radius, IOCStyleColors::Outline, 1.0f, FVector2f(64.0f, 28.0f));
        FSlateRoundedBoxBrush Hovered(HoverFill, Radius, OutlineHover, 1.5f, FVector2f(64.0f, 28.0f));
        FSlateRoundedBoxBrush Pressed(PressFill, Radius, OutlineHover, 1.5f, FVector2f(64.0f, 28.0f));

        return FButtonStyle()
            .SetNormal(Normal)
            .SetHovered(Hovered)
            .SetPressed(Pressed)
            .SetNormalPadding(FMargin(0.0f, 0.0f, 0.0f, 1.0f))
            .SetPressedPadding(FMargin(1.0f, 1.0f, 0.0f, 0.0f));
    }
}

void FIOCWizardStyle::Initialize()
{
    if (!StyleInstance.IsValid())
    {
        StyleInstance = Create();
        FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
    }
}

void FIOCWizardStyle::Shutdown()
{
    if (StyleInstance.IsValid())
    {
        FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
        ensure(StyleInstance.IsUnique());
        StyleInstance.Reset();
    }
}

const ISlateStyle& FIOCWizardStyle::Get()
{
    Initialize();
    return *StyleInstance;
}

FName FIOCWizardStyle::GetStyleSetName()
{
    static FName Name(TEXT("IOCWizardStyle"));
    return Name;
}

static FString IOCResolveUIDir()
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("InstantOrganicCaves"));
    return Plugin.IsValid() ? (Plugin->GetBaseDir() / TEXT("Resources") / TEXT("UI")) : FString();
}

FString FIOCWizardStyle::GetHeroImagePath()
{
    return IOCResolveUIDir() / TEXT("IOC_Hero.jpg");
}

FString FIOCWizardStyle::GetEmblemImagePath()
{
    return IOCResolveUIDir() / TEXT("IOC_Emblem.png");
}

FString FIOCWizardStyle::GetBackdropImagePath()
{
    return IOCResolveUIDir() / TEXT("IOC_Backdrop.jpg");
}

FString FIOCWizardStyle::GetScrimImagePath()
{
    return IOCResolveUIDir() / TEXT("IOC_ScrimDown.png");
}

FString FIOCWizardStyle::GetVignetteImagePath()
{
    return IOCResolveUIDir() / TEXT("IOC_Vignette.png");
}

FString FIOCWizardStyle::GetGregOriginImagePath()
{
    return IOCResolveUIDir() / TEXT("IOC_GregOrigin.png");
}

TArray<FString> FIOCWizardStyle::GetAllImagePaths()
{
    return {
        GetHeroImagePath(),
        GetEmblemImagePath(),
        GetBackdropImagePath(),
        GetScrimImagePath(),
        GetVignetteImagePath(),
        GetGregOriginImagePath(),
    };
}

TSharedRef<FSlateStyleSet> FIOCWizardStyle::Create()
{
    TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(GetStyleSetName()));

    const FString UIDir = IOCResolveUIDir();
    if (UIDir.IsEmpty())
    {
        UE_LOG(LogIOCEditor, Warning,
            TEXT("Wizard style: the InstantOrganicCaves plugin could not be found, so its ")
            TEXT("branding images cannot be loaded. The wizard will fall back to plain chrome."));
    }
    Style->SetContentRoot(UIDir);

    // An unresolved brush draws a checkerboard and says nothing, so check the files exist
    // and say so here rather than letting the wizard quietly look broken.
    for (const FString& Path : GetAllImagePaths())
    {
        if (!UIDir.IsEmpty() && !FPaths::FileExists(Path))
        {
            // Warning, not Error: a missing decorative image must not escalate an entire
            // editor session to critical-error status (which fails unrelated automation
            // runs). Content.WizardStyleAssets is the hard gate for this.
            UE_LOG(LogIOCEditor, Warning,
                TEXT("Wizard style: branding image '%s' is missing, so the wizard will draw ")
                TEXT("placeholder squares there. It ships in Resources/UI; reinstall the ")
                TEXT("plugin if this build lacks it."), *Path);
        }
    }

    // Brand imagery. Sizes are the natural pixel sizes; widgets scale them down.
    Style->Set(HeroBrush, new FSlateImageBrush(GetHeroImagePath(), FVector2f(1240.0f, 648.0f)));
    Style->Set(EmblemBrush, new FSlateImageBrush(GetEmblemImagePath(), FVector2f(256.0f, 256.0f)));
    Style->Set(BackdropBrush, new FSlateImageBrush(GetBackdropImagePath(), FVector2f(1600.0f, 893.0f)));
    Style->Set(VignetteBrush, new FSlateImageBrush(GetVignetteImagePath(), FVector2f(512.0f, 288.0f)));

    // Authored at ~2x its on-screen size and painted white, so the footer can tint it to
    // whatever the surrounding chrome needs without shipping a second file.
    Style->Set(GregOriginBrush, new FSlateImageBrush(GetGregOriginImagePath(), FVector2f(595.0f, 71.0f)));

    // Stretched vertically wherever it is used, so a 16px-wide strip is plenty. Tinted at
    // the use site to whatever colour the content underneath should fade into.
    Style->Set(ScrimDownBrush, new FSlateImageBrush(GetScrimImagePath(), FVector2f(16.0f, 256.0f)));

    // Panel chrome. A rounded rect with a lit hairline edge is what makes a translucent
    // panel read as a panel instead of a smudge -- the outline is doing as much work as
    // the fill.
    Style->Set(CardPanel, new FSlateRoundedBoxBrush(
        IOCStyleColors::CardFill, FVector4(8.0f, 8.0f, 8.0f, 8.0f),
        IOCStyleColors::OutlineSoft, 1.0f, FVector2f(64.0f, 48.0f)));

    Style->Set(CardPanelHover, new FSlateRoundedBoxBrush(
        IOCStyleColors::CardFillHover, FVector4(8.0f, 8.0f, 8.0f, 8.0f),
        IOCStyleColors::Cyan, 1.0f, FVector2f(64.0f, 48.0f)));

    Style->Set(GlassPanel, new FSlateRoundedBoxBrush(
        IOCStyleColors::GlassFill, FVector4(10.0f, 10.0f, 10.0f, 10.0f),
        IOCStyleColors::OutlineSoft, 1.0f, FVector2f(64.0f, 48.0f)));

    // Radius is half the capsule's height rather than an arbitrarily large number: the
    // rounded-box shader draws the corner arc at the radius it is given, so overshooting
    // it flattens the sides instead of rounding them further.
    Style->Set(ChipPill, new FSlateRoundedBoxBrush(
        IOCStyleColors::ChipFill, FVector4(11.0f, 11.0f, 11.0f, 11.0f),
        IOCStyleColors::Teal, 1.0f, FVector2f(64.0f, 22.0f)));

    // Corner radii are (TopLeft, TopRight, BottomRight, BottomLeft), so these two are
    // mirror halves that meet on a shared straight edge.
    Style->Set(CardBody, new FSlateRoundedBoxBrush(
        IOCStyleColors::CardFill, FVector4(0.0f, 8.0f, 8.0f, 0.0f),
        IOCStyleColors::OutlineSoft, 1.0f, FVector2f(64.0f, 48.0f)));

    Style->Set(SpineLeft, new FSlateRoundedBoxBrush(
        FLinearColor::White, FVector4(3.0f, 0.0f, 0.0f, 3.0f),
        FLinearColor::Transparent, 0.0f, FVector2f(3.0f, 48.0f)));

    Style->Set(ButtonPrimary, MakeIOCButtonStyle(
        IOCStyleColors::Teal, IOCStyleColors::Cyan, IOCStyleColors::Panel,
        IOCStyleColors::CyanBright));

    Style->Set(ButtonSecondary, MakeIOCButtonStyle(
        IOCStyleColors::Surface, IOCStyleColors::SurfaceHover, IOCStyleColors::SurfacePressed,
        IOCStyleColors::Cyan));

    Style->Set(ButtonDanger, MakeIOCButtonStyle(
        IOCStyleColors::Danger, IOCStyleColors::DangerBright, IOCStyleColors::Danger,
        IOCStyleColors::DangerBright));

    return Style;
}
