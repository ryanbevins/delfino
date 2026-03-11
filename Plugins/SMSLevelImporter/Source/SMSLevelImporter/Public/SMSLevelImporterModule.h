// Copyright ryana. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSMSImporter, Log, All);

class FSMSLevelImporterModule : public IModuleInterface
{
public:
    /** IModuleInterface implementation */
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    /** Opens the SMS Level Importer Slate window */
    void OpenImporterWindow();

private:
    /** Registers the Tools menu entry */
    void RegisterMenus();
};
