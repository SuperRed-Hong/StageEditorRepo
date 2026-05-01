// Copyright Stage Editor Plugin. All Rights Reserved.

#include "EditorUI/SStageEditorPanelV2.h"
#include "EditorUI/StageEditorOutliner.h"
#include "EditorUI/StageEditorPanel.h" // For FAssetCreationSettings
#include "EditorLogic/StageEditorController.h"
#include "Actors/Stage.h"
#include "Data/StageRegistryAsset.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "Subsystems/StageEditorSubsystem.h"
#include "DataLayerSync/SRegistryCreationDialog.h"
#include "DataLayerSync/StageMigrationTypes.h"

#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "LevelEditor.h"
#include "IStructureDetailsView.h"
#include "PropertyEditorModule.h"
#include "Framework/Application/SlateApplication.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "DebugHeader.h"

#define LOCTEXT_NAMESPACE "StageEditorPanelV2"

void SStageEditorPanelV2::Construct(const FArguments& InArgs, TSharedPtr<FStageEditorController> InController)
{
	Controller = InController;

	// Bind to Controller updates
	if (Controller.IsValid())
	{
		Controller->OnModelChanged.AddSP(this, &SStageEditorPanelV2::OnModelChanged);
	}

	// Create Creation Settings struct (reuse from V1)
	CreationSettings = MakeShared<FStructOnScope>(FAssetCreationSettings::StaticStruct());
	FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
	*Settings = FAssetCreationSettings(); // Initialize with defaults

	// Create Details View for creation settings
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FStructureDetailsViewArgs StructureViewArgs;
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bHideSelectionTip = true;
	SettingsDetailsView = PropertyModule.CreateStructureDetailView(DetailsViewArgs, StructureViewArgs, CreationSettings);

	// Bind to property changes
	if (SettingsDetailsView.IsValid())
	{
		SettingsDetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &SStageEditorPanelV2::OnAssetCreationSettingsChanged);
	}

	// Initialize Controller's DataLayer asset folder path
	if (Controller.IsValid())
	{
		FString DataLayerPath = Settings->bIsCustomDataLayerAssetPath ?
			Settings->DataLayerAssetFolderPath.Path : TEXT("/StageEditor/DataLayers");
		Controller->SetDataLayerAssetFolderPath(DataLayerPath);
	}

	// Register for map change events
	if (!MapChangedHandle.IsValid())
	{
		MapChangedHandle = FEditorDelegates::OnMapOpened.AddSP(this, &SStageEditorPanelV2::OnMapOpened);
	}

	// Check if World Partition is enabled
	bCachedIsWorldPartition = IsWorldPartitionLevel();

	// Register viewport selection listener
	RegisterViewportSelectionListener();

	// Build the UI
	RebuildUI();
}

SStageEditorPanelV2::~SStageEditorPanelV2()
{
	// Unregister viewport selection listener
	UnregisterViewportSelectionListener();

	// Unregister map change delegate
	if (MapChangedHandle.IsValid())
	{
		FEditorDelegates::OnMapOpened.Remove(MapChangedHandle);
		MapChangedHandle.Reset();
	}

	// Close settings window if open
	CloseSettingsWindow();

	Controller.Reset();
	StageOutliner.Reset();
}

void SStageEditorPanelV2::RebuildUI()
{
	const bool bIsWorldPartition = IsWorldPartitionLevel();
	bCachedIsWorldPartition = bIsWorldPartition;

	if (!bIsWorldPartition)
	{
		// Show World Partition requirement warning
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(20)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(30)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)

					// Warning Icon & Title
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0, 0, 0, 20)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("WPRequired_Title", "World Partition Required"))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
						.ColorAndOpacity(FLinearColor(1.0f, 0.6f, 0.0f))
					]

					// Description
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0, 0, 0, 30)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("WPRequired_Description",
							"StageEditor requires a World Partition level to function properly.\n\n"
							"World Partition enables:\n"
							"  - Dynamic resource streaming via DataLayers\n"
							"  - Efficient memory management for large worlds\n"
							"  - Proper Act-based scene state management\n\n"
							"Please convert your current level to World Partition to continue."))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						.AutoWrapText(true)
						.Justification(ETextJustify::Center)
					]

					// Convert Button
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0, 0, 0, 10)
					[
						SNew(SButton)
						.Text(LOCTEXT("ConvertToWP", "Convert to World Partition"))
						.ToolTipText(LOCTEXT("ConvertToWP_Tooltip", "Convert the current level to World Partition format"))
						.OnClicked(this, &SStageEditorPanelV2::OnConvertToWorldPartitionClicked)
						.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
					]

					// Refresh Button
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0, 0, 0, 20)
					[
						SNew(SButton)
						.Text(LOCTEXT("RefreshWPStatus", "Refresh Status"))
						.ToolTipText(LOCTEXT("RefreshWPStatus_Tooltip", "Refresh World Partition status check"))
						.OnClicked(this, &SStageEditorPanelV2::OnRefreshWorldPartitionStatusClicked)
					]

					// Documentation Link
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("WPDocs", "Learn more about World Partition in UE5 documentation"))
						.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
						.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f))
					]
				]
			]
		];
	}
	else
	{
		// Refresh status caches on initial build
		RefreshStatusCaches();

		// Normal StageEditor V2 UI
		ChildSlot
		[
			SNew(SVerticalBox)

			// Main Toolbar
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5)
			[
				CreateMainToolbar()
			]

			// Quick Create Toolbar
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5, 0, 5, 5)
			[
				CreateQuickCreateToolbar()
			]

			// Registry Warning Banner (shown when Registry is missing)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5, 0, 5, 5)
			[
				CreateRegistryWarningBanner()
			]

			// Registry Info Banner (shows which Registry is being used)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5, 0, 5, 5)
			[
				CreateRegistryInfoBanner()
			]

			// Lock Status Bar (Multi-user mode only)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5, 0, 5, 2)
			[
				CreateLockStatusBar()
			]

			// Sync Warning Banner (Multi-user mode, when out of sync)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5, 0, 5, 2)
			[
				CreateSyncWarningBanner()
			]

			// Separator
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
			]

			// Stage Outliner (SceneOutliner-based)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				CreateOutliner()
			]
		];
	}
}

void SStageEditorPanelV2::RefreshUI()
{
	if (StageOutliner.IsValid())
	{
		StageOutliner->Refresh();
	}
}

TArray<AStage*> SStageEditorPanelV2::GetSelectedStages() const
{
	if (StageOutliner.IsValid())
	{
		return StageOutliner->GetSelectedStages();
	}
	return TArray<AStage*>();
}

TArray<AActor*> SStageEditorPanelV2::GetSelectedEntities() const
{
	if (StageOutliner.IsValid())
	{
		return StageOutliner->GetSelectedEntities();
	}
	return TArray<AActor*>();
}

void SStageEditorPanelV2::SetSelection(AStage* Stage)
{
	if (StageOutliner.IsValid())
	{
		StageOutliner->SetSelection(Stage);
	}
}

void SStageEditorPanelV2::SetSelection(AActor* Entity)
{
	if (StageOutliner.IsValid())
	{
		StageOutliner->SetSelection(Entity);
	}
}

void SStageEditorPanelV2::ExpandAll()
{
	if (StageOutliner.IsValid())
	{
		StageOutliner->ExpandAll();
	}
}

void SStageEditorPanelV2::CollapseAll()
{
	if (StageOutliner.IsValid())
	{
		StageOutliner->CollapseAll();
	}
}

//----------------------------------------------------------------
// UI Construction
//----------------------------------------------------------------

TSharedRef<SWidget> SStageEditorPanelV2::CreateMainToolbar()
{
	return SNew(SHorizontalBox)

		// Register Selected Entities Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("RegisterEntities", "Register Selected Entities"))
			.ToolTipText(LOCTEXT("RegisterEntities_Tooltip", "Register currently selected actors as Entities to the selected Stage"))
			.OnClicked(this, &SStageEditorPanelV2::OnRegisterSelectedEntitiesClicked)
		]

		// Refresh Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(5, 0, 0, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("Refresh", "Refresh"))
			.ToolTipText(LOCTEXT("Refresh_Tooltip", "Manually scan for Stages in the level"))
			.OnClicked(this, &SStageEditorPanelV2::OnRefreshClicked)
		]

		// Clean Orphaned Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(5, 0, 0, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("CleanOrphaned", "Clean Orphaned"))
			.ToolTipText(LOCTEXT("CleanOrphaned_Tooltip", "Clean orphaned Entities (Entities whose owner Stage was deleted)"))
			.OnClicked(this, &SStageEditorPanelV2::OnCleanOrphanedEntitiesClicked)
		]

		// Expand All Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(5, 0, 0, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("ExpandAll", "Expand All"))
			.ToolTipText(LOCTEXT("ExpandAll_Tooltip", "Expand all items in the tree"))
			.OnClicked(this, &SStageEditorPanelV2::OnExpandAllClicked)
		]

		// Collapse All Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(5, 0, 0, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("CollapseAll", "Collapse All"))
			.ToolTipText(LOCTEXT("CollapseAll_Tooltip", "Collapse all items in the tree"))
			.OnClicked(this, &SStageEditorPanelV2::OnCollapseAllClicked)
		];
}

TSharedRef<SWidget> SStageEditorPanelV2::CreateQuickCreateToolbar()
{
	return SNew(SHorizontalBox)

		// Create Stage BP Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("CreateStageBP", "Create Stage BP"))
			.ToolTipText(LOCTEXT("CreateStageBP_Tooltip", "Create a new Stage Blueprint in Content Browser"))
			.OnClicked(this, &SStageEditorPanelV2::OnCreateStageBPClicked)
		]

		// Create Entity Actor BP Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(5, 0, 0, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("CreateEntityActorBP", "Create Entity Actor BP"))
			.ToolTipText(LOCTEXT("CreateEntityActorBP_Tooltip", "Create a new Entity Actor Blueprint in Content Browser"))
			.OnClicked(this, &SStageEditorPanelV2::OnCreateEntityActorBPClicked)
		]

		// Create Entity Component BP Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(5, 0, 0, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("CreateEntityComponentBP", "Create Entity Component BP"))
			.ToolTipText(LOCTEXT("CreateEntityComponentBP_Tooltip", "Create a new Entity Component Blueprint in Content Browser"))
			.OnClicked(this, &SStageEditorPanelV2::OnCreateEntityComponentBPClicked)
		]

		// Settings Gear Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(10, 0, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("OpenSettings_Tooltip", "Open Asset Creation Settings"))
			.OnClicked(this, &SStageEditorPanelV2::OnOpenSettingsClicked)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Settings"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
}

TSharedRef<SWidget> SStageEditorPanelV2::CreateRegistryWarningBanner()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(1.0f, 0.8f, 0.0f, 0.8f))  // Yellow warning
		.Padding(10)
		.Visibility(this, &SStageEditorPanelV2::GetRegistryWarningVisibility)
		[
			SNew(SHorizontalBox)

			// Warning Icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 10, 0)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Warning"))
				.ColorAndOpacity(FLinearColor(1.0f, 0.6f, 0.0f))
			]

			// Warning Text
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RegistryMissing", "Stage Registry not found! Stage IDs will not persist across editor sessions. Click 'Create Registry' to enable persistence."))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.AutoWrapText(true)
			]

			// Create Registry Button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(10, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("CreateRegistry", "Create Registry"))
				.ToolTipText(LOCTEXT("CreateRegistry_Tooltip", "Create a StageRegistryAsset to enable Stage ID persistence"))
				.OnClicked(this, &SStageEditorPanelV2::OnCreateRegistryClicked)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
			]

			// Select Existing Registry Button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(5, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("SelectExistingRegistry", "Select Existing"))
				.ToolTipText(LOCTEXT("SelectExistingRegistry_Tooltip", "Browse and select an existing StageRegistryAsset (e.g., after exporting plugin to new project)"))
				.OnClicked(this, &SStageEditorPanelV2::OnSelectExistingRegistryClicked)
			]
		];
}

TSharedRef<SWidget> SStageEditorPanelV2::CreateRegistryInfoBanner()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(0.1f, 0.3f, 0.5f, 0.6f))  // Dark blue info
		.Padding(8, 4)
		[
			SNew(SHorizontalBox)

			// Info Icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Info"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]

			// Registry Path Label
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 5, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CurrentRegistry", "Current Registry:"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			// Registry Path Value
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SStageEditorPanelV2::GetRegistryPathText)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FLinearColor(0.8f, 0.9f, 1.0f))
			]
		];
}

TSharedRef<SWidget> SStageEditorPanelV2::CreateOutliner()
{
	// Get editor world
	UWorld* World = nullptr;
	if (GEditor)
	{
		World = GEditor->GetEditorWorldContext().World();
	}

	SAssignNew(StageOutliner, SStageEditorOutliner)
		.Controller(Controller)
		.World(World);

	return StageOutliner.ToSharedRef();
}

//----------------------------------------------------------------
// Toolbar Button Handlers
//----------------------------------------------------------------

FReply SStageEditorPanelV2::OnRefreshClicked()
{
	if (Controller.IsValid())
	{
		Controller->FindStageInWorld();
	}
	RefreshUI();
	return FReply::Handled();
}

FReply SStageEditorPanelV2::OnExpandAllClicked()
{
	ExpandAll();
	return FReply::Handled();
}

FReply SStageEditorPanelV2::OnCollapseAllClicked()
{
	CollapseAll();
	return FReply::Handled();
}

FReply SStageEditorPanelV2::OnRegisterSelectedEntitiesClicked()
{
	if (!Controller.IsValid() || !GEditor)
	{
		return FReply::Handled();
	}

	// Get selected Stages from the outliner
	TArray<AStage*> SelectedStages = GetSelectedStages();
	if (SelectedStages.Num() == 0)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Please select a Stage first"));
		return FReply::Handled();
	}

	// Use first selected Stage
	AStage* TargetStage = SelectedStages[0];

	// Get selected actors in viewport
	TArray<AActor*> ActorsToRegister;
	USelection* Selection = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*Selection); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			// Skip Stages themselves
			if (!Actor->IsA<AStage>())
			{
				ActorsToRegister.Add(Actor);
			}
		}
	}

	if (ActorsToRegister.Num() == 0)
	{
		DebugHeader::ShowNotifyInfo(TEXT("No actors selected in viewport"));
		return FReply::Handled();
	}

	// Register entities
	if (Controller->RegisterEntities(ActorsToRegister, TargetStage))
	{
		DebugHeader::ShowNotifyInfo(FString::Printf(
			TEXT("Registered %d actor(s) to Stage '%s'"),
			ActorsToRegister.Num(),
			*TargetStage->GetActorLabel()));
		RefreshUI();
	}

	return FReply::Handled();
}

FReply SStageEditorPanelV2::OnCleanOrphanedEntitiesClicked()
{
	if (!Controller.IsValid())
	{
		return FReply::Handled();
	}

	int32 CleanedCount = Controller->CleanOrphanedEntities();
	if (CleanedCount > 0)
	{
		DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Cleaned %d orphaned entities"), CleanedCount));
		RefreshUI();
	}
	else
	{
		DebugHeader::ShowNotifyInfo(TEXT("No orphaned entities found"));
	}

	return FReply::Handled();
}

FReply SStageEditorPanelV2::OnCreateStageBPClicked()
{
	if (!Controller.IsValid() || !CreationSettings.IsValid())
	{
		return FReply::Handled();
	}

	FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
	FString FolderPath = Settings->bIsCustomStageAssetFolderPath ?
		Settings->StageAssetFolderPath.Path : TEXT("/StageEditor/StagesBP");

	Controller->CreateStageBlueprint(FolderPath, Settings->DefaultStageBlueprintParentClass.Get());

	return FReply::Handled();
}

FReply SStageEditorPanelV2::OnCreateEntityActorBPClicked()
{
	if (!Controller.IsValid() || !CreationSettings.IsValid())
	{
		return FReply::Handled();
	}

	FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
	FString FolderPath = Settings->bIsCustomEntityActorAssetPath ?
		Settings->EntityActorAssetFolderPath.Path : TEXT("/StageEditor/EntitiesBP");

	Controller->CreateEntityActorBlueprint(FolderPath, Settings->DefaultEntityActorBlueprintParentClass.Get());

	return FReply::Handled();
}

FReply SStageEditorPanelV2::OnCreateEntityComponentBPClicked()
{
	if (!Controller.IsValid() || !CreationSettings.IsValid())
	{
		return FReply::Handled();
	}

	FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
	FString FolderPath = Settings->bIsCustomEntityComponentAssetPath ?
		Settings->EntityComponentAssetFolderPath.Path : TEXT("/StageEditor/EntitiesBP");

	Controller->CreateEntityComponentBlueprint(FolderPath, Settings->DefaultEntityComponentBlueprintParentClass.Get());

	return FReply::Handled();
}

FReply SStageEditorPanelV2::OnOpenSettingsClicked()
{
	// Check if window already exists
	if (TSharedPtr<SWindow> ExistingWindow = SettingsWindow.Pin())
	{
		ExistingWindow->BringToFront();
		return FReply::Handled();
	}

	// Create new window
	TSharedRef<SWindow> NewWindow = SNew(SWindow)
		.Title(LOCTEXT("SettingsWindowTitle", "Stage Editor Settings"))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.HasCloseButton(true)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(10)
			[
				SNew(SVerticalBox)

				// Title
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 10)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AssetCreationSettings", "Asset Creation Settings"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
				]

				// Settings Details View
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SettingsDetailsView.IsValid() ? SettingsDetailsView->GetWidget().ToSharedRef() : SNullWidget::NullWidget
				]
			]
		];

	SettingsWindow = NewWindow;

	FSlateApplication::Get().AddWindow(NewWindow);

	return FReply::Handled();
}

void SStageEditorPanelV2::CloseSettingsWindow()
{
	if (TSharedPtr<SWindow> Window = SettingsWindow.Pin())
	{
		Window->RequestDestroyWindow();
		SettingsWindow.Reset();
	}
}

//----------------------------------------------------------------
// Registry Helpers
//----------------------------------------------------------------

FReply SStageEditorPanelV2::OnCreateRegistryClicked()
{
	// Use the Registry creation dialog
	ECollaborationMode SelectedMode = ECollaborationMode::Solo;
	if (SRegistryCreationDialog::ShowDialog(SelectedMode))
	{
		// Dialog was confirmed, Registry creation is handled by the dialog
		// Invalidate cache to pick up new registry
		CachedRegistry = nullptr;
		CachedRegistryLevelPath.Empty();
	}
	return FReply::Handled();
}

FReply SStageEditorPanelV2::OnSelectExistingRegistryClicked()
{
	if (!GEditor)
	{
		return FReply::Handled();
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		DebugHeader::ShowNotifyInfo(TEXT("No active World"));
		return FReply::Handled();
	}

	// Open Asset Picker dialog for StageRegistryAsset
	FAssetPickerConfig PickerConfig;
	PickerConfig.Filter.ClassPaths.Add(UStageRegistryAsset::StaticClass()->GetClassPathName());
	PickerConfig.SelectionMode = ESelectionMode::Single;
	PickerConfig.bAllowNullSelection = false;
	PickerConfig.bFocusSearchBoxWhenOpened = true;
	PickerConfig.InitialAssetViewType = EAssetViewType::List;

	// Callback when asset is selected
	PickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda(
		[this, World](const FAssetData& AssetData)
		{
			if (AssetData.IsValid())
			{
				FString RegistryPath = AssetData.GetSoftObjectPath().ToString();

				// Get Subsystem and set manual association
				UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
				if (EditorSubsystem)
				{
					EditorSubsystem->SetManualRegistryAssociation(World, RegistryPath);

					// Invalidate cache
					CachedRegistry = nullptr;
					CachedRegistryLevelPath.Empty();

					DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Registry associated: %s"), *RegistryPath));
				}
			}

			// Close the picker menu
			FSlateApplication::Get().DismissAllMenus();
		});

	// Create and show the asset picker in a menu
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TSharedRef<SWidget> AssetPicker = ContentBrowserModule.Get().CreateAssetPicker(PickerConfig);

	// Show as popup menu
	FSlateApplication::Get().PushMenu(
		AsShared(),
		FWidgetPath(),
		SNew(SBox)
		.WidthOverride(400)
		.HeightOverride(500)
		[
			AssetPicker
		],
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect(FPopupTransitionEffect::TypeInPopup)
	);

	return FReply::Handled();
}

FReply SStageEditorPanelV2::OnConvertToWorldPartitionClicked()
{
	// Open the World Partition conversion dialog
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.GetLevelEditorTabManager()->TryInvokeTab(FName("WorldPartitionEditor"));

	return FReply::Handled();
}

FReply SStageEditorPanelV2::OnRefreshWorldPartitionStatusClicked()
{
	CheckAndRefreshWorldPartitionStatus();
	return FReply::Handled();
}

bool SStageEditorPanelV2::IsWorldPartitionLevel() const
{
	if (!GEditor)
	{
		return false;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World || !World->PersistentLevel)
	{
		return false;
	}

	return World->PersistentLevel->bIsPartitioned;
}

bool SStageEditorPanelV2::HasRegistryAsset() const
{
	return GetCachedRegistry() != nullptr;
}

UStageRegistryAsset* SStageEditorPanelV2::GetCachedRegistry() const
{
	if (!GEditor)
	{
		return nullptr;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return nullptr;
	}

	// Check if cache is still valid
	FString CurrentLevelPath = World->GetPathName();
	if (CachedRegistryLevelPath != CurrentLevelPath || !CachedRegistry.IsValid())
	{
		// Cache is stale, refresh
		CachedRegistryLevelPath = CurrentLevelPath;
		CachedRegistry = nullptr;

		// Find Registry via StageEditorSubsystem
		UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
		if (EditorSubsystem)
		{
			CachedRegistry = EditorSubsystem->GetOrLoadRegistryAsset(World);
		}
	}

	return CachedRegistry.Get();
}

EVisibility SStageEditorPanelV2::GetRegistryWarningVisibility() const
{
	return HasRegistryAsset() ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SStageEditorPanelV2::GetRegistryPathText() const
{
	if (UStageRegistryAsset* Registry = GetCachedRegistry())
	{
		return FText::FromString(Registry->GetPathName());
	}
	return LOCTEXT("NoRegistry", "None");
}

//----------------------------------------------------------------
// Viewport Selection Sync
//----------------------------------------------------------------

void SStageEditorPanelV2::RegisterViewportSelectionListener()
{
	if (GEditor)
	{
		USelection* Selection = GEditor->GetSelectedActors();
		if (Selection)
		{
			ActorSelectionPtr = Selection;
			ViewportSelectionDelegateHandle = Selection->SelectionChangedEvent.AddSP(
				this, &SStageEditorPanelV2::HandleViewportSelectionChanged);
		}
	}
}

void SStageEditorPanelV2::UnregisterViewportSelectionListener()
{
	if (USelection* Selection = ActorSelectionPtr.Get())
	{
		Selection->SelectionChangedEvent.Remove(ViewportSelectionDelegateHandle);
	}
	ViewportSelectionDelegateHandle.Reset();
	ActorSelectionPtr.Reset();
}

void SStageEditorPanelV2::HandleViewportSelectionChanged(UObject* SelectedObject)
{
	if (bUpdatingViewportSelectionFromPanel)
	{
		return; // Prevent recursion
	}

	bUpdatingTreeSelectionFromViewport = true;

	// Sync selection to outliner
	if (AActor* Actor = Cast<AActor>(SelectedObject))
	{
		if (AStage* Stage = Cast<AStage>(Actor))
		{
			SetSelection(Stage);
		}
		else
		{
			SetSelection(Actor);
		}
	}

	bUpdatingTreeSelectionFromViewport = false;
}

//----------------------------------------------------------------
// Event Handlers
//----------------------------------------------------------------

void SStageEditorPanelV2::OnMapOpened(const FString& Filename, bool bAsTemplate)
{
	// Re-register viewport selection listener for new map
	UnregisterViewportSelectionListener();
	RegisterViewportSelectionListener();

	// Invalidate registry cache
	CachedRegistry = nullptr;
	CachedRegistryLevelPath.Empty();

	// Check WP status and rebuild if needed
	CheckAndRefreshWorldPartitionStatus();
}

void SStageEditorPanelV2::OnModelChanged()
{
	RefreshUI();
}

void SStageEditorPanelV2::OnAssetCreationSettingsChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	// Update Controller's DataLayer path if changed
	if (Controller.IsValid() && CreationSettings.IsValid())
	{
		FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
		FString DataLayerPath = Settings->bIsCustomDataLayerAssetPath ?
			Settings->DataLayerAssetFolderPath.Path : TEXT("/StageEditor/DataLayers");
		Controller->SetDataLayerAssetFolderPath(DataLayerPath);
	}
}

void SStageEditorPanelV2::CheckAndRefreshWorldPartitionStatus()
{
	bool bCurrentIsWorldPartition = IsWorldPartitionLevel();
	if (bCurrentIsWorldPartition != bCachedIsWorldPartition)
	{
		RebuildUI();
	}
}

//----------------------------------------------------------------
// Multi-User Status (Phase 4)
//----------------------------------------------------------------

TSharedRef<SWidget> SStageEditorPanelV2::CreateLockStatusBar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(0.2f, 0.4f, 0.8f, 0.8f))  // Blue info
		.Padding(8)
		.Visibility(this, &SStageEditorPanelV2::GetLockStatusBarVisibility)
		[
			SNew(SHorizontalBox)

			// Lock Icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Lock"))
			]

			// Lock Status Text
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SStageEditorPanelV2::GetLockStatusText)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
			]

			// View Changelist Button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(10, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("ViewChangelist", "View Changelist"))
				.ToolTipText(LOCTEXT("ViewChangelist_Tooltip", "Open the Source Control Changelist panel to review and submit pending changes"))
				.OnClicked(this, &SStageEditorPanelV2::OnViewChangelistClicked)
			]

			// Manual Refresh Button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("RefreshLockStatus_Tooltip",
					"Refresh Source Control Status\n"
					"Check if other users have locked this Registry."))
				.OnClicked(this, &SStageEditorPanelV2::OnRefreshLockStatusClicked)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Refresh"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		];
}

TSharedRef<SWidget> SStageEditorPanelV2::CreateSyncWarningBanner()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(1.0f, 0.5f, 0.0f, 0.8f))  // Orange warning
		.Padding(8)
		.Visibility(this, &SStageEditorPanelV2::GetSyncWarningVisibility)
		[
			SNew(SHorizontalBox)

			// Warning Icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Warning"))
				.ColorAndOpacity(FLinearColor(1.0f, 0.6f, 0.0f))
			]

			// Sync Status Text
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SStageEditorPanelV2::GetSyncStatusText)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.AutoWrapText(true)
			]

			// Dynamic Sync/Reconcile Button
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(10, 0, 0, 0)
			[
				SNew(SButton)
				.Text(this, &SStageEditorPanelV2::GetSyncButtonText)
				.ToolTipText(this, &SStageEditorPanelV2::GetSyncButtonTooltip)
				.OnClicked(this, &SStageEditorPanelV2::OnSyncRegistryClicked)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
			]
		];
}

EVisibility SStageEditorPanelV2::GetLockStatusBarVisibility() const
{
	return CachedLockStatusBarVisibility;
}

FText SStageEditorPanelV2::GetLockStatusText() const
{
	return CachedLockStatusText;
}

EVisibility SStageEditorPanelV2::GetSyncWarningVisibility() const
{
	return CachedSyncWarningVisibility;
}

FText SStageEditorPanelV2::GetSyncStatusText() const
{
	return CachedSyncStatusText;
}

FText SStageEditorPanelV2::GetSyncButtonText() const
{
	if (!Controller.IsValid())
	{
		return LOCTEXT("SyncRegistry", "Sync Registry");
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UStageRegistryAsset* Registry = GetCachedRegistry();
	if (!World || !Registry)
	{
		return LOCTEXT("SyncRegistry", "Sync Registry");
	}

	FRegistrySyncStatus SyncStatus = Controller->CalculateSyncStatus(World, Registry);
	int32 ReconcileCount = SyncStatus.GetPendingReconciliationCount();
	if (ReconcileCount > 0)
	{
		return FText::Format(LOCTEXT("ReconcileStages", "Reconcile {0} Stage(s)"), ReconcileCount);
	}

	return LOCTEXT("SyncRegistry", "Sync Registry");
}

FText SStageEditorPanelV2::GetSyncButtonTooltip() const
{
	if (!Controller.IsValid())
	{
		return LOCTEXT("SyncRegistry_Tooltip", "Assign IDs to pending Stages and remove orphaned Registry entries");
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UStageRegistryAsset* Registry = GetCachedRegistry();
	if (!World || !Registry)
	{
		return LOCTEXT("SyncRegistry_Tooltip", "Assign IDs to pending Stages and remove orphaned Registry entries");
	}

	FRegistrySyncStatus SyncStatus = Controller->CalculateSyncStatus(World, Registry);
	int32 ReconcileCount = SyncStatus.GetPendingReconciliationCount();
	if (ReconcileCount > 0)
	{
		return LOCTEXT("ReconcileStages_Tooltip", "Convert temporary IDs to real IDs for offline-created Stages");
	}

	return LOCTEXT("SyncRegistry_Tooltip", "Assign IDs to pending Stages and remove orphaned Registry entries");
}

FReply SStageEditorPanelV2::OnViewChangelistClicked()
{
	// Open the Source Control Changelist window
	FGlobalTabmanager::Get()->TryInvokeTab(FName("SourceControlChangelists"));
	return FReply::Handled();
}

FReply SStageEditorPanelV2::OnRefreshLockStatusClicked()
{
	RefreshStatusCaches();
	return FReply::Handled();
}

FReply SStageEditorPanelV2::OnSyncRegistryClicked()
{
	if (!Controller.IsValid())
	{
		return FReply::Handled();
	}

	// Check if we need to prioritize reconciliation
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World)
	{
		UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
		if (EditorSubsystem)
		{
			UStageRegistryAsset* Registry = EditorSubsystem->GetOrLoadRegistryAsset(World);
			if (Registry)
			{
				FRegistrySyncStatus SyncStatus = Controller->CalculateSyncStatus(World, Registry);
				if (SyncStatus.GetPendingReconciliationCount() > 0)
				{
					// Priority: Call ReconcilePendingStages() first
					Controller->ReconcilePendingStages();
					RefreshStatusCaches();
					RefreshUI();
					return FReply::Handled();
				}
			}
		}
	}

	// Default: Call SyncRegistry()
	if (Controller->SyncRegistry())
	{
		RefreshStatusCaches();
		RefreshUI();
	}

	return FReply::Handled();
}

void SStageEditorPanelV2::RefreshStatusCaches()
{
	// Reset to hidden by default
	CachedLockStatusBarVisibility = EVisibility::Collapsed;
	CachedLockStatusText = FText::GetEmpty();
	CachedSyncWarningVisibility = EVisibility::Collapsed;
	CachedSyncStatusText = FText::GetEmpty();
	CachedDeletedIDsCount = 0;
	CachedDuplicateIDsCount = 0;

	if (!GEditor)
	{
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return;
	}

	UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (!EditorSubsystem)
	{
		return;
	}

	UStageRegistryAsset* Registry = GetCachedRegistry();
	if (!Registry)
	{
		return;
	}

	// Check if Multi-user mode (Source Control enabled and Registry exists)
	bool bIsMultiUserMode = EditorSubsystem->IsSourceControlEnabled() && Registry->CollaborationMode == ECollaborationMode::Multi;

	if (bIsMultiUserMode)
	{
		// Lock Status Bar - show when in Multi-user mode
		CachedLockStatusBarVisibility = EVisibility::Visible;

		// Get lock status from Source Control (Phase 18.1: Use cached version for UI performance)
		FRegistryLockInfo LockInfo = EditorSubsystem->GetCachedRegistryLockInfo(Registry);
		if (LockInfo.bIsCheckedOutByMe)
		{
			CachedLockStatusText = LOCTEXT("RegistryCheckedOut", "Registry is checked out by you. Remember to submit when done.");
		}
		else if (LockInfo.bIsCheckedOutByOther)
		{
			CachedLockStatusText = FText::Format(LOCTEXT("RegistryLockedByOther", "Registry is locked by: {0}"), FText::FromString(LockInfo.OtherUserName));
		}
		else
		{
			CachedLockStatusText = LOCTEXT("RegistryUnlocked", "Registry is available. Click 'View Changelist' to check out.");
		}

		// Sync Status - check if out of sync
		if (Controller.IsValid())
		{
			FRegistrySyncStatus SyncStatus = Controller->CalculateSyncStatus(World, Registry);
			if (!SyncStatus.IsSynced())
			{
				CachedSyncWarningVisibility = EVisibility::Visible;

				int32 PendingAssignment = SyncStatus.PendingAssignment.Num();
				int32 PendingRemoval = SyncStatus.PendingRemoval.Num();
				int32 PendingReconcile = SyncStatus.GetPendingReconciliationCount();

				if (PendingReconcile > 0)
				{
					CachedSyncStatusText = FText::Format(
						LOCTEXT("SyncStatus_Reconcile", "Found {0} Stage(s) created offline. Click 'Reconcile' to assign real IDs."),
						PendingReconcile);
				}
				else if (PendingAssignment > 0 && PendingRemoval > 0)
				{
					CachedSyncStatusText = FText::Format(
						LOCTEXT("SyncStatus_Both", "Registry out of sync: {0} pending assignment, {1} pending removal."),
						PendingAssignment, PendingRemoval);
				}
				else if (PendingAssignment > 0)
				{
					CachedSyncStatusText = FText::Format(
						LOCTEXT("SyncStatus_Assignment", "Found {0} Stage(s) without IDs. Click 'Sync Registry' to assign."),
						PendingAssignment);
				}
				else if (PendingRemoval > 0)
				{
					CachedSyncStatusText = FText::Format(
						LOCTEXT("SyncStatus_Removal", "Found {0} orphaned Registry entries. Click 'Sync Registry' to clean up."),
						PendingRemoval);
				}
			}
		}
	}

	// Update Recycle/Repair counts (for Phase 7 buttons)
	TArray<int32> GapIDs;
	CachedDeletedIDsCount = Registry->DetectIDGaps(GapIDs);
	// CachedDuplicateIDsCount would require scanning - skip for now
}

//----------------------------------------------------------------
// Recycle/Repair IDs (Phase 7)
//----------------------------------------------------------------

FReply SStageEditorPanelV2::OnRecycleIDsClicked()
{
	if (!GEditor)
	{
		return FReply::Handled();
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (!EditorSubsystem || !World)
	{
		return FReply::Handled();
	}

	UStageRegistryAsset* Registry = EditorSubsystem->GetOrLoadRegistryAsset(World);
	if (!Registry)
	{
		return FReply::Handled();
	}

	int32 RecycledCount = Registry->RecycleIDGaps();
	if (RecycledCount > 0)
	{
		Registry->MarkPackageDirty();
		DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Recycled %d deleted ID(s)"), RecycledCount));
		RefreshStatusCaches();
	}
	else
	{
		DebugHeader::ShowNotifyInfo(TEXT("No deleted IDs to recycle"));
	}

	return FReply::Handled();
}

EVisibility SStageEditorPanelV2::GetRecycleIDsButtonVisibility() const
{
	return (CachedDeletedIDsCount > 0) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SStageEditorPanelV2::GetRecycleIDsButtonText() const
{
	if (CachedDeletedIDsCount > 0)
	{
		return FText::Format(LOCTEXT("RecycleIDs_Count", "Recycle {0} ID(s)"), CachedDeletedIDsCount);
	}
	return LOCTEXT("RecycleIDs", "Recycle IDs");
}

FText SStageEditorPanelV2::GetRecycleIDsButtonTooltip() const
{
	return LOCTEXT("RecycleIDs_Tooltip", "Recycle unused StageIDs for reuse when creating new Stages");
}

FReply SStageEditorPanelV2::OnRepairDuplicateIDsClicked()
{
	// TODO: Implement duplicate ID repair
	DebugHeader::ShowNotifyInfo(TEXT("Repair Duplicate IDs not yet implemented in V2"));
	return FReply::Handled();
}

EVisibility SStageEditorPanelV2::GetRepairDuplicateIDsButtonVisibility() const
{
	return (CachedDuplicateIDsCount > 0) ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SStageEditorPanelV2::GetRepairDuplicateIDsButtonText() const
{
	return LOCTEXT("RepairDuplicateIDs", "Repair Duplicate IDs");
}

FText SStageEditorPanelV2::GetRepairDuplicateIDsButtonTooltip() const
{
	return LOCTEXT("RepairDuplicateIDs_Tooltip", "Detect and repair duplicate StageIDs");
}

#undef LOCTEXT_NAMESPACE
