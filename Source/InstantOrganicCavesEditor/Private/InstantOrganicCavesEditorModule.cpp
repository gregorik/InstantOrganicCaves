// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#include "InstantOrganicCavesEditorModule.h"

#include "IOCSetupWizard.h"
#include "IOCWizardStyle.h"
#include "InstantOrganicCavesModule.h"
#include "IOCProceduralActor.h"
#include "IOCShowcaseLauncher.h"

#include "Editor.h"
#include "LevelEditorSubsystem.h"
#include "LevelEditorViewport.h"
#include "ToolMenus.h"
#include "Styling/AppStyle.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Interfaces/IPluginManager.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "InstantOrganicCavesEditor"

DEFINE_LOG_CATEGORY(LogIOCEditor);

IMPLEMENT_MODULE(FInstantOrganicCavesEditorModule, InstantOrganicCavesEditor)

namespace
{
    void ShowNotification(const FText& Message,
        SNotificationItem::ECompletionState State = SNotificationItem::CS_None,
        float ExpireDuration = 4.0f)
    {
        FNotificationInfo Notification(Message);
        Notification.ExpireDuration = ExpireDuration;
        Notification.bFireAndForget = true;
        Notification.bUseLargeFont = false;

        if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Notification))
        {
            Item->SetCompletionState(State);
        }
    }

    // ---- Showcase viewport control, supplied to the runtime module -------------------

    FVector GSavedViewLocation = FVector::ZeroVector;
    FRotator GSavedViewRotation = FRotator::ZeroRotator;
    bool GSavedRealtime = false;

    FLevelEditorViewportClient* FindShowcaseEditorViewport()
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

    // ---- Authoring capture command ---------------------------------------------------

    FTSTicker::FDelegateHandle GWizardPresetCaptureRetryHandle;

    void CaptureWizardPresetCmd(const TArray<FString>& Args)
    {
        const FString PresetName = Args.Num() > 0 ? Args[0] : TEXT("LargeTunnel");
        if (GWizardPresetCaptureRetryHandle.IsValid())
        {
            FTSTicker::GetCoreTicker().RemoveTicker(GWizardPresetCaptureRetryHandle);
            GWizardPresetCaptureRetryHandle.Reset();
        }

        TSharedRef<int32, ESPMode::ThreadSafe> Attempts = MakeShared<int32, ESPMode::ThreadSafe>(0);
        UE_LOG(LogIOCEditor, Log, TEXT("Scheduling rendered setup wizard capture for preset '%s'."), *PresetName);

        GWizardPresetCaptureRetryHandle = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateLambda([PresetName, Attempts](float)
            {
                if (SIOCSetupWizard::CaptureRenderedPresetPreview(PresetName, true))
                {
                    UE_LOG(LogIOCEditor, Log, TEXT("Rendered setup wizard capture started for preset '%s'."), *PresetName);
                    GWizardPresetCaptureRetryHandle.Reset();
                    return false;
                }

                ++(*Attempts);
                if (*Attempts == 1 || (*Attempts % 10) == 0)
                {
                    UE_LOG(LogIOCEditor, Log, TEXT("Waiting for editor world/viewport before capturing preset '%s' (attempt %d)."),
                        *PresetName,
                        *Attempts);
                }

                if (*Attempts >= 60)
                {
                    UE_LOG(LogIOCEditor, Warning, TEXT("Timed out waiting to capture rendered setup wizard preset '%s'."), *PresetName);
                    GWizardPresetCaptureRetryHandle.Reset();
                    FPlatformMisc::RequestExit(false);
                    return false;
                }

                return true;
            }),
            0.5f);
    }

    void ValidateInstallationCmd(const TArray<FString>& Args)
    {
        FInstantOrganicCavesEditorModule::ValidateInstallation();
    }

    void OpenSetupWizardCmd(const TArray<FString>& Args)
    {
        // Slate needs a real window, so this is meaningless in a headless run -- say so
        // rather than silently doing nothing.
        if (!FApp::CanEverRender())
        {
            UE_LOG(LogIOCEditor, Warning,
                TEXT("IOC.OpenSetupWizard needs a rendering editor; this session cannot render."));
            return;
        }

        UE_LOG(LogIOCEditor, Display, TEXT("Opening the IOC setup wizard."));
        SIOCSetupWizard::OpenWizard();
    }
}

// ---------------------------------------------------------------------------------------

bool FInstantOrganicCavesEditorModule::ValidateInstallation()
{
    bool bAllOk = true;

    auto LogCheck = [&bAllOk](const TCHAR* Label, bool bOk, const FString& Detail)
    {
        bAllOk &= bOk;
        UE_LOG(LogIOCEditor, Display, TEXT("Validate: %s %s - %s"),
            bOk ? TEXT("[OK]") : TEXT("[FAIL]"),
            Label,
            *Detail);
    };

    const TSharedPtr<IPlugin> IOCPlugin = IPluginManager::Get().FindPlugin(TEXT("InstantOrganicCaves"));
    LogCheck(TEXT("Plugin"), IOCPlugin.IsValid() && IOCPlugin->IsEnabled(),
        IOCPlugin.IsValid() ? IOCPlugin->GetBaseDir() : TEXT("InstantOrganicCaves plugin not found"));

    UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    LogCheck(TEXT("Editor World"), EditorWorld != nullptr,
        EditorWorld ? TEXT("Level is open") : TEXT("Open or create a level"));

    LogCheck(TEXT("Plugin Content"), FPackageName::DoesPackageExist(TEXT("/InstantOrganicCaves/MI_IOC_CaveWalls")),
        TEXT("/InstantOrganicCaves content mount"));

    LogCheck(TEXT("Smart Material Instance"),
        LoadObject<UObject>(nullptr, TEXT("/InstantOrganicCaves/MI_IOC_SmartCave_Inst.MI_IOC_SmartCave_Inst")) != nullptr,
        TEXT("Starter assets ship with the plugin. Reinstall the plugin if this asset is missing."));

    LogCheck(TEXT("Starter Blueprint"),
        LoadObject<UObject>(nullptr, TEXT("/InstantOrganicCaves/BP_IOC_Cave.BP_IOC_Cave")) != nullptr,
        TEXT("Starter assets ship with the plugin. Reinstall the plugin if this asset is missing."));

    LogCheck(TEXT("Cave Actor Class"), AIOCProceduralActor::StaticClass() != nullptr,
        TEXT("/Script/InstantOrganicCaves.IOCProceduralActor"));

    LogCheck(TEXT("Showcase Launcher Class"),
        AIOCShowcaseLauncher::StaticClass() != nullptr,
        TEXT("Required for starter/showcase level"));

    const TSharedPtr<IPlugin> PythonPlugin = IPluginManager::Get().FindPlugin(TEXT("PythonScriptPlugin"));
    LogCheck(TEXT("Python Automation (Optional)"), true,
        PythonPlugin.IsValid() && PythonPlugin->IsEnabled()
            ? TEXT("Enabled for optional Resources/*.py automation helpers.")
            : TEXT("Not required. The setup wizard uses built-in editor setup for starter assets and the showcase map."));

    const TSharedPtr<IPlugin> PCGPlugin = IPluginManager::Get().FindPlugin(TEXT("PCG"));
    LogCheck(TEXT("PCG Plugin"), PCGPlugin.IsValid() && PCGPlugin->IsEnabled(),
        TEXT("Required for IOC Voxel Core PCG workflows"));

    UE_LOG(LogIOCEditor, Display, TEXT("Validate: %s"), bAllOk
        ? TEXT("Installation validation passed.")
        : TEXT("Installation validation found issues."));

    ShowNotification(
        bAllOk
            ? LOCTEXT("ValidationPassed", "Instant Organic Caves validation passed. Details were written to the Output Log.")
            : LOCTEXT("ValidationFailed", "Instant Organic Caves validation found issues. Review the Output Log for details."),
        bAllOk ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail,
        5.0f);

    return bAllOk;
}

void FInstantOrganicCavesEditorModule::OpenDocumentation()
{
    FString DocsPath;
    if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("InstantOrganicCaves")))
    {
        DocsPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("Docs"), TEXT("index.html"));
    }

    if (!DocsPath.IsEmpty() && FPaths::FileExists(DocsPath))
    {
        FPlatformProcess::LaunchFileInDefaultExternalApplication(*DocsPath);
        return;
    }

    FString Error;
    FPlatformProcess::LaunchURL(TEXT("https://www.gregorigin.com/InstantOrganicCaves/"), TEXT(""), &Error);
    if (!Error.IsEmpty())
    {
        ShowNotification(
            FText::Format(LOCTEXT("DocsOpenFailed", "Could not open IOC documentation: {0}"), FText::FromString(Error)),
            SNotificationItem::CS_Fail,
            6.0f);
    }
}

void FInstantOrganicCavesEditorModule::RegisterShowcaseViewportHooks()
{
    FIOCShowcaseViewportHooks& Hooks = IOCGetShowcaseViewportHooks();

    Hooks.CaptureState = []()
    {
        if (FLevelEditorViewportClient* ViewportClient = FindShowcaseEditorViewport())
        {
            GSavedViewLocation = ViewportClient->GetViewLocation();
            GSavedViewRotation = ViewportClient->GetViewRotation();
            GSavedRealtime = ViewportClient->IsRealtime();
            ViewportClient->SetRealtime(true);
        }
    };

    Hooks.RestoreState = []()
    {
        if (FLevelEditorViewportClient* ViewportClient = FindShowcaseEditorViewport())
        {
            ViewportClient->SetViewLocation(GSavedViewLocation);
            ViewportClient->SetViewRotation(GSavedViewRotation);
            ViewportClient->SetRealtime(GSavedRealtime);
            ViewportClient->Invalidate();
        }
    };

    Hooks.ApplyView = [](const FVector& Location, const FRotator& Rotation)
    {
        if (FLevelEditorViewportClient* ViewportClient = FindShowcaseEditorViewport())
        {
            ViewportClient->SetViewLocation(Location);
            ViewportClient->SetViewRotation(Rotation);
            ViewportClient->Invalidate();
        }
    };

    Hooks.RedrawViewports = []()
    {
        if (GEditor)
        {
            GEditor->RedrawAllViewports(false);
        }
    };
}

void FInstantOrganicCavesEditorModule::UnregisterShowcaseViewportHooks()
{
    // The runtime module can outlive this one, and the lambdas above capture nothing but
    // still point at code in this DLL. Clearing them avoids a stale call after unload.
    IOCGetShowcaseViewportHooks() = FIOCShowcaseViewportHooks();
}

void FInstantOrganicCavesEditorModule::RegisterEditorMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    auto AddEntry = [](FToolMenuSection& Section, const FName EntryName, const FText& Label,
        const FText& Tooltip, FExecuteAction ExecuteAction)
    {
        Section.AddMenuEntry(EntryName, Label, Tooltip, FSlateIcon(), FUIAction(ExecuteAction));
    };

    if (UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window"))
    {
        FToolMenuSection& Section = WindowMenu->FindOrAddSection("WindowLayout");
        AddEntry(
            Section,
            TEXT("IOC_SetupWizard"),
            LOCTEXT("MenuSetupWizard", "IOC Setup Wizard..."),
            LOCTEXT("MenuSetupWizardTip", "Open the Instant Organic Caves guided setup wizard."),
            FExecuteAction::CreateStatic(&SIOCSetupWizard::OpenWizard));
    }

    if (UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools"))
    {
        FToolMenuSection& Section = ToolsMenu->FindOrAddSection("Plugins");
        Section.AddSubMenu(
            TEXT("IOCQuickActions"),
            LOCTEXT("MenuRoot", "Instant Organic Caves"),
            LOCTEXT("MenuRootTip", "Quick access to the IOC setup wizard, validation, and showcase actions."),
            FNewToolMenuDelegate::CreateLambda([AddEntry](UToolMenu* SubMenu)
            {
                FToolMenuSection& QuickStart = SubMenu->FindOrAddSection("IOCQuickStart");
                AddEntry(
                    QuickStart,
                    TEXT("IOC_OpenWizard"),
                    LOCTEXT("MenuOpenWizard", "Open Setup Wizard"),
                    LOCTEXT("MenuOpenWizardTip", "Launch the guided setup wizard for cave generation and starter content."),
                    FExecuteAction::CreateStatic(&SIOCSetupWizard::OpenWizard));
                AddEntry(
                    QuickStart,
                    TEXT("IOC_Validate"),
                    LOCTEXT("MenuValidate", "Validate Installation"),
                    LOCTEXT("MenuValidateTip", "Run the built-in IOC validation checks and write details to the Output Log."),
                    FExecuteAction::CreateLambda([]()
                    {
                        FInstantOrganicCavesEditorModule::ValidateInstallation();
                    }));
                AddEntry(
                    QuickStart,
                    TEXT("IOC_Docs"),
                    LOCTEXT("MenuDocs", "Open Documentation"),
                    LOCTEXT("MenuDocsTip", "Open the local IOC documentation page or website."),
                    FExecuteAction::CreateStatic(&FInstantOrganicCavesEditorModule::OpenDocumentation));

                FToolMenuSection& DemoSection = SubMenu->FindOrAddSection("IOCDemos");
                AddEntry(
                    DemoSection,
                    TEXT("IOC_TunnelDemo"),
                    LOCTEXT("MenuTunnelDemo", "Spawn Tunnel Demo"),
                    LOCTEXT("MenuTunnelDemoTip", "Spawn a ready-to-play tunnel demo in the current world."),
                    FExecuteAction::CreateLambda([]()
                    {
                        FInstantOrganicCavesModule::SpawnTunnelDemo();
                    }));
                AddEntry(
                    DemoSection,
                    TEXT("IOC_SpectacularDemo"),
                    LOCTEXT("MenuSpectacularDemo", "Spawn Spectacular Demo"),
                    LOCTEXT("MenuSpectacularDemoTip", "Spawn the dramatic crystal cave demo with mood lighting and scatter layers."),
                    FExecuteAction::CreateLambda([]()
                    {
                        FInstantOrganicCavesModule::SpawnSpectacularDemo();
                    }));
                AddEntry(
                    DemoSection,
                    TEXT("IOC_ShowcaseStandard"),
                    LOCTEXT("MenuShowcase", "Spawn Showcase Flythrough"),
                    LOCTEXT("MenuShowcaseTip", "Launch the 8-section automated IOC showcase with captions."),
                    FExecuteAction::CreateLambda([]()
                    {
                        FIOCShowcaseOptions Options;
                        Options.bCaptureMode = false;
                        Options.bShowCaptions = true;
                        FInstantOrganicCavesModule::SpawnShowcase(Options);
                    }));
                AddEntry(
                    DemoSection,
                    TEXT("IOC_ShowcaseCapture"),
                    LOCTEXT("MenuShowcaseCapture", "Spawn Capture Showcase"),
                    LOCTEXT("MenuShowcaseCaptureTip", "Launch the capture-polished IOC showcase."),
                    FExecuteAction::CreateLambda([]()
                    {
                        FIOCShowcaseOptions Options;
                        Options.bCaptureMode = true;
                        Options.bShowCaptions = true;
                        Options.bLoop = false;
                        FInstantOrganicCavesModule::SpawnShowcase(Options);
                    }));
                AddEntry(
                    DemoSection,
                    TEXT("IOC_ClearShowcase"),
                    LOCTEXT("MenuClearShowcase", "Clear Showcase"),
                    LOCTEXT("MenuClearShowcaseTip", "Destroy the showcase flythrough actors and restore the camera. Leaves the tunnel and spectacular demos in place."),
                    FExecuteAction::CreateLambda([]()
                    {
                        FInstantOrganicCavesModule::ClearShowcase();
                    }));

                AddEntry(
                    DemoSection,
                    TEXT("IOC_OpenDemoMap"),
                    LOCTEXT("MenuOpenDemoMap", "Open Demo Map"),
                    LOCTEXT("MenuOpenDemoMapTip", "Open the shipped IOC demo level, then press Play for the guided showcase."),
                    FExecuteAction::CreateLambda([]()
                    {
                        // The map lives in plugin content, which the Content Browser hides
                        // unless "Show Plugin Content" is enabled -- so a menu entry is the
                        // only reliable way a customer finds it.
                        static const TCHAR* DemoMapPackage = TEXT("/InstantOrganicCaves/Maps/IOC_DemoMap");
                        if (!FPackageName::DoesPackageExist(DemoMapPackage))
                        {
                            ShowNotification(LOCTEXT("DemoMapMissing",
                                "The IOC demo map is not installed with this plugin build."),
                                SNotificationItem::CS_Fail);
                            return;
                        }
                        if (GEditor)
                        {
                            if (ULevelEditorSubsystem* LevelSubsystem =
                                    GEditor->GetEditorSubsystem<ULevelEditorSubsystem>())
                            {
                                LevelSubsystem->LoadLevel(DemoMapPackage);
                            }
                        }
                    }));

                AddEntry(
                    DemoSection,
                    TEXT("IOC_ClearAllDemos"),
                    LOCTEXT("MenuClearAllDemos", "Clear All Demos"),
                    LOCTEXT("MenuClearAllDemosTip", "Remove every actor the IOC demos spawned -- showcase, tunnel demo, spectacular demo and the demo character."),
                    FExecuteAction::CreateLambda([]()
                    {
                        FInstantOrganicCavesModule::ClearAllDemos();
                    }));
            }),
            false,
            FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.GameSettings"));
    }
}

void FInstantOrganicCavesEditorModule::UnregisterEditorMenus()
{
    if (UToolMenus::TryGet())
    {
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
    }
}

void FInstantOrganicCavesEditorModule::StartupModule()
{
    FIOCWizardStyle::Initialize();

    RegisterShowcaseViewportHooks();

    // Authoring-only screenshot tool: it spawns actors into whatever level is open, moves the
    // user's viewport, and can quit the editor. Registered only with -IOCDevTools.
    if (FParse::Param(FCommandLine::Get(), TEXT("IOCDevTools")))
    {
        IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("IOC.CaptureWizardPreset"),
            TEXT("Authoring tool. Renders a setup-wizard preset preview to Saved/IOCPreviewShots and exits the editor. Example: IOC.CaptureWizardPreset LargeTunnel"),
            FConsoleCommandWithArgsDelegate::CreateStatic(&CaptureWizardPresetCmd),
            ECVF_Cheat);
    }

    IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("IOC.ValidateInstallation"),
        TEXT("Validates IOC plugin content, starter assets, Python, PCG, required classes, and editor world state."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&ValidateInstallationCmd),
        ECVF_Default);

    // Opens the guided setup wizard. The menu entries do the same thing, but a console
    // command is scriptable: it lets an automation run or a support session put the wizard
    // on screen without a human navigating menus.
    IConsoleManager::Get().RegisterConsoleCommand(
        TEXT("IOC.OpenSetupWizard"),
        TEXT("Opens the Instant Organic Caves guided setup wizard. Requires a rendering editor."),
        FConsoleCommandWithArgsDelegate::CreateStatic(&OpenSetupWizardCmd),
        ECVF_Default);

    UToolMenus::RegisterStartupCallback(
        FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FInstantOrganicCavesEditorModule::RegisterEditorMenus));
}

void FInstantOrganicCavesEditorModule::ShutdownModule()
{
    FIOCWizardStyle::Shutdown();
    UnregisterEditorMenus();
    UnregisterShowcaseViewportHooks();

    // This ticker holds a pointer into this module; leaving it armed crashes on hot reload.
    if (GWizardPresetCaptureRetryHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(GWizardPresetCaptureRetryHandle);
        GWizardPresetCaptureRetryHandle.Reset();
    }

    for (const TCHAR* Command : { TEXT("IOC.CaptureWizardPreset"), TEXT("IOC.ValidateInstallation"),
                                  TEXT("IOC.OpenSetupWizard") })
    {
        if (IConsoleObject* Cmd = IConsoleManager::Get().FindConsoleObject(Command))
        {
            IConsoleManager::Get().UnregisterConsoleObject(Cmd);
        }
    }
}

#undef LOCTEXT_NAMESPACE
