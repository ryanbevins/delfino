// SSMSImporterWindow.cpp - Implementation of the SMS Level Importer Slate window

#include "UI/SSMSImporterWindow.h"
#include "Scene/SMSSceneLoader.h"
#include "Scene/SMSLevelDefinitions.h"
#include "SMSLevelImporterModule.h"

#include "DesktopPlatformModule.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SSeparator.h"
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

		// Tab bar
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 0)
		[
			BuildTabBar()
		]

		// Tab content area
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(8, 4)
		[
			SAssignNew(TabSwitcher, SWidgetSwitcher)
			.WidgetIndex(0)

			// Tab 0: Levels
			+ SWidgetSwitcher::Slot()
			[
				BuildLevelBrowser()
			]

			// Tab 1: Characters
			+ SWidgetSwitcher::Slot()
			[
				BuildCharacterBrowser()
			]

			// Tab 2: Settings
			+ SWidgetSwitcher::Slot()
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
			.OnClicked_Lambda([this]() { return OnBrowseISO(); })
		];
}

// ----------------------------------------------------------------------------
// UI Builder: Tab Bar
// ----------------------------------------------------------------------------

TSharedRef<SWidget> SSMSImporterWindow::BuildTabBar()
{
	auto MakeTabButton = [this](const FString& Label, int32 TabIndex) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.IsChecked_Lambda([this, TabIndex]()
			{
				return ActiveTabIndex == TabIndex ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, TabIndex](ECheckBoxState)
			{
				SetActiveTab(TabIndex);
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.Margin(FMargin(8, 2))
			];
	};

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 2, 0)
		[
			MakeTabButton(TEXT("Levels"), 0)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
		[
			MakeTabButton(TEXT("Characters"), 1)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2, 0)
		[
			MakeTabButton(TEXT("Settings"), 2)
		];
}

void SSMSImporterWindow::SetActiveTab(int32 TabIndex)
{
	ActiveTabIndex = TabIndex;
	if (TabSwitcher.IsValid())
	{
		TabSwitcher->SetActiveWidgetIndex(TabIndex);
	}
}

// ----------------------------------------------------------------------------
// UI Builder: Level Browser (Tab 0)
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
			FString Key = FString::Printf(TEXT("%s:0"), *Level.InternalName);
			LevelListBox->AddSlot().AutoHeight().Padding(20, 1, 2, 1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)
				[
					SNew(SCheckBox)
					.OnCheckStateChanged_Lambda([this, Key](ECheckBoxState State)
					{
						OnLevelCheckChanged(State, Key);
					})
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
						.OnCheckStateChanged_Lambda([this, Key](ECheckBoxState State)
						{
							OnLevelCheckChanged(State, Key);
						})
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
// UI Builder: Character Browser (Tab 1)
// ----------------------------------------------------------------------------

TSharedRef<SWidget> SSMSImporterWindow::BuildCharacterBrowser()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(4)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(4)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Characters")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
			]

			+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(CharacterListBox, SVerticalBox)
				]
			]
		];
}

void SSMSImporterWindow::BuildCharacterList()
{
	if (!CharacterListBox.IsValid())
	{
		return;
	}

	CharacterListBox->ClearChildren();

	if (AvailableCharacters.Num() == 0)
	{
		CharacterListBox->AddSlot().AutoHeight().Padding(4)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("No character archives found.\nOpen an ISO to scan for characters.")))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		];
		return;
	}

	for (const FSMSCharacterInfo& Character : AvailableCharacters)
	{
		FString ArchivePath = Character.ArchivePath;

		// Parent checkbox for the character
		CharacterListBox->AddSlot().AutoHeight().Padding(2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, ArchivePath]()
				{
					return SelectedCharacters.Contains(ArchivePath) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, ArchivePath](ECheckBoxState State)
				{
					OnCharacterCheckChanged(State, ArchivePath);
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Character.DisplayName))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			]
		];

		// Child checkboxes for each BCK animation
		for (const FString& BckPath : Character.BckFiles)
		{
			FString BckFilename = FPaths::GetCleanFilename(BckPath);

			CharacterListBox->AddSlot().AutoHeight().Padding(20, 1, 2, 1)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(2)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this, ArchivePath, BckPath]()
					{
						const TSet<FString>* Bcks = SelectedBcks.Find(ArchivePath);
						return (Bcks && Bcks->Contains(BckPath)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this, ArchivePath, BckPath](ECheckBoxState State)
					{
						OnBckCheckChanged(State, ArchivePath, BckPath);
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2)
				[
					SNew(STextBlock)
					.Text(FText::FromString(BckFilename))
				]
			];
		}
	}
}

// ----------------------------------------------------------------------------
// UI Builder: Import Options (Tab 2 — Settings)
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
			.OnClicked_Lambda([this]() { return OnImportClicked(); })
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
		// Populate level list
		AvailableLevels = SceneLoader->GetAvailableLevels();
		SelectedScenes.Empty();
		BuildLevelList();

		// Scan for character archives
		AvailableCharacters = SceneLoader->ScanCharacterArchives();
		SelectedCharacters.Empty();
		SelectedBcks.Empty();
		BuildCharacterList();

		UE_LOG(LogSMSImporter, Log, TEXT("Opened ISO: %s — Region: %s, %d levels, %d characters"),
			*Path, *SceneLoader->GetRegion(), AvailableLevels.Num(), AvailableCharacters.Num());
	}
	else
	{
		ISOPathText->SetText(FText::FromString(TEXT("ERROR: Not a valid SMS ISO")));
		SceneLoader.Reset();
		AvailableLevels.Empty();
		SelectedScenes.Empty();
		AvailableCharacters.Empty();
		SelectedCharacters.Empty();
		SelectedBcks.Empty();
		BuildLevelList();
		BuildCharacterList();
	}
}

FReply SSMSImporterWindow::OnImportClicked()
{
	if (bIsImporting || !SceneLoader.IsValid())
	{
		return FReply::Handled();
	}

	const bool bHasLevels = SelectedScenes.Num() > 0;
	const bool bHasCharacters = SelectedCharacters.Num() > 0;

	if (!bHasLevels && !bHasCharacters)
	{
		return FReply::Handled();
	}

	bIsImporting = true;
	ImportButton->SetEnabled(false);
	CurrentProgress = 0.0f;
	StatusText->SetText(FText::FromString(TEXT("Starting import...")));
	ProgressBar->SetPercent(0.0f);

	const int32 TotalItems = SelectedScenes.Num() + SelectedCharacters.Num();
	int32 CurrentItem = 0;

	// Import selected levels
	for (const FString& Key : SelectedScenes)
	{
		FString LevelName, EpisodeStr;
		Key.Split(TEXT(":"), &LevelName, &EpisodeStr);
		int32 Episode = FCString::Atoi(*EpisodeStr);

		// Scale progress per item
		auto PerItemProgress = FOnSMSImportProgress::CreateLambda(
			[this, CurrentItem, TotalItems](float SubProgress, const FString& Message)
		{
			float OverallProgress = (static_cast<float>(CurrentItem) + SubProgress) / FMath::Max(1, TotalItems);
			OnImportProgress(OverallProgress, Message);
		});

		SceneLoader->ImportScene(LevelName, Episode, ImportOptions, PerItemProgress);

		if (SceneLoader->IsCancelled())
		{
			break;
		}
		CurrentItem++;
	}

	// Import selected characters
	if (!SceneLoader->IsCancelled())
	{
		for (const FSMSCharacterInfo& Character : AvailableCharacters)
		{
			if (!SelectedCharacters.Contains(Character.ArchivePath))
			{
				continue;
			}

			// Get selected BCKs for this character
			TSet<FString> CharBcks;
			if (const TSet<FString>* BckSet = SelectedBcks.Find(Character.ArchivePath))
			{
				CharBcks = *BckSet;
			}

			auto PerItemProgress = FOnSMSImportProgress::CreateLambda(
				[this, CurrentItem, TotalItems](float SubProgress, const FString& Message)
			{
				float OverallProgress = (static_cast<float>(CurrentItem) + SubProgress) / FMath::Max(1, TotalItems);
				OnImportProgress(OverallProgress, Message);
			});

			SceneLoader->ImportCharacter(Character, CharBcks, ImportOptions, PerItemProgress);

			if (SceneLoader->IsCancelled())
			{
				break;
			}
			CurrentItem++;
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
	for (const FSMSLevelInfo& Level : AvailableLevels)
	{
		if (Level.InternalName == InternalName)
		{
			if (Level.MaxEpisodes == -1)
			{
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

void SSMSImporterWindow::OnCharacterCheckChanged(ECheckBoxState State, FString ArchivePath)
{
	if (State == ECheckBoxState::Checked)
	{
		SelectedCharacters.Add(ArchivePath);

		// Also select all BCK files for this character
		for (const FSMSCharacterInfo& Character : AvailableCharacters)
		{
			if (Character.ArchivePath == ArchivePath)
			{
				TSet<FString>& Bcks = SelectedBcks.FindOrAdd(ArchivePath);
				for (const FString& BckPath : Character.BckFiles)
				{
					Bcks.Add(BckPath);
				}
				break;
			}
		}
	}
	else
	{
		SelectedCharacters.Remove(ArchivePath);
		SelectedBcks.Remove(ArchivePath);
	}
}

void SSMSImporterWindow::OnBckCheckChanged(ECheckBoxState State, FString ArchivePath, FString BckPath)
{
	TSet<FString>& Bcks = SelectedBcks.FindOrAdd(ArchivePath);

	if (State == ECheckBoxState::Checked)
	{
		Bcks.Add(BckPath);
		// Also ensure the parent character is selected
		SelectedCharacters.Add(ArchivePath);
	}
	else
	{
		Bcks.Remove(BckPath);
		// If no BCKs remain selected, deselect the character
		if (Bcks.Num() == 0)
		{
			SelectedCharacters.Remove(ArchivePath);
			SelectedBcks.Remove(ArchivePath);
		}
	}
}

#undef LOCTEXT_NAMESPACE
