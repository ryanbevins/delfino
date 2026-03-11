// SSMSImporterWindow.h - Slate editor window for the SMS Level Importer
//
// Provides a full GUI for selecting a GameCube ISO, browsing available
// SMS levels/episodes, configuring import options, and running the import.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"

struct FSMSLevelInfo;
struct FSMSImportOptions;
class FSMSSceneLoader;

/**
 * Main Slate window for the SMS Level Importer.
 *
 * Allows users to:
 *  - Browse for a GameCube ISO file
 *  - View available levels and episodes
 *  - Configure import options (geometry, textures, collision, etc.)
 *  - Run the import with progress feedback
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

	// UI builders
	TSharedRef<SWidget> BuildISOSection();
	TSharedRef<SWidget> BuildLevelBrowser();
	TSharedRef<SWidget> BuildImportOptions();
	TSharedRef<SWidget> BuildProgressSection();

	// Rebuild the level list widget from AvailableLevels
	void BuildLevelList();

	// Callbacks
	FReply OnBrowseISO();
	void OnISOSelected(const FString& Path);
	FReply OnImportClicked();
	void OnImportProgress(float Progress, const FString& Message);
	void OnLevelCheckChanged(ECheckBoxState State, FString Key);
	void OnLevelGroupCheckChanged(ECheckBoxState State, FString InternalName);

	// Progress state
	float CurrentProgress = 0.0f;
	FString CurrentStatus = TEXT("Ready");
	bool bIsImporting = false;

	// Cached widgets that need updating
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<SProgressBar> ProgressBar;
	TSharedPtr<SVerticalBox> LevelListBox;
	TSharedPtr<STextBlock> ISOPathText;
	TSharedPtr<SButton> ImportButton;
};
