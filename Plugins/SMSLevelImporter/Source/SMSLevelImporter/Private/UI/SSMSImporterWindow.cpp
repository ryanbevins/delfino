// SSMSImporterWindow.cpp - Implementation of the SMS Level Importer Slate window

#include "UI/SSMSImporterWindow.h"
#include "Scene/SMSSceneLoader.h"
#include "Scene/SMSLevelDefinitions.h"
#include "SMSLevelImporterModule.h"

#include "DesktopPlatformModule.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Notifications/SProgressBar.h"

#define LOCTEXT_NAMESPACE "SSMSImporterWindow"

void SSMSImporterWindow::Construct(const FArguments& InArgs)
{
	// Initialize default import options
	ImportOptions.bImportGeometry = true;
	ImportOptions.bImportTextures = true;
	ImportOptions.bImportCollision = true;
	ImportOptions.bImportObjects = true;
	ImportOptions.bImportAnimations = true;
	ImportOptions.ScaleFactor = 1.0f;
	ImportOptions.OutputDirectory = TEXT("/Game/SMS");

	ChildSlot
	[
		SNew(SVerticalBox)

		// ISO file selection section
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 4)
		[
			BuildISOSection()
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 4)
		[
			SNew(SSeparator)
		]

		// Main body: level browser + import options side by side
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8, 4)
		[
			SNew(SHorizontalBox)

			// Level browser (left side)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 4, 0)
			[
				BuildLevelBrowser()
			]

			// Import options (right side)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4, 0, 0, 0)
			[
				BuildImportOptions()
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 0)
		[
			SNew(SSeparator)
		]

		// Progress section and import button
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
		[
			BuildProgressSection()
		]
	];
}

// ----------------------------------------------------------------------------
// UI Builder: ISO File Section
// ----------------------------------------------------------------------------

TSharedRef<SWidget> SSMSImporterWindow::BuildISOSection()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("ISO File:")))
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(4)
			[
				SAssignNew(ISOPathText, STextBlock)
				.Text(FText::FromString(TEXT("No ISO selected")))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(4)
		[
			SNew(SButton)
			.Text(FText::FromString(TEXT("Browse...")))
			.OnClicked(FOnClicked::CreateSP(this, &SSMSImporterWindow::OnBrowseISO))
		];
}

// ----------------------------------------------------------------------------
// UI Builder: Level Browser
// ----------------------------------------------------------------------------

TSharedRef<SWidget> SSMSImporterWindow::BuildLevelBrowser()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Levels")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
			]

			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(LevelListBox, SVerticalBox)
				]
			]
		];
}

void SSMSImporterWindow::BuildLevelList()
{
	if (!LevelListBox.IsValid())
	{
		return;
	}

	LevelListBox->ClearChildren();

	for (const FSMSLevelInfo& Level : AvailableLevels)
	{
		// Parent checkbox for the level area
		LevelListBox->AddSlot().AutoHeight().Padding(2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SCheckBox)
				.OnCheckStateChanged_Lambda([this, InternalName = Level.InternalName](ECheckBoxState State)
				{
					OnLevelGroupCheckChanged(State, InternalName);
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Level.DisplayName))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
		];

		// Child checkboxes for each episode
		if (Level.MaxEpisodes == -1)
		{
			// Special archive: single entry, no episodes
			FString Key = FString::Printf(TEXT("%s:0"), *Level.InternalName);
			LevelListBox->AddSlot().AutoHeight().Padding(20, 1, 2, 1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)
				[
					SNew(SCheckBox)
					.OnCheckStateChanged(
						SCheckBox::FOnCheckStateChanged::CreateSP(
							this, &SSMSImporterWindow::OnLevelCheckChanged, Key))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("Scene")))
				]
			];
		}
		else
		{
			for (int32 Ep = 0; Ep < Level.MaxEpisodes; ++Ep)
			{
				FString Key = FString::Printf(TEXT("%s:%d"), *Level.InternalName, Ep);
				FString Label = FString::Printf(TEXT("Episode %d"), Ep);

				LevelListBox->AddSlot().AutoHeight().Padding(20, 1, 2, 1)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(2)
					[
						SNew(SCheckBox)
						.OnCheckStateChanged(
							SCheckBox::FOnCheckStateChanged::CreateSP(
								this, &SSMSImporterWindow::OnLevelCheckChanged, Key))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Label))
					]
				];
			}
		}
	}
}

// ----------------------------------------------------------------------------
// UI Builder: Import Options
// ----------------------------------------------------------------------------

TSharedRef<SWidget> SSMSImporterWindow::BuildImportOptions()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Import Options")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
			]

			// Geometry
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
			[
				SNew(SCheckBox)
				.IsChecked(ImportOptions.bImportGeometry ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					ImportOptions.bImportGeometry = (State == ECheckBoxState::Checked);
				})
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Geometry")))
				]
			]

			// Textures
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
			[
				SNew(SCheckBox)
				.IsChecked(ImportOptions.bImportTextures ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					ImportOptions.bImportTextures = (State == ECheckBoxState::Checked);
				})
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Textures")))
				]
			]

			// Collision
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
			[
				SNew(SCheckBox)
				.IsChecked(ImportOptions.bImportCollision ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					ImportOptions.bImportCollision = (State == ECheckBoxState::Checked);
				})
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Collision")))
				]
			]

			// Objects
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
			[
				SNew(SCheckBox)
				.IsChecked(ImportOptions.bImportObjects ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					ImportOptions.bImportObjects = (State == ECheckBoxState::Checked);
				})
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Objects")))
				]
			]

			// Animations
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 2)
			[
				SNew(SCheckBox)
				.IsChecked(ImportOptions.bImportAnimations ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					ImportOptions.bImportAnimations = (State == ECheckBoxState::Checked);
				})
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Animations")))
				]
			]

			// Scale factor
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 8, 4, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Scale:")))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SSpinBox<float>)
					.MinValue(0.01f)
					.MaxValue(100.0f)
					.Value(ImportOptions.ScaleFactor)
					.OnValueChanged_Lambda([this](float Value)
					{
						ImportOptions.ScaleFactor = Value;
					})
				]
			]

			// Output directory
			+ SVerticalBox::Slot().AutoHeight().Padding(4, 8, 4, 2)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Output:")))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(ImportOptions.OutputDirectory))
					.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
					{
						ImportOptions.OutputDirectory = Text.ToString();
					})
				]
			]
		];
}

// ----------------------------------------------------------------------------
// UI Builder: Progress Section
// ----------------------------------------------------------------------------

TSharedRef<SWidget> SSMSImporterWindow::BuildProgressSection()
{
	return SNew(SVerticalBox)

		// Import button
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SAssignNew(ImportButton, SButton)
			.Text(FText::FromString(TEXT("Import Selected")))
			.HAlign(HAlign_Center)
			.OnClicked(FOnClicked::CreateSP(this, &SSMSImporterWindow::OnImportClicked))
		]

		// Progress bar
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
		[
			SAssignNew(ProgressBar, SProgressBar)
			.Percent(0.0f)
		]

		// Status text
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 2)
		[
			SAssignNew(StatusText, STextBlock)
			.Text(FText::FromString(TEXT("Ready")))
		];
}

// ----------------------------------------------------------------------------
// Callbacks
// ----------------------------------------------------------------------------

FReply SSMSImporterWindow::OnBrowseISO()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	TArray<FString> OutFiles;
	bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Select GameCube ISO"),
		TEXT(""),
		TEXT(""),
		TEXT("GameCube ISO (*.iso)|*.iso"),
		EFileDialogFlags::None,
		OutFiles);

	if (bOpened && OutFiles.Num() > 0)
	{
		OnISOSelected(OutFiles[0]);
	}

	return FReply::Handled();
}

void SSMSImporterWindow::OnISOSelected(const FString& Path)
{
	ISOPath = Path;
	ISOPathText->SetText(FText::FromString(Path));

	SceneLoader = MakeShared<FSMSSceneLoader>();
	if (SceneLoader->OpenISO(Path))
	{
		AvailableLevels = SceneLoader->GetAvailableLevels();
		SelectedScenes.Empty();
		BuildLevelList();

		UE_LOG(LogSMSImporter, Log, TEXT("Opened ISO: %s — Region: %s, %d levels available"),
			*Path, *SceneLoader->GetRegion(), AvailableLevels.Num());
	}
	else
	{
		ISOPathText->SetText(FText::FromString(TEXT("ERROR: Not a valid SMS ISO")));
		SceneLoader.Reset();
		AvailableLevels.Empty();
		SelectedScenes.Empty();
		BuildLevelList();
	}
}

FReply SSMSImporterWindow::OnImportClicked()
{
	if (bIsImporting || !SceneLoader.IsValid() || SelectedScenes.Num() == 0)
	{
		return FReply::Handled();
	}

	bIsImporting = true;
	ImportButton->SetEnabled(false);
	CurrentProgress = 0.0f;
	StatusText->SetText(FText::FromString(TEXT("Starting import...")));
	ProgressBar->SetPercent(0.0f);

	// Import each selected scene sequentially
	for (const FString& Key : SelectedScenes)
	{
		FString LevelName, EpisodeStr;
		Key.Split(TEXT(":"), &LevelName, &EpisodeStr);
		int32 Episode = FCString::Atoi(*EpisodeStr);

		SceneLoader->ImportScene(LevelName, Episode, ImportOptions,
			FOnSMSImportProgress::CreateSP(this, &SSMSImporterWindow::OnImportProgress));

		if (SceneLoader->IsCancelled())
		{
			break;
		}
	}

	bIsImporting = false;
	ImportButton->SetEnabled(true);
	CurrentStatus = TEXT("Import complete!");
	StatusText->SetText(FText::FromString(CurrentStatus));
	ProgressBar->SetPercent(1.0f);

	return FReply::Handled();
}

void SSMSImporterWindow::OnImportProgress(float Progress, const FString& Message)
{
	CurrentProgress = Progress;
	CurrentStatus = Message;

	if (ProgressBar.IsValid())
	{
		ProgressBar->SetPercent(Progress);
	}
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(
			FString::Printf(TEXT("%.0f%% %s"), Progress * 100.0f, *Message)));
	}
}

void SSMSImporterWindow::OnLevelCheckChanged(ECheckBoxState State, FString Key)
{
	if (State == ECheckBoxState::Checked)
	{
		SelectedScenes.Add(Key);
	}
	else
	{
		SelectedScenes.Remove(Key);
	}
}

void SSMSImporterWindow::OnLevelGroupCheckChanged(ECheckBoxState State, FString InternalName)
{
	// Find the level info for this internal name
	for (const FSMSLevelInfo& Level : AvailableLevels)
	{
		if (Level.InternalName == InternalName)
		{
			if (Level.MaxEpisodes == -1)
			{
				// Special archive: single scene
				FString Key = FString::Printf(TEXT("%s:0"), *InternalName);
				if (State == ECheckBoxState::Checked)
				{
					SelectedScenes.Add(Key);
				}
				else
				{
					SelectedScenes.Remove(Key);
				}
			}
			else
			{
				// Toggle all episodes
				for (int32 Ep = 0; Ep < Level.MaxEpisodes; ++Ep)
				{
					FString Key = FString::Printf(TEXT("%s:%d"), *InternalName, Ep);
					if (State == ECheckBoxState::Checked)
					{
						SelectedScenes.Add(Key);
					}
					else
					{
						SelectedScenes.Remove(Key);
					}
				}
			}
			break;
		}
	}
}

#undef LOCTEXT_NAMESPACE
