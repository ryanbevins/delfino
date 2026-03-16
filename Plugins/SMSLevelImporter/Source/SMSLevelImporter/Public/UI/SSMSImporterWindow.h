// SSMSImporterWindow.h - Slate editor window for the SMS Level Importer
//
// Provides a full GUI with tabbed layout for selecting a GameCube ISO,
// browsing levels/episodes, browsing character archives, configuring
// import options, and running the import.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Scene/SMSSceneLoader.h"
#include "Scene/SMSLevelDefinitions.h"

/**
 * Main Slate window for the SMS Level Importer.
 *
 * Layout:
 *   ISO file selection header
 *   [Levels] [Characters] [Settings] tab bar
 *   Active tab content
 *   Import button + progress bar + status text
 */
class SSMSImporterWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSMSImporterWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// State
	TSharedPtr<FSMSSceneLoader> SceneLoader;
	FSMSImportOptions ImportOptions;
	FString ISOPath;
	TArray<FSMSLevelInfo> AvailableLevels;

	// Track which levels/episodes are selected for import
	// Key: "levelname:episode" e.g., "dolpic:0"
	TSet<FString> SelectedScenes;

	// Character browser state
	TArray<FSMSCharacterInfo> AvailableCharacters;
	TSet<FString> SelectedCharacters;          // Archive paths
	TMap<FString, TSet<FString>> SelectedBcks; // Per-character BCK selection

	// Tab tracking
	int32 ActiveTabIndex = 0;
	TSharedPtr<SWidgetSwitcher> TabSwitcher;

	// UI builders
	TSharedRef<SWidget> BuildISOSection();
	TSharedRef<SWidget> BuildTabBar();
	TSharedRef<SWidget> BuildLevelBrowser();
	TSharedRef<SWidget> BuildCharacterBrowser();
	TSharedRef<SWidget> BuildImportOptions();
	TSharedRef<SWidget> BuildProgressSection();

	// Rebuild lists from data
	void BuildLevelList();
	void BuildCharacterList();

	// Callbacks
	FReply OnBrowseISO();
	void OnISOSelected(const FString& Path);
	FReply OnImportClicked();
	void OnImportProgress(float Progress, const FString& Message);
	void OnLevelCheckChanged(ECheckBoxState State, FString Key);
	void OnLevelGroupCheckChanged(ECheckBoxState State, FString InternalName);
	void OnCharacterCheckChanged(ECheckBoxState State, FString ArchivePath);
	void OnBckCheckChanged(ECheckBoxState State, FString ArchivePath, FString BckPath);
	void SetActiveTab(int32 TabIndex);

	// Progress state
	float CurrentProgress = 0.0f;
	FString CurrentStatus = TEXT("Ready");
	bool bIsImporting = false;

	// Cached widgets that need updating
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<SProgressBar> ProgressBar;
	TSharedPtr<SVerticalBox> LevelListBox;
	TSharedPtr<SVerticalBox> CharacterListBox;
	TSharedPtr<STextBlock> ISOPathText;
	TSharedPtr<SButton> ImportButton;
};
