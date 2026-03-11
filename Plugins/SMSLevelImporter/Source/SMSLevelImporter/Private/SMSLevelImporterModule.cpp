// Copyright ryana. All Rights Reserved.

#include "SMSLevelImporterModule.h"
#include "ToolMenus.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"

#define LOCTEXT_NAMESPACE "FSMSLevelImporterModule"

DEFINE_LOG_CATEGORY(LogSMSImporter);

void FSMSLevelImporterModule::StartupModule()
{
    UE_LOG(LogSMSImporter, Log, TEXT("SMSLevelImporter module starting up."));

    if (!IsRunningCommandlet())
    {
        UToolMenus::RegisterStartupCallback(
            FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FSMSLevelImporterModule::RegisterMenus));
    }
}

void FSMSLevelImporterModule::ShutdownModule()
{
    UE_LOG(LogSMSImporter, Log, TEXT("SMSLevelImporter module shutting down."));
    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
}

void FSMSLevelImporterModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
    if (ToolsMenu)
    {
        FToolMenuSection& Section = ToolsMenu->FindOrAddSection("SMSLevelImporter");
        Section.Label = LOCTEXT("SMSLevelImporterSection", "SMS Level Importer");

        Section.AddMenuEntry(
            "OpenSMSImporter",
            LOCTEXT("OpenSMSImporterLabel", "SMS Level Importer"),
            LOCTEXT("OpenSMSImporterTooltip", "Open the Super Mario Sunshine level importer"),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([]()
            {
                UE_LOG(LogSMSImporter, Log, TEXT("SMS Importer opened"));
            }))
        );
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSMSLevelImporterModule, SMSLevelImporter)
