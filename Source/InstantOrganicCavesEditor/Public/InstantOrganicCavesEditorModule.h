// Copyright (c) 2026 GregOrigin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** Editor-side logging, separate from the runtime module's LogIOC. */
DECLARE_LOG_CATEGORY_EXTERN(LogIOCEditor, Log, All);

/**
 * Owns everything that only exists in the editor: the setup wizard, the Tools menu,
 * installation validation, the documentation opener, and the authoring capture command.
 *
 * It also supplies the runtime module's showcase with level-viewport control through
 * FIOCShowcaseViewportHooks, so the runtime module never has to depend on LevelEditor.
 */
class FInstantOrganicCavesEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** Runs the installation checks and writes a report to the Output Log. */
    static bool ValidateInstallation();

    /** Opens the shipped local documentation, falling back to the website. */
    static void OpenDocumentation();

private:
    void RegisterEditorMenus();
    void UnregisterEditorMenus();
    void RegisterShowcaseViewportHooks();
    void UnregisterShowcaseViewportHooks();
};
