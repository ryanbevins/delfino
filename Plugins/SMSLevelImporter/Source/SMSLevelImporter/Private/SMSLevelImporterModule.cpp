// Copyright ryana. All Rights Reserved.

#include "SMSLevelImporterModule.h"
#include "UI/SSMSImporterWindow.h"
#include "ToolMenus.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "Framework/Application/SlateApplication.h"

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
            FUIAction(FExecuteAction::CreateRaw(this, &FSMSLevelImporterModule::OpenImporterWindow))
        );
    }
}

void FSMSLevelImporterModule::OpenImporterWindow()
{
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(FText::FromString(TEXT("SMS Level Importer")))
        .ClientSize(FVector2D(900, 650))
        .SupportsMinimize(true)
        .SupportsMaximize(true)
        [
            SNew(SSMSImporterWindow)
        ];

    FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSMSLevelImporterModule, SMSLevelImporter)
