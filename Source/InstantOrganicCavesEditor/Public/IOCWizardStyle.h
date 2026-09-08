// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**
 * Slate style set for the setup wizard: the brand imagery and the button styles that give
 * the wizard its hover/press feedback.
 *
 * Kept separate from IOCSetupWizard.cpp because a style set has to outlive any one widget --
 * it is created on module startup and unregistered on shutdown, while wizard windows come
 * and go.
 *
 * Brushes resolve against the plugin's Resources/UI folder at runtime. An unresolved Slate
 * brush renders as a silent checkerboard with no warning, so Initialize() logs loudly if the
 * content root or either image is missing, and Content.WizardStyleAssets asserts they exist.
 */
class INSTANTORGANICCAVESEDITOR_API FIOCWizardStyle
{
public:
    static void Initialize();
    static void Shutdown();

    /** The registered style set. Only valid between Initialize() and Shutdown(). */
    static const ISlateStyle& Get();

    static FName GetStyleSetName();

    /** Style names, so call sites are not stringly-typed. */
    static const FName HeroBrush;          // wide branded banner, Welcome page
    static const FName EmblemBrush;        // arch mark, page headers
    static const FName BackdropBrush;      // dim cave art behind the whole wizard
    static const FName ScrimDownBrush;     // transparent->opaque vertical fade
    static const FName VignetteBrush;      // clear centre -> opaque edges, corner falloff
    static const FName GregOriginBrush;    // publisher lockup, white on transparency
    static const FName ButtonPrimary;      // the one action a step wants you to take
    static const FName ButtonSecondary;    // everything else
    static const FName ButtonDanger;       // destructive / rollback

    /**
     * Panel chrome.
     *
     * These carry their own fill *and* hairline outline, which is why they are brushes
     * rather than colours passed to a white SBorder: a rounded rect with a lit edge is what
     * separates a panel from the backdrop showing through it, and Slate cannot synthesise
     * that from a tint. Do not set BorderBackgroundColor on a widget using one -- the tint
     * multiplies the outline as well as the fill, and the edge disappears.
     */
    static const FName CardPanel;          // standard content card
    static const FName CardPanelHover;     // the same card under the cursor
    static const FName GlassPanel;         // most translucent; for nested / inset groups
    static const FName ChipPill;           // small capsule label

    // A spined card is drawn as two brushes butted together: an accent bar rounded only on
    // its left, and a body rounded only on its right. Rounding a single full-radius card
    // and laying a bar over it leaves a notch where the bar meets the corner arc, so the
    // radii are split between the two pieces instead. The bar is white, to be tinted per
    // card; the body is not.
    static const FName CardBody;           // right-hand body of a spined card
    static const FName SpineLeft;          // the accent bar, tint at the call site

    /** Absolute paths of the two images, for the test that proves they ship. */
    static FString GetHeroImagePath();
    static FString GetEmblemImagePath();
    static FString GetBackdropImagePath();
    static FString GetScrimImagePath();
    static FString GetVignetteImagePath();
    static FString GetGregOriginImagePath();

    /** Every image the style set expects on disk, for the test that proves they ship. */
    static TArray<FString> GetAllImagePaths();

private:
    static TSharedPtr<FSlateStyleSet> StyleInstance;
    static TSharedRef<FSlateStyleSet> Create();
};
