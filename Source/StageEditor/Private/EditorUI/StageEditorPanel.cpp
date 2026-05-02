#include "EditorUI/StageEditorPanel.h"

#include <DebugHeader.h>

// Helper modules
#include <Logging/StageEditorLog.h>

#include "EditorUI/StageEditorStateManager.h"
#include "EditorUI/StageEditorTreeBuilder.h"
#include "EditorUI/StageEditorActionHandlers.h"
#include "EditorUI/StageEditorDragDropHandler.h"

#include "Actors/Stage.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Input/Events.h"
#include "IStructureDetailsView.h"
#include "Engine/Selection.h"
#include "DragAndDrop/ActorDragDropOp.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Templates/UnrealTemplate.h"
#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "DesktopPlatformModule.h"
#include "Widgets/Input/SCheckBox.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "UObject/ObjectSaveContext.h"
#include "Subsystems/StageEditorSubsystem.h"
#include "Data/StageRegistryAsset.h"
#include "DataLayerSync/StageMigrationAnalyzer.h"
#include "DataLayerSync/SStageMigrationDialog.h"
#include "DataLayerSync/SRegistryCreationDialog.h"

#define LOCTEXT_NAMESPACE "SStageEditorPanel"

#pragma endregion Imports

#pragma region Helper Functions
/**
 * Helper to get the StageManagerSubsystem from PIE or Game world.
 * Used for Debug HUD watch feature in Editor Panel.
 */
static UStageManagerSubsystem* GetStageManagerSubsystemForWatch()
{
	if (!GEngine) return nullptr;

	// Try to find the game world (PIE or standalone)
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
		{
			if (UWorld* World = Context.World())
			{
				return World->GetSubsystem<UStageManagerSubsystem>();
			}
		}
	}
	return nullptr;
}
#pragma endregion Helper Functions

#pragma region Construction

void SStageEditorPanel::Construct(const FArguments& InArgs, TSharedPtr<FStageEditorController> InController)
{
	Controller = InController;

	// Create Creation Settings struct FIRST (before ActionHandlers needs it)
	CreationSettings = MakeShared<FStructOnScope>(FAssetCreationSettings::StaticStruct());
	FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
	*Settings = FAssetCreationSettings(); // Initialize with defaults

	// Load saved settings from config (overrides defaults)
	LoadAssetCreationSettingsFromConfig();

	// Create Details View for creation settings
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FStructureDetailsViewArgs StructureViewArgs;
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.bHideSelectionTip = true;
	SettingsDetailsView = PropertyModule.CreateStructureDetailView(DetailsViewArgs, StructureViewArgs, CreationSettings);

	// Bind to property changes to update Controller's DataLayer path
	if (SettingsDetailsView.IsValid())
	{
		SettingsDetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &SStageEditorPanel::OnAssetCreationSettingsChanged);
	}

	// Initialize helper modules (ActionHandlers now receives valid CreationSettings)
	StateManager = MakeShared<FStageEditorStateManager>(Controller);
	TreeBuilder = MakeShared<FStageEditorTreeBuilder>(Controller);
	ActionHandlers = MakeShared<FStageEditorActionHandlers>(Controller, SharedThis(this), CreationSettings);
	DragDropHandler = MakeShared<FStageEditorDragDropHandler>(Controller, SharedThis(this));

	// Phase 18: Lazy-load status texts (Path 3: Initial load)
	// Refresh all status caches on first open to avoid empty state
	if (StateManager.IsValid())
	{
		StateManager->RefreshAllStatusTexts();
	}

	// Bind to Controller updates
	if (Controller.IsValid())
	{
		Controller->OnModelChanged.AddSP(this, &SStageEditorPanel::RefreshUI);
	}

	// Initialize Controller's DataLayer asset folder path
	if (Controller.IsValid())
	{
		FString DataLayerPath = Settings->bIsCustomDataLayerAssetPath ?
			Settings->DataLayerAssetFolderPath.Path : TEXT("/StageEditor/DataLayers");
		Controller->SetDataLayerAssetFolderPath(DataLayerPath);
	}

	// Check if World Partition is enabled
	const bool bIsWorldPartition = IsWorldPartitionLevel();
	bCachedIsWorldPartition = bIsWorldPartition;

	// Register for map change events (only once)
	if (!MapChangedHandle.IsValid())
	{
		MapChangedHandle = FEditorDelegates::OnMapOpened.AddSP(this, &SStageEditorPanel::OnMapOpened);
	}

	// Register for post-save world events to detect WorldPartition changes
	if (!PostSaveWorldHandle.IsValid())
	{
		PostSaveWorldHandle = FEditorDelegates::PostSaveWorldWithContext.AddSP(this, &SStageEditorPanel::OnPostSaveWorld);
	}

	// Phase 21: Register for PIE end events to clear stale TWeakObjectPtr references in tree items.
	// After PIE ends, UE reconstructs Blueprint instances in the editor world, invalidating any
	// weak pointers held in RootTreeItems. Without this, the panel shows garbled/stale data.
	if (!EndPIEHandle.IsValid())
	{
		EndPIEHandle = FEditorDelegates::EndPIE.AddSP(this, &SStageEditorPanel::OnEndPIE);
	}

	// Register for stage data changed events (Import/Sync operations)
	if (!StageDataChangedHandle.IsValid())
	{
		if (UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
		{
			if (UStageManagerSubsystem* Subsystem = World->GetSubsystem<UStageManagerSubsystem>())
			{
				StageDataChangedHandle = Subsystem->OnStageDataChanged.AddSP(this, &SStageEditorPanel::OnStageDataChanged);
			}
		}
	}

	// Phase 18: Register for Source Control state changed events (Submit/Sync operations)
	// When user submits Registry via View Changelist, Subsystem broadcasts this event
	if (!SourceControlStateChangedHandle.IsValid())
	{
		if (UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>())
		{
			SourceControlStateChangedHandle = EditorSubsystem->OnSourceControlStateChanged.AddSP(this, &SStageEditorPanel::OnSourceControlStateChanged);
		}
	}

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
						.Text(LOCTEXT("WPRequired_Title", "⚠ World Partition Required"))
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
							"• Dynamic resource streaming via DataLayers\n"
							"• Efficient memory management for large worlds\n"
							"• Proper Act-based scene state management\n\n"
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
						.OnClicked(this, &SStageEditorPanel::OnConvertToWorldPartitionClicked)
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
						.ToolTipText(LOCTEXT("RefreshWPStatus_Tooltip", "Refresh World Partition status check (use after conversion completes)"))
						.OnClicked(this, &SStageEditorPanel::OnRefreshWorldPartitionStatusClicked)
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
		// Normal StageEditor UI - wrapped in SOverlay for Registry Required overlay
		ChildSlot
		[
			SNew(SOverlay)

			// Slot 0: Normal UI content
			+ SOverlay::Slot()
			[
				SNew(SVerticalBox)

				// Toolbar
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(5)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("RegisterEntities", "Register Selected Entities"))
						.OnClicked(this, &SStageEditorPanel::OnRegisterSelectedEntitiesClicked)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(5, 0, 0, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("Refresh", "Refresh"))
						.OnClicked(this, &SStageEditorPanel::OnRefreshClicked)
						.ToolTipText(LOCTEXT("Refresh_Tooltip", "Manually scan for Stages in the level"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(5, 0, 0, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("CleanOrphanedEntities", "Clean Orphaned"))
						.OnClicked(this, &SStageEditorPanel::OnCleanOrphanedEntitiesClicked)
						.ToolTipText(LOCTEXT("CleanOrphanedEntities_Tooltip", "Clean orphaned Entities (Entities whose owner Stage was deleted)"))
					]
				]

				// Quick Create Toolbar
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(5, 0, 5, 5)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("CreateStageBP", "Create Stage BP"))
						.OnClicked(this, &SStageEditorPanel::OnCreateStageBPClicked)
						.ToolTipText(LOCTEXT("CreateStageBP_Tooltip", "Create a new Stage Blueprint in Content Browser"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(5, 0, 0, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("CreateEntityActorBP", "Create Entity Actor BP"))
						.OnClicked(this, &SStageEditorPanel::OnCreateEntityActorBPClicked)
						.ToolTipText(LOCTEXT("CreateEntityActorBP_Tooltip", "Create a new Entity Actor Blueprint in Content Browser"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(5, 0, 0, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("CreateEntityComponentBP", "Create Entity Component BP"))
						.OnClicked(this, &SStageEditorPanel::OnCreateEntityComponentBPClicked)
						.ToolTipText(LOCTEXT("CreateEntityComponentBP_Tooltip", "Create a new Entity Component Blueprint in Content Browser"))
					]

					// Settings Gear Button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(10, 0, 0, 0)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.OnClicked(this, &SStageEditorPanel::OnOpenSettingsClicked)
						.ToolTipText(LOCTEXT("OpenSettings_Tooltip", "Open Asset Creation Settings"))
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Settings"))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]

					// Phase 13.10: Recycle IDs Button (manual ID recycling)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(10, 0, 0, 0)
					[
						SNew(SButton)
						.Text(this, &SStageEditorPanel::GetRecycleIDsButtonText)
						.ToolTipText(this, &SStageEditorPanel::GetRecycleIDsButtonTooltip)
						.OnClicked(this, &SStageEditorPanel::OnRecycleIDsClicked)
						.Visibility(this, &SStageEditorPanel::GetRecycleIDsButtonVisibility)
					]

					// Phase 13.10: Repair Duplicate IDs Button (for fixing duplicate StageID issues)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(10, 0, 0, 0)
					[
						SNew(SButton)
						.Text(this, &SStageEditorPanel::GetRepairDuplicateIDsButtonText)
						.ToolTipText(this, &SStageEditorPanel::GetRepairDuplicateIDsButtonTooltip)
						.OnClicked(this, &SStageEditorPanel::OnRepairDuplicateIDsClicked)
						.Visibility(this, &SStageEditorPanel::GetRepairDuplicateIDsButtonVisibility)
					]
				]

				// Registry Info Banner (shows which Registry is being used)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(5, 0, 5, 5)
				[
					SNew(SBorder)
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
							.Text(this, &SStageEditorPanel::GetRegistryPathText)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							.ColorAndOpacity(FLinearColor(0.8f, 0.9f, 1.0f))  // Light blue text
						]
					]
				]

				// Phase 13.5: Lock Status Bar (Multi-user mode only)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 2, 0, 2)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(FLinearColor(0.2f, 0.4f, 0.8f, 0.8f))  // Blue info
					.Padding(8)
					.Visibility(this, &SStageEditorPanel::GetLockStatusBarVisibility)
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
							.Text(this, &SStageEditorPanel::GetLockStatusText)
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
							.OnClicked(this, &SStageEditorPanel::OnViewChangelistClicked)
						]

						// Phase 18: Manual Refresh Button (Performance Fix)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4, 0, 0, 0)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ToolTipText(LOCTEXT("RefreshLockStatus_Tooltip",
								"Refresh Source Control Status\n"
								"Check if other users have locked this Registry.\n\n"
								"Note: Status is automatically updated after CheckOut/Revert operations."))
							.OnClicked(this, &SStageEditorPanel::OnRefreshLockStatusClicked)
							[
								SNew(SImage)
								.Image(FAppStyle::GetBrush("Icons.Refresh"))
								.ColorAndOpacity(FSlateColor::UseForeground())
							]
						]
					]
				]

				// Phase 13.5: Sync Warning Banner (Multi-user mode, when out of sync)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 2, 0, 2)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.BorderBackgroundColor(FLinearColor(1.0f, 0.5f, 0.0f, 0.8f))  // Orange warning
					.Padding(8)
					.Visibility(this, &SStageEditorPanel::GetSyncWarningVisibility)
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
							.Text(this, &SStageEditorPanel::GetSyncStatusText)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
							.AutoWrapText(true)
						]

						// Phase 13.9: Dynamic Sync/Reconcile Button
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(10, 0, 0, 0)
						[
							SNew(SButton)
							.Text(this, &SStageEditorPanel::GetSyncButtonText)
							.ToolTipText(this, &SStageEditorPanel::GetSyncButtonTooltip)
							.OnClicked(this, &SStageEditorPanel::OnSyncRegistryClicked)
							.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
						]
					]
				]

				// Tree View with Header Row
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SBorder)
					.Padding(2)
					[
						SAssignNew(StageTreeView, STreeView<TSharedPtr<FStageTreeItem>>)
						.TreeItemsSource(&RootTreeItems)
						.SelectionMode(ESelectionMode::Multi)  // Enable Ctrl/Shift multi-select
						.OnGenerateRow(this, &SStageEditorPanel::OnGenerateRow)
						.OnGetChildren(this, &SStageEditorPanel::OnGetChildren)
						.OnSelectionChanged(this, &SStageEditorPanel::OnSelectionChanged)
						.OnContextMenuOpening(this, &SStageEditorPanel::OnContextMenuOpening)
						.OnMouseButtonDoubleClick(this, &SStageEditorPanel::OnRowDoubleClicked)
						.HeaderRow
						(
							SAssignNew(HeaderRow, SHeaderRow)
							+ SHeaderRow::Column(StageTreeColumns::Watch)
							.DefaultLabel(FText::GetEmpty())  // Icon only in header
							.ManualWidth(24.0f)
							.HeaderContentPadding(FMargin(2, 0, 0, 0))
							.HeaderContent()
							[
								SNew(SImage)
								.Image(FAppStyle::GetBrush("Level.VisibleIcon16x"))
								.ToolTipText(LOCTEXT("WatchColumnHeader_Tooltip", "Toggle Stage watch state for Debug HUD"))
							]

							+ SHeaderRow::Column(StageTreeColumns::ID)
							.DefaultLabel(LOCTEXT("SUIDColumn", "SUID"))
							.ManualWidth(150.0f)
							.HeaderContentPadding(FMargin(4, 0, 0, 0))

							+ SHeaderRow::Column(StageTreeColumns::Name)
							.DefaultLabel(LOCTEXT("NameColumn", "Name"))
							.ManualWidth(200.0f)
							.HeaderContentPadding(FMargin(4, 0, 0, 0))

							+ SHeaderRow::Column(StageTreeColumns::Actions)
							.DefaultLabel(LOCTEXT("ActionsColumn", "Actions"))
							.ManualWidth(220.0f)
							.HeaderContentPadding(FMargin(4, 0, 0, 0))
						)
					]
				]
			]  // End SVerticalBox (Normal UI content)

			// Slot 1: Registry Required Overlay (shown when Registry is missing)
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.BorderBackgroundColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f))  // Semi-transparent dark
				.Visibility(this, &SStageEditorPanel::GetRegistryWarningVisibility)
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.BorderBackgroundColor(FLinearColor(0.15f, 0.15f, 0.15f, 1.0f))
						.Padding(30)
						[
							SNew(SVerticalBox)

							// Warning Icon
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0, 0, 0, 15)
							[
								SNew(SImage)
								.Image(FAppStyle::GetBrush("Icons.Warning"))
								.ColorAndOpacity(FLinearColor(1.0f, 0.6f, 0.0f))
								.DesiredSizeOverride(FVector2D(48, 48))
							]

							// Title
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0, 0, 0, 10)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("RegistryRequired_Title", "Stage Registry Required"))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
								.ColorAndOpacity(FLinearColor(1.0f, 0.8f, 0.2f))
							]

							// Description
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0, 0, 0, 20)
							[
								SNew(STextBlock)
								.Text(LOCTEXT("RegistryRequired_Desc",
									"A Stage Registry is required to use the Stage Editor.\n"
									"The Registry stores Stage IDs and metadata for persistence."))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
								.Justification(ETextJustify::Center)
								.AutoWrapText(true)
							]

							// Create Registry Button
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							[
								SNew(SButton)
								.Text(LOCTEXT("CreateRegistryLarge", "Create Registry"))
								.ToolTipText(LOCTEXT("CreateRegistryLarge_Tooltip", "Create a StageRegistryAsset to enable Stage Editor functionality"))
								.OnClicked(this, &SStageEditorPanel::OnCreateRegistryClicked)
								.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
								.ContentPadding(FMargin(20, 8))
							]

							// Select Existing Registry Button
							+ SVerticalBox::Slot()
							.AutoHeight()
							.HAlign(HAlign_Center)
							.Padding(0, 10, 0, 0)
							[
								SNew(SButton)
								.Text(LOCTEXT("SelectExistingRegistryLarge", "Select Existing Registry"))
								.ToolTipText(LOCTEXT("SelectExistingRegistryLarge_Tooltip", "Browse and select an existing StageRegistryAsset (e.g., after exporting plugin to new project)"))
								.OnClicked(this, &SStageEditorPanel::OnSelectExistingRegistryClicked)
								.ContentPadding(FMargin(20, 8))
							]
						]
					]
				]
			]  // End Registry Required Overlay
		];  // End SOverlay
	}

	RefreshUI();

	// ❌ Disabled: We want one-way sync (Panel → Viewport only)
	// Viewport selection should NOT affect Panel selection
	// RegisterViewportSelectionListener();

	// Initialize Controller (scan for level actors) AFTER UI is built
	if (Controller.IsValid())
	{
		Controller->Initialize();
	}
}

SStageEditorPanel::~SStageEditorPanel()
{
	// Unregister map changed delegate
	if (MapChangedHandle.IsValid())
	{
		FEditorDelegates::OnMapOpened.Remove(MapChangedHandle);
		MapChangedHandle.Reset();
	}

	// Unregister post-save world delegate
	if (PostSaveWorldHandle.IsValid())
	{
		FEditorDelegates::PostSaveWorldWithContext.Remove(PostSaveWorldHandle);
		PostSaveWorldHandle.Reset();
	}

	// Unregister stage data changed delegate
	if (StageDataChangedHandle.IsValid())
	{
		if (UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
		{
			if (UStageManagerSubsystem* Subsystem = World->GetSubsystem<UStageManagerSubsystem>())
			{
				Subsystem->OnStageDataChanged.Remove(StageDataChangedHandle);
			}
		}
		StageDataChangedHandle.Reset();
	}

	// Phase 18: Unregister Source Control state changed delegate
	if (SourceControlStateChangedHandle.IsValid())
	{
		if (UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>())
		{
			EditorSubsystem->OnSourceControlStateChanged.Remove(SourceControlStateChangedHandle);
		}
		SourceControlStateChangedHandle.Reset();
	}

	// Phase 21: Unregister PIE end delegate
	if (EndPIEHandle.IsValid())
	{
		FEditorDelegates::EndPIE.Remove(EndPIEHandle);
		EndPIEHandle.Reset();
	}

	// ❌ Disabled: Viewport listener was not registered
	// UnregisterViewportSelectionListener();
}

void SStageEditorPanel::OnMapOpened(const FString& Filename, bool bAsTemplate)
{
	UE_LOG(LogStageEditor, Log, TEXT("OnMapOpened: Map changed to '%s', invalidating caches"), *Filename);

	// Invalidate all StateManager caches (Registry, status texts, etc.)
	if (StateManager.IsValid())
	{
		StateManager->InvalidateAllCaches();
	}

	// Refresh World Partition status
	CheckAndRefreshWorldPartitionStatus();

	// Rebuild entire UI with new level's data
	RebuildUI();
}

void SStageEditorPanel::OnPostSaveWorld(UWorld* World, FObjectPostSaveContext ObjectSaveContext)
{
	// After saving, check if WorldPartition status changed
	// (conversion to WP requires save)
	CheckAndRefreshWorldPartitionStatus();
}

void SStageEditorPanel::OnStageDataChanged(AStage* Stage)
{
	// Refresh UI when Stage data changes (after Import/Sync operations)
	// This ensures the StageEditorPanel reflects the latest state
	if (Controller.IsValid())
	{
		// Rescan the world for stages in case new ones were created
		Controller->FindStageInWorld();
	}
	RefreshUI();
}

void SStageEditorPanel::OnSourceControlStateChanged()
{
	// Phase 18: Refresh SC status caches when Source Control state changes
	// When user submits Registry via View Changelist, this callback is triggered
	// and we refresh Lock Status and Sync Status text caches
	if (StateManager.IsValid())
	{
		StateManager->RefreshAllStatusTexts();
		UE_LOG(LogStageEditor, Verbose, TEXT("OnSourceControlStateChanged: Refreshed SC status caches"));
	}
}

void SStageEditorPanel::OnEndPIE(bool bIsSimulating)
{
	// Phase 21: After PIE ends, Blueprint instances in the editor world are reconstructed.
	// All TWeakObjectPtr in RootTreeItems (ActorPtr, StagePtr) become stale.
	// Rescan the world and rebuild the tree to restore valid references.
	UE_LOG(LogStageEditor, Log, TEXT("OnEndPIE: PIE ended, refreshing StageEditorPanel to clear stale references"));

	if (Controller.IsValid())
	{
		Controller->FindStageInWorld();
	}
	RefreshUI();
}

void SStageEditorPanel::CheckAndRefreshWorldPartitionStatus()
{
	const bool bNewIsWorldPartition = IsWorldPartitionLevel();

	if (bNewIsWorldPartition != bCachedIsWorldPartition)
	{
		UE_LOG(LogTemp, Log, TEXT("StageEditorPanel: WorldPartition status changed from %s to %s"),
			bCachedIsWorldPartition ? TEXT("true") : TEXT("false"),
			bNewIsWorldPartition ? TEXT("true") : TEXT("false"));

		bCachedIsWorldPartition = bNewIsWorldPartition;
		RebuildUI();

		// Show notification to user
		if (bNewIsWorldPartition)
		{
			DebugHeader::ShowNotifyInfo(TEXT("World Partition detected! Stage Editor is now available."));
		}
	}
	else
	{
		// Same state, just refresh the data
		RefreshUI();
	}
}

void SStageEditorPanel::RebuildUI()
{
	// Clear current content and rebuild the entire UI
	// This is needed when switching between WP/non-WP states
	ChildSlot.DetachWidget();

	// Re-run construction logic by calling Construct again
	// We need to preserve the Controller reference
	TSharedPtr<FStageEditorController> SavedController = Controller;

	// Build new arguments
	FArguments Args;

	// Re-construct
	Construct(Args, SavedController);

	UE_LOG(LogTemp, Log, TEXT("StageEditorPanel: UI rebuilt. WorldPartition=%s"),
		bCachedIsWorldPartition ? TEXT("true") : TEXT("false"));
}


#pragma endregion Construction

#pragma region Core API

void SStageEditorPanel::RefreshUI()
{
	if (!Controller.IsValid()) return;

	// If StageTreeView doesn't exist (e.g., in non-World Partition level), skip refresh
	if (!StageTreeView.IsValid())
	{
		return;
	}

	// Save Expansion State via StateManager
	TSet<FString> ExpansionState;
	StateManager->SaveExpansionState(RootTreeItems, ExpansionState, StageTreeView);

	// Build tree data via TreeBuilder
	TreeBuilder->RebuildTreeItems(RootTreeItems, ActorPathToTreeItem);

	StageTreeView->RequestTreeRefresh();

	// Restore Expansion State via StateManager
	StateManager->RestoreExpansionState(RootTreeItems, ExpansionState, StageTreeView);

	// Phase 18 Path 1: Auto-refresh status caches after model changes
	// When user modifies Registry (creates Act, registers Entity, etc.),
	// Controller broadcasts OnModelChanged → RefreshUI() → this refreshes SC status
	if (StateManager.IsValid())
	{
		StateManager->RefreshAllStatusTexts();
	}

	// HandleViewportSelectionChanged(nullptr);
}
#pragma endregion Core API

#pragma region SStageTreeRow

/**
 * Custom multi-column table row for Stage tree items
 * Supports resizable columns via SHeaderRow integration
 */
class SStageTreeRow : public SMultiColumnTableRow<TSharedPtr<FStageTreeItem>>
{
public:
	SLATE_BEGIN_ARGS(SStageTreeRow) {}
		SLATE_ARGUMENT(TSharedPtr<FStageTreeItem>, Item)
		SLATE_ARGUMENT(SStageEditorPanel*, OwnerPanel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
	{
		Item = InArgs._Item;
		OwnerPanel = InArgs._OwnerPanel;

		// Cache parent info
		ParentItem = Item.IsValid() ? Item->Parent.Pin() : nullptr;
		bParentIsAct = ParentItem.IsValid() && ParentItem->Type == EStageTreeItemType::Act;
		bParentIsEntitiesFolder = ParentItem.IsValid() && ParentItem->Type == EStageTreeItemType::EntitiesFolder;
		bIsFolder = Item.IsValid() && (Item->Type == EStageTreeItemType::ActsFolder || Item->Type == EStageTreeItemType::EntitiesFolder);

		// Determine icon
		DetermineIcon();

		// Build ID and Name text
		BuildDisplayText();

		SMultiColumnTableRow<TSharedPtr<FStageTreeItem>>::Construct(
			FSuperRowType::FArguments()
			.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.AlternatingRow"))
			.OnDrop_Lambda([this](const FDragDropEvent& DragDropEvent) -> FReply
			{
				if (OwnerPanel)
				{
					return OwnerPanel->OnRowDrop(FGeometry(), DragDropEvent, Item);
				}
				return FReply::Unhandled();
			})
			.OnDragEnter_Lambda([this](const FDragDropEvent& DragDropEvent)
			{
				if (OwnerPanel)
				{
					OwnerPanel->OnRowDragEnter(DragDropEvent, Item);
					// Request refresh to update highlight (lighter than RebuildList)
					if (OwnerPanel->StageTreeView.IsValid())
					{
						OwnerPanel->StageTreeView->RequestListRefresh();
					}
				}
			})
			.OnDragLeave_Lambda([this](const FDragDropEvent& DragDropEvent)
			{
				if (OwnerPanel)
				{
					OwnerPanel->OnRowDragLeave(DragDropEvent, Item);
					// Request refresh when leaving to clear highlight
					if (OwnerPanel->StageTreeView.IsValid())
					{
						OwnerPanel->StageTreeView->RequestListRefresh();
					}
				}
			}),
			InOwnerTable
		);
	}

	/**
	 * Override OnDragDetected to support dragging Entities from Registered Entities folder
	 * Only draggable items return Handled, others use default TreeView behavior
	 */
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		// Delegate to DragDropHandler if available
		if (OwnerPanel && OwnerPanel->DragDropHandler.IsValid())
		{
			return OwnerPanel->DragDropHandler->OnRowDragDetected(MyGeometry, MouseEvent, Item);
		}

		// Fallback to parent behavior
		return SMultiColumnTableRow<TSharedPtr<FStageTreeItem>>::OnDragDetected(MyGeometry, MouseEvent);
	}

	/** Check if this row is currently a drag target */
	bool IsDragTarget() const
	{
		if (!OwnerPanel || !Item.IsValid()) return false;
		if (!OwnerPanel->DragDropHandler.IsValid()) return false;
		return OwnerPanel->DragDropHandler->IsDragTarget(Item);
	}

	/** Override to provide drag highlight effect similar to hover */
	virtual const FSlateBrush* GetBorder() const override
	{
		// Check if this row should be highlighted as drag target
		if (IsDragTarget())
		{
			// Use the same brush as hover state from the style
			const FTableRowStyle* RowStyle = &FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.AlternatingRow");
			return &RowStyle->ActiveHoveredBrush;
		}

		// Default behavior from parent class
		return SMultiColumnTableRow<TSharedPtr<FStageTreeItem>>::GetBorder();
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		if (InColumnName == StageTreeColumns::Watch)
		{
			return GenerateWatchColumnWidget();
		}
		else if (InColumnName == StageTreeColumns::ID)
		{
			return GenerateIDColumnWidget();
		}
		else if (InColumnName == StageTreeColumns::Name)
		{
			return GenerateNameColumnWidget();
		}
		else if (InColumnName == StageTreeColumns::Actions)
		{
			return GenerateActionsColumnWidget();
		}

		return SNullWidget::NullWidget;
	}

private:
	TSharedPtr<FStageTreeItem> Item;
	SStageEditorPanel* OwnerPanel = nullptr;
	TSharedPtr<FStageTreeItem> ParentItem;
	bool bParentIsAct = false;
	bool bParentIsEntitiesFolder = false;
	bool bIsFolder = false;

	const FSlateBrush* IconBrush = nullptr;
	FSlateColor IconColor = FSlateColor::UseForeground();
	FString IDText;
	FString NameText;

	void DetermineIcon()
	{
		if (!Item.IsValid()) return;

		switch (Item->Type)
		{
		case EStageTreeItemType::Stage:
			IconBrush = FAppStyle::GetBrush("LevelEditor.Tabs.Levels");
			IconColor = FSlateColor(FLinearColor(0.3f, 0.6f, 1.0f)); // Blue
			break;
		case EStageTreeItemType::ActsFolder:
			IconBrush = FAppStyle::GetBrush("ContentBrowser.AssetTreeFolderClosed");
			IconColor = FSlateColor(FLinearColor(0.5f, 1.0f, 0.5f)); // Green (matching Acts)
			break;
		case EStageTreeItemType::EntitiesFolder:
			IconBrush = FAppStyle::GetBrush("ContentBrowser.AssetTreeFolderClosed");
			IconColor = FSlateColor(FLinearColor(1.0f, 0.6f, 0.3f)); // Orange (matching Entities)
			break;
		case EStageTreeItemType::Act:
			IconBrush = FAppStyle::GetBrush("Sequencer.Tracks.Event");
			IconColor = FSlateColor(FLinearColor(0.5f, 1.0f, 0.5f)); // Green
			break;
		case EStageTreeItemType::Entity:
			IconBrush = FAppStyle::GetBrush("ClassIcon.StaticMeshActor");
			IconColor = FSlateColor(FLinearColor(1.0f, 0.6f, 0.3f)); // Orange
			break;
		}
	}

	void BuildDisplayText()
	{
		if (!Item.IsValid()) return;

		// Helper to get StageID from ancestor
		auto GetStageID = [this]() -> int32
		{
			if (Item->StagePtr.IsValid())
			{
				return Item->StagePtr->GetStageID();
			}
			// Walk up parent chain to find Stage
			TSharedPtr<FStageTreeItem> Current = Item->Parent.Pin();
			while (Current.IsValid())
			{
				if (Current->Type == EStageTreeItemType::Stage && Current->StagePtr.IsValid())
				{
					return Current->StagePtr->GetStageID();
				}
				Current = Current->Parent.Pin();
			}
			return 0;
		};

		switch (Item->Type)
		{
		case EStageTreeItemType::Stage:
			// Display: S_StageID.0.0
			IDText = FString::Printf(TEXT("S_%d.0.0"), Item->ID);
			NameText = Item->StagePtr.IsValid() ? Item->StagePtr->GetActorLabel() : TEXT("");

			// Phase 13.9: Show pending status for offline-created Stages
			if (Item->StagePtr.IsValid() && Item->ID < 0)
			{
				NameText += TEXT(" ⚠️ Pending Sync");
			}
			break;
		case EStageTreeItemType::Act:
			// Display: A_StageID.ActID.0 (ActID starts from 1, Default Act = 1)
			IDText = FString::Printf(TEXT("A_%d.%d.0"), GetStageID(), Item->ID);
			NameText = Item->DisplayName;
			break;
		case EStageTreeItemType::Entity:
			// Display: E_StageID.0.EntityID
			IDText = FString::Printf(TEXT("E_%d.0.%d"), GetStageID(), Item->ID);
			NameText = Item->ActorPtr.IsValid() ? Item->ActorPtr->GetActorLabel() : TEXT("Invalid");
			break;
		case EStageTreeItemType::ActsFolder:
		case EStageTreeItemType::EntitiesFolder:
			// Folders: show name in SUID column, Name column empty
			IDText = Item->DisplayName;
			NameText = TEXT("");
			break;
		default:
			IDText = TEXT("");
			NameText = Item->DisplayName;
			break;
		}
	}

	TSharedRef<SWidget> GenerateWatchColumnWidget()
	{
		// Only show watch toggle for Stage rows
		if (!Item.IsValid() || Item->Type != EStageTreeItemType::Stage || !Item->StagePtr.IsValid())
		{
			return SNullWidget::NullWidget;
		}

		// Get Stage pointer for this row
		TWeakObjectPtr<AStage> WeakStage = Item->StagePtr;
		int32 StageID = Item->ID;

		// Create eye icon button
		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.WidthOverride(20.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.ToolTipText_Lambda([WeakStage, StageID]()
				{
					// Check PIE first
					UStageManagerSubsystem* Subsystem = GetStageManagerSubsystemForWatch();
					if (Subsystem)
					{
						bool bIsWatched = Subsystem->IsStageWatched(StageID);
						return bIsWatched
							? LOCTEXT("WatchButton_Unwatch_PIE", "Click to stop watching (PIE active)")
							: LOCTEXT("WatchButton_Watch_PIE", "Click to watch in Debug HUD (PIE active)");
					}

					// Editor mode - show editor watch state
					if (AStage* Stage = WeakStage.Get())
					{
						return Stage->bEditorWatched
							? LOCTEXT("WatchButton_Unwatch_Editor", "Click to unmark for Debug HUD (will sync when PIE starts)")
							: LOCTEXT("WatchButton_Watch_Editor", "Click to mark for Debug HUD (will sync when PIE starts)");
					}

					return LOCTEXT("WatchButton_Invalid", "Stage not available");
				})
				.OnClicked_Lambda([WeakStage, StageID]()
				{
					// Check PIE first
					UStageManagerSubsystem* Subsystem = GetStageManagerSubsystemForWatch();
					if (Subsystem)
					{
						// PIE mode - toggle in Subsystem AND sync back to Stage
						if (Subsystem->IsStageWatched(StageID))
						{
							Subsystem->UnwatchStage(StageID);
							if (AStage* Stage = WeakStage.Get())
							{
								Stage->bEditorWatched = false;
							}
						}
						else
						{
							Subsystem->WatchStage(StageID);
							if (AStage* Stage = WeakStage.Get())
							{
								Stage->bEditorWatched = true;
							}
						}
					}
					else
					{
						// Editor mode - toggle bEditorWatched
						if (AStage* Stage = WeakStage.Get())
						{
							Stage->Modify();  // Support Undo
							Stage->bEditorWatched = !Stage->bEditorWatched;
						}
					}
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image_Lambda([WeakStage, StageID]() -> const FSlateBrush*
					{
						// Check PIE first
						UStageManagerSubsystem* Subsystem = GetStageManagerSubsystemForWatch();
						if (Subsystem)
						{
							bool bIsWatched = Subsystem->IsStageWatched(StageID);
							return bIsWatched
								? FAppStyle::GetBrush("Level.VisibleIcon16x")
								: FAppStyle::GetBrush("Level.NotVisibleIcon16x");
						}

						// Editor mode - use bEditorWatched
						if (AStage* Stage = WeakStage.Get())
						{
							return Stage->bEditorWatched
								? FAppStyle::GetBrush("Level.VisibleIcon16x")
								: FAppStyle::GetBrush("Level.NotVisibleIcon16x");
						}

						return FAppStyle::GetBrush("Level.NotVisibleIcon16x");
					})
					.ColorAndOpacity_Lambda([WeakStage, StageID]() -> FSlateColor
					{
						// Check PIE first
						UStageManagerSubsystem* Subsystem = GetStageManagerSubsystemForWatch();
						if (Subsystem)
						{
							bool bIsWatched = Subsystem->IsStageWatched(StageID);
							return bIsWatched
								? FSlateColor(FLinearColor(0.2f, 0.8f, 0.2f))  // Green when watched
								: FSlateColor::UseForeground();
						}

						// Editor mode - use bEditorWatched with different color
						if (AStage* Stage = WeakStage.Get())
						{
							return Stage->bEditorWatched
								? FSlateColor(FLinearColor(0.5f, 0.7f, 1.0f))  // Light blue for editor preset
								: FSlateColor::UseForeground();
						}

						return FSlateColor(FLinearColor(0.3f, 0.3f, 0.3f, 0.5f));
					})
				]
			];
	}

	TSharedRef<SWidget> GenerateIDColumnWidget()
	{
		TSharedRef<SHorizontalBox> ColumnContent = SNew(SHorizontalBox);

		// Expander Arrow (for tree hierarchy)
		ColumnContent->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SExpanderArrow, SharedThis(this))
			.IndentAmount(16)
		];

		// Icon
		ColumnContent->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2, 0, 4, 0)
		[
			SNew(SImage)
			.Image(IconBrush)
			.ColorAndOpacity(IconColor)
		];

		// ID Text (or folder name for folders)
		ColumnContent->AddSlot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(IDText))
			.Font(bIsFolder ? FCoreStyle::GetDefaultFontStyle("Bold", 10) : FCoreStyle::GetDefaultFontStyle("Regular", 10))
		];

		return ColumnContent;
	}

	TSharedRef<SWidget> GenerateNameColumnWidget()
	{
		TSharedRef<SHorizontalBox> ColumnContent = SNew(SHorizontalBox);

		// Name Text (also used for folder display names)
		ColumnContent->AddSlot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(4, 0, 0, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(NameText))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
			.ColorAndOpacity(bIsFolder
				? FSlateColor::UseForeground()  // Folders use normal color
				: FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f))) // Others slightly dimmed
		];

		// EntityState inline edit - only for Entities under Act
		if (Item.IsValid() && Item->Type == EStageTreeItemType::Entity && bParentIsAct && Item->bHasEntityState)
		{
			TSharedPtr<FStageTreeItem> CapturedItem = Item;
			TSharedPtr<FStageTreeItem> CapturedParent = ParentItem;
			SStageEditorPanel* CapturedPanel = OwnerPanel;

			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("State:")))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 0, 0, 0)
				[
					SNew(SNumericEntryBox<int32>)
					.Value(Item->EntityState)
					.AllowSpin(false)
					.MinDesiredValueWidth(40.0f)
					.ToolTipText(LOCTEXT("EntityStateInlineEdit_Tooltip", "Adjust the Entity state applied within this Act"))
					.OnValueCommitted_Lambda([CapturedPanel, CapturedItem, CapturedParent](int32 NewValue, ETextCommit::Type)
					{
						if (CapturedPanel)
						{
							CapturedPanel->ApplyEntityStateChange(CapturedItem, CapturedParent, NewValue);
						}
					})
				]
			];
		}

		return ColumnContent;
	}

	/** Recursively sets expansion state for an item and all its children */
	void SetExpansionRecursive(TSharedPtr<FStageTreeItem> InItem, bool bExpand)
	{
		if (!InItem.IsValid() || !OwnerPanel || !OwnerPanel->StageTreeView.IsValid()) return;

		OwnerPanel->StageTreeView->SetItemExpansion(InItem, bExpand);
		for (const TSharedPtr<FStageTreeItem>& Child : InItem->Children)
		{
			SetExpansionRecursive(Child, bExpand);
		}
	}

	TSharedRef<SWidget> GenerateActionsColumnWidget()
	{
		TSharedRef<SHorizontalBox> ColumnContent = SNew(SHorizontalBox);

		if (!Item.IsValid() || !OwnerPanel) return ColumnContent;

		TSharedPtr<FStageTreeItem> CapturedItem = Item;
		TSharedPtr<FStageTreeItem> CapturedParent = ParentItem;
		SStageEditorPanel* CapturedPanel = OwnerPanel;

		// Expand All / Collapse All buttons for Stage items
		if (Item->Type == EStageTreeItemType::Stage)
		{
			// Expand All button
			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("ExpandAllStage_Tooltip", "Expand all children of this Stage"))
				.OnClicked_Lambda([this, CapturedItem]()
				{
					SetExpansionRecursive(CapturedItem, true);
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("TreeArrow_Expanded"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];

			// Collapse All button
			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("CollapseAllStage_Tooltip", "Collapse all children of this Stage"))
				.OnClicked_Lambda([this, CapturedItem]()
				{
					SetExpansionRecursive(CapturedItem, false);
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("TreeArrow_Collapsed"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
		}

		// Create Act button for Acts folder
		if (Item->Type == EStageTreeItemType::ActsFolder)
		{
			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("CreateActInline_Tooltip", "Create a new Act in this Stage"))
				.OnClicked_Lambda([CapturedPanel, CapturedItem]()
				{
					if (CapturedPanel && CapturedPanel->Controller.IsValid() && CapturedItem->Parent.IsValid())
					{
						TSharedPtr<FStageTreeItem> ParentStage = CapturedItem->Parent.Pin();
						if (ParentStage.IsValid() && ParentStage->StagePtr.IsValid())
						{
							CapturedPanel->Controller->SetActiveStage(ParentStage->StagePtr.Get());
							CapturedPanel->Controller->CreateNewAct();
						}
					}
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Plus"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
		}

		// Browse to Asset and Edit BP buttons for Stage items
		if (Item->Type == EStageTreeItemType::Stage && Item->StagePtr.IsValid())
		{
			// Browse to Asset button
			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("BrowseToStageBP_Tooltip", "Browse to Stage Blueprint in Content Browser"))
				.OnClicked_Lambda([CapturedItem]()
				{
					if (CapturedItem->StagePtr.IsValid())
					{
						if (UBlueprint* Blueprint = Cast<UBlueprint>(CapturedItem->StagePtr->GetClass()->ClassGeneratedBy))
						{
							TArray<UObject*> Assets;
							Assets.Add(Blueprint);
							GEditor->SyncBrowserToObjects(Assets);
						}
					}
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.BrowseContent"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];

			// Edit BP button
			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("EditStageBP_Tooltip", "Edit Stage Blueprint"))
				.OnClicked_Lambda([CapturedItem]()
				{
					if (CapturedItem->StagePtr.IsValid())
					{
						if (UBlueprint* Blueprint = Cast<UBlueprint>(CapturedItem->StagePtr->GetClass()->ClassGeneratedBy))
						{
							GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
						}
					}
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Edit"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];

			// Delete Stage button
			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("DeleteStage_Tooltip", "Delete this Stage (with confirmation)"))
				.OnClicked_Lambda([CapturedPanel, CapturedItem]()
				{
					if (CapturedPanel && CapturedPanel->Controller.IsValid() && CapturedItem->StagePtr.IsValid())
					{
						CapturedPanel->Controller->DeleteStageWithConfirmation(CapturedItem->StagePtr.Get());
					}
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Delete"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
		}

		// Browse to Asset and Edit BP buttons for Entity items
		if (Item->Type == EStageTreeItemType::Entity && Item->ActorPtr.IsValid())
		{
			// Browse to Asset button
			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("BrowseToEntityBP_Tooltip", "Browse to Entity Blueprint in Content Browser"))
				.OnClicked_Lambda([CapturedItem]()
				{
					if (CapturedItem->ActorPtr.IsValid())
					{
						if (UBlueprint* Blueprint = Cast<UBlueprint>(CapturedItem->ActorPtr->GetClass()->ClassGeneratedBy))
						{
							TArray<UObject*> Assets;
							Assets.Add(Blueprint);
							GEditor->SyncBrowserToObjects(Assets);
						}
					}
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.BrowseContent"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];

			// Edit BP button
			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("EditEntityBP_Tooltip", "Edit Entity Blueprint"))
				.OnClicked_Lambda([CapturedItem]()
				{
					if (CapturedItem->ActorPtr.IsValid())
					{
						if (UBlueprint* Blueprint = Cast<UBlueprint>(CapturedItem->ActorPtr->GetClass()->ClassGeneratedBy))
						{
							GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
						}
					}
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Edit"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
		}

		// FollowStageState checkbox and InitialDataLayerState dropdown for Act items
		if (Item->Type == EStageTreeItemType::Act)
		{
			// Helper lambda to find parent Stage
			auto FindParentStage = [CapturedItem]() -> AStage*
			{
				TSharedPtr<FStageTreeItem> Current = CapturedItem->Parent.Pin();
				while (Current.IsValid())
				{
					if (Current->Type == EStageTreeItemType::Stage && Current->StagePtr.IsValid())
					{
						return Current->StagePtr.Get();
					}
					Current = Current->Parent.Pin();
				}
				return nullptr;
			};

			// Get current values for this Act
			int32 ActID = Item->ID;
			bool bFollowStageState = false;
			EDataLayerRuntimeState CurrentState = EDataLayerRuntimeState::Unloaded;
			if (AStage* Stage = FindParentStage())
			{
				for (const FAct& Act : Stage->Acts)
				{
					if (Act.SUID.ActID == ActID)
					{
						bFollowStageState = Act.bFollowStageState;
						CurrentState = Act.InitialDataLayerState;
						break;
					}
				}
			}

			// FollowStageState checkbox
			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([CapturedItem]() -> ECheckBoxState
				{
					// Find parent Stage and get current bFollowStageState
					AStage* Stage = nullptr;
					TSharedPtr<FStageTreeItem> Current = CapturedItem.IsValid() ? CapturedItem->Parent.Pin() : nullptr;
					while (Current.IsValid())
					{
						if (Current->Type == EStageTreeItemType::Stage && Current->StagePtr.IsValid())
						{
							Stage = Current->StagePtr.Get();
							break;
						}
						Current = Current->Parent.Pin();
					}

					if (!Stage || !CapturedItem.IsValid()) return ECheckBoxState::Unchecked;

					int32 ActID = CapturedItem->ID;
					for (const FAct& Act : Stage->Acts)
					{
						if (Act.SUID.ActID == ActID)
						{
							return Act.bFollowStageState ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}
					}
					return ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([CapturedItem, CapturedPanel](ECheckBoxState NewState)
				{
					if (!CapturedItem.IsValid() || !CapturedPanel) return;

					// Find parent Stage
					AStage* Stage = nullptr;
					TSharedPtr<FStageTreeItem> Current = CapturedItem->Parent.Pin();
					while (Current.IsValid())
					{
						if (Current->Type == EStageTreeItemType::Stage && Current->StagePtr.IsValid())
						{
							Stage = Current->StagePtr.Get();
							break;
						}
						Current = Current->Parent.Pin();
					}

					if (!Stage) return;

					// Find and modify the Act
					int32 ActID = CapturedItem->ID;
					bool bNewFollowState = (NewState == ECheckBoxState::Checked);
					for (FAct& Act : Stage->Acts)
					{
						if (Act.SUID.ActID == ActID)
						{
							if (Act.bFollowStageState != bNewFollowState)
							{
								FScopedTransaction Transaction(LOCTEXT("SetActFollowStageState", "Set Act Follow Stage State"));
								Stage->Modify();
								Act.bFollowStageState = bNewFollowState;

								// Notify Controller to broadcast model change (triggers UI refresh)
								if (CapturedPanel->Controller.IsValid())
								{
									CapturedPanel->Controller->OnModelChanged.Broadcast();
								}
							}
							break;
						}
					}
				})
				.ToolTipText(LOCTEXT("FollowStageState_Tooltip", "When checked, this Act's DataLayer state mirrors the Stage's state (Stage Loaded → Act Loaded, Stage Active → Act Active)"))
			];

			// "Follow" label
			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FollowLabel", "Follow"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity_Lambda([CapturedItem]() -> FSlateColor
				{
					// Dim the text if Follow is unchecked
					AStage* Stage = nullptr;
					TSharedPtr<FStageTreeItem> Current = CapturedItem.IsValid() ? CapturedItem->Parent.Pin() : nullptr;
					while (Current.IsValid())
					{
						if (Current->Type == EStageTreeItemType::Stage && Current->StagePtr.IsValid())
						{
							Stage = Current->StagePtr.Get();
							break;
						}
						Current = Current->Parent.Pin();
					}

					if (!Stage || !CapturedItem.IsValid()) return FSlateColor::UseSubduedForeground();

					int32 ActID = CapturedItem->ID;
					for (const FAct& Act : Stage->Acts)
					{
						if (Act.SUID.ActID == ActID)
						{
							return Act.bFollowStageState ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground();
						}
					}
					return FSlateColor::UseSubduedForeground();
				})
			];

			// Build state options list
			static TArray<TSharedPtr<FText>> DataLayerStateOptions;
			if (DataLayerStateOptions.Num() == 0)
			{
				DataLayerStateOptions.Add(MakeShared<FText>(LOCTEXT("DLS_Unloaded", "Unloaded")));
				DataLayerStateOptions.Add(MakeShared<FText>(LOCTEXT("DLS_Loaded", "Loaded")));
				DataLayerStateOptions.Add(MakeShared<FText>(LOCTEXT("DLS_Activated", "Activated")));
			}

			// Convert current state to option index
			int32 CurrentIndex = 0;
			switch (CurrentState)
			{
			case EDataLayerRuntimeState::Unloaded: CurrentIndex = 0; break;
			case EDataLayerRuntimeState::Loaded: CurrentIndex = 1; break;
			case EDataLayerRuntimeState::Activated: CurrentIndex = 2; break;
			default: CurrentIndex = 0; break;
			}

			// InitialDataLayerState dropdown (disabled when FollowStageState is checked)
			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 0, 0)
			[
				SNew(SBox)
				.WidthOverride(80.0f)
				.IsEnabled_Lambda([CapturedItem]() -> bool
				{
					// Disable dropdown when bFollowStageState is checked
					AStage* Stage = nullptr;
					TSharedPtr<FStageTreeItem> Current = CapturedItem.IsValid() ? CapturedItem->Parent.Pin() : nullptr;
					while (Current.IsValid())
					{
						if (Current->Type == EStageTreeItemType::Stage && Current->StagePtr.IsValid())
						{
							Stage = Current->StagePtr.Get();
							break;
						}
						Current = Current->Parent.Pin();
					}

					if (!Stage || !CapturedItem.IsValid()) return true;

					int32 ActID = CapturedItem->ID;
					for (const FAct& Act : Stage->Acts)
					{
						if (Act.SUID.ActID == ActID)
						{
							return !Act.bFollowStageState; // Disable if following
						}
					}
					return true;
				})
				[
					SNew(SComboBox<TSharedPtr<FText>>)
					.OptionsSource(&DataLayerStateOptions)
					.InitiallySelectedItem(DataLayerStateOptions[CurrentIndex])
					.ToolTipText(LOCTEXT("ActInitialDataLayerState_Tooltip", "Initial DataLayer state when Stage becomes Active (only used when 'Follow' is unchecked)"))
					.OnGenerateWidget_Lambda([](TSharedPtr<FText> InOption)
					{
						return SNew(STextBlock)
							.Text(*InOption)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9));
					})
					.OnSelectionChanged_Lambda([CapturedItem, CapturedPanel](TSharedPtr<FText> NewSelection, ESelectInfo::Type SelectInfo)
					{
						if (SelectInfo == ESelectInfo::Direct || !NewSelection.IsValid() || !CapturedItem.IsValid())
						{
							return;
						}

						// Find parent Stage
						AStage* Stage = nullptr;
						TSharedPtr<FStageTreeItem> Current = CapturedItem->Parent.Pin();
						while (Current.IsValid())
						{
							if (Current->Type == EStageTreeItemType::Stage && Current->StagePtr.IsValid())
							{
								Stage = Current->StagePtr.Get();
								break;
							}
							Current = Current->Parent.Pin();
						}

						if (!Stage) return;

						// Determine new state from selection
						EDataLayerRuntimeState NewState = EDataLayerRuntimeState::Unloaded;
						FString SelectionText = NewSelection->ToString();
						if (SelectionText.Contains(TEXT("Activated")))
						{
							NewState = EDataLayerRuntimeState::Activated;
						}
						else if (SelectionText.Contains(TEXT("Unloaded")))
						{
							// Check Unloaded BEFORE Loaded because "Unloaded" contains "Loaded"
							NewState = EDataLayerRuntimeState::Unloaded;
						}
						else if (SelectionText.Contains(TEXT("Loaded")))
						{
							NewState = EDataLayerRuntimeState::Loaded;
						}

						// Find and modify the Act
						int32 ActID = CapturedItem->ID;
						for (FAct& Act : Stage->Acts)
						{
							if (Act.SUID.ActID == ActID)
							{
								if (Act.InitialDataLayerState != NewState)
								{
									FScopedTransaction Transaction(LOCTEXT("SetActInitialDataLayerState", "Set Act Initial DataLayer State"));
									Stage->Modify();
									Act.InitialDataLayerState = NewState;
								}
								break;
							}
						}
					})
					[
						SNew(STextBlock)
						.Text_Lambda([CapturedItem]() -> FText
						{
							// Find parent Stage and get current state
							AStage* Stage = nullptr;
							TSharedPtr<FStageTreeItem> Current = CapturedItem.IsValid() ? CapturedItem->Parent.Pin() : nullptr;
							while (Current.IsValid())
							{
								if (Current->Type == EStageTreeItemType::Stage && Current->StagePtr.IsValid())
								{
									Stage = Current->StagePtr.Get();
									break;
								}
								Current = Current->Parent.Pin();
							}

							if (!Stage || !CapturedItem.IsValid()) return LOCTEXT("DLS_Unknown", "?");

							int32 ActID = CapturedItem->ID;
							for (const FAct& Act : Stage->Acts)
							{
								if (Act.SUID.ActID == ActID)
								{
									// Show "(Follow)" if following Stage state
									if (Act.bFollowStageState)
									{
										return LOCTEXT("DLS_Follow", "(Follow)");
									}
									switch (Act.InitialDataLayerState)
									{
									case EDataLayerRuntimeState::Unloaded: return LOCTEXT("DLS_U", "Unloaded");
									case EDataLayerRuntimeState::Loaded: return LOCTEXT("DLS_L", "Loaded");
									case EDataLayerRuntimeState::Activated: return LOCTEXT("DLS_A", "Activated");
									default: return LOCTEXT("DLS_Unknown2", "?");
									}
								}
							}
							return LOCTEXT("DLS_NotFound", "?");
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					]
				]
			];
		}

		// Delete button for Act items (except Default Act)
		if (Item->Type == EStageTreeItemType::Act && Item->ID != 0)
		{
			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("DeleteActInline_Tooltip", "Delete this Act"))
				.OnClicked_Lambda([CapturedPanel, CapturedItem]()
				{
					if (CapturedPanel && CapturedPanel->Controller.IsValid())
					{
						if (TSharedPtr<FStageTreeItem> StageItem = CapturedPanel->FindStageAncestor(CapturedItem))
						{
							if (StageItem->StagePtr.IsValid())
							{
								CapturedPanel->Controller->SetActiveStage(StageItem->StagePtr.Get());

								// Confirmation dialog
								FText ConfirmTitle = LOCTEXT("ConfirmDeleteAct", "Confirm Delete Act");
								FText ConfirmMessage = FText::Format(
									LOCTEXT("ConfirmDeleteActMsg", "Are you sure you want to delete Act '{0}'?"),
									FText::FromString(CapturedItem->DisplayName)
								);

								if (CapturedPanel->ShowConfirmDialog(ConfirmTitle, ConfirmMessage))
								{
									CapturedPanel->Controller->DeleteAct(CapturedItem->ID);
								}
							}
						}
					}
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Delete"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
		}

		// Delete button for Entity items
		if (Item->Type == EStageTreeItemType::Entity && (bParentIsAct || bParentIsEntitiesFolder))
		{
			FText TooltipText = bParentIsAct
				? LOCTEXT("RemoveEntityFromActInline_Tooltip", "Remove this Entity from the Act")
				: LOCTEXT("UnregisterEntityInline_Tooltip", "Unregister this Entity from the Stage");

			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(TooltipText)
				.OnClicked_Lambda([CapturedPanel, CapturedItem, CapturedParent, bIsAct = bParentIsAct]()
				{
					if (CapturedPanel && CapturedPanel->Controller.IsValid())
					{
						TSharedPtr<FStageTreeItem> StartItem = bIsAct ? CapturedParent : CapturedItem;
						if (TSharedPtr<FStageTreeItem> StageItem = CapturedPanel->FindStageAncestor(StartItem))
						{
							if (StageItem->StagePtr.IsValid())
							{
								CapturedPanel->Controller->SetActiveStage(StageItem->StagePtr.Get());

								if (bIsAct)
								{
									CapturedPanel->Controller->RemoveEntityFromAct(CapturedItem->ID, CapturedParent->ID);
								}
								else
								{
									FText ConfirmTitle = LOCTEXT("ConfirmUnregisterEntityInline", "Confirm Unregister");
									FText ConfirmMessage = FText::Format(
										LOCTEXT("ConfirmUnregisterEntityInlineMsg", "Are you sure you want to unregister '{0}' from the Stage?\n\nThis will remove it from all Acts."),
										FText::FromString(CapturedItem->DisplayName)
									);

									if (CapturedPanel->ShowConfirmDialog(ConfirmTitle, ConfirmMessage))
									{
										CapturedPanel->Controller->UnregisterAllEntities(CapturedItem->ID);
									}
								}
							}
						}
					}
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Delete"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
		}

		return ColumnContent;
	}
};

#pragma endregion SStageTreeRow

#pragma region Callbacks

TSharedRef<ITableRow> SStageEditorPanel::OnGenerateRow(TSharedPtr<FStageTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SStageTreeRow, OwnerTable)
		.Item(Item)
		.OwnerPanel(this);
}

void SStageEditorPanel::OnGetChildren(TSharedPtr<FStageTreeItem> Item, TArray<TSharedPtr<FStageTreeItem>>& OutChildren)
{
	OutChildren = Item->Children;
}

void SStageEditorPanel::OnSelectionChanged(TSharedPtr<FStageTreeItem> Item, ESelectInfo::Type SelectInfo)
{
	if (bUpdatingTreeSelectionFromViewport || !Item.IsValid() || !Controller.IsValid())
	{
		return;
	}

	// IMPORTANT: Do NOT call any Controller methods here that would trigger OnModelChanged!
	// OnModelChanged -> RefreshUI -> Tree rebuild -> Selection lost
	//
	// All Controller operations (SetActiveStage, PreviewAct, etc.) are deferred to:
	// - OnRowDoubleClicked: for viewport sync and act preview
	// - Context menu actions: for explicit operations
	// - Button clicks: for explicit operations
	//
	// Single-click only selects the tree item without side effects.
}

void SStageEditorPanel::OnRowDoubleClicked(TSharedPtr<FStageTreeItem> Item)
{
	if (!Item.IsValid() || !Controller.IsValid()) return;

	// Set active stage and perform type-specific actions
	AActor* ActorToFocus = nullptr;

	if (Item->Type == EStageTreeItemType::Stage && Item->StagePtr.IsValid())
	{
		Controller->SetActiveStage(Item->StagePtr.Get());
		ActorToFocus = Item->StagePtr.Get();
	}
	else if (Item->Type == EStageTreeItemType::Act)
	{
		// Set active stage first
		if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(Item))
		{
			if (StageItem->StagePtr.IsValid())
			{
				Controller->SetActiveStage(StageItem->StagePtr.Get());

				// Navigate Content Browser to the Act's DataLayer asset
				if (AStage* Stage = StageItem->StagePtr.Get())
				{
					for (const FAct& Act : Stage->Acts)
					{
						if (Act.SUID.ActID == Item->ID && Act.AssociatedDataLayer)
						{
							TArray<UObject*> Assets = { const_cast<UDataLayerAsset*>(Act.AssociatedDataLayer.Get()) };
							GEditor->SyncBrowserToObjects(Assets);
							break;
						}
					}
				}
			}
		}
		// Preview the Act (activates DataLayer etc.)
		Controller->PreviewAct(Item->ID);
	}
	else if (Item->Type == EStageTreeItemType::Entity && Item->ActorPtr.IsValid())
	{
		// Set active stage first
		if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(Item))
		{
			if (StageItem->StagePtr.IsValid())
			{
				Controller->SetActiveStage(StageItem->StagePtr.Get());
			}
		}
		ActorToFocus = Item->ActorPtr.Get();
	}
	else if (Item->Type == EStageTreeItemType::ActsFolder || Item->Type == EStageTreeItemType::EntitiesFolder)
	{
		// Set active stage for folder items
		if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(Item))
		{
			if (StageItem->StagePtr.IsValid())
			{
				Controller->SetActiveStage(StageItem->StagePtr.Get());
			}
		}
	}

	// Focus viewport on actor if applicable
	if (ActorToFocus && GEditor)
	{
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(ActorToFocus, true, true);
		GEditor->MoveViewportCamerasToActor(*ActorToFocus, false);
	}
}

FReply SStageEditorPanel::OnCreateActClicked()
{
	return ActionHandlers.IsValid() ? ActionHandlers->OnCreateActClicked() : FReply::Handled();
}

FReply SStageEditorPanel::OnRegisterSelectedEntitiesClicked()
{
	return ActionHandlers.IsValid() ? ActionHandlers->OnRegisterSelectedEntitiesClicked() : FReply::Handled();
}

FReply SStageEditorPanel::OnCreateStageBPClicked()
{
	return ActionHandlers.IsValid() ? ActionHandlers->OnCreateStageBPClicked() : FReply::Handled();
}

FReply SStageEditorPanel::OnCreateEntityActorBPClicked()
{
	return ActionHandlers.IsValid() ? ActionHandlers->OnCreateEntityActorBPClicked() : FReply::Handled();
}

FReply SStageEditorPanel::OnCreateEntityComponentBPClicked()
{
	return ActionHandlers.IsValid() ? ActionHandlers->OnCreateEntityComponentBPClicked() : FReply::Handled();
}

FReply SStageEditorPanel::OnRefreshClicked()
{
	if (Controller.IsValid())
	{
		Controller->FindStageInWorld();
	}
	return FReply::Handled();
}

FReply SStageEditorPanel::OnCleanOrphanedEntitiesClicked()
{
	return ActionHandlers.IsValid() ? ActionHandlers->OnCleanOrphanedEntitiesClicked() : FReply::Handled();
}

TSharedPtr<SWidget> SStageEditorPanel::OnContextMenuOpening()
{
	if (!Controller.IsValid()) return nullptr;

	TArray<TSharedPtr<FStageTreeItem>> SelectedItems;
	StageTreeView->GetSelectedItems(SelectedItems);

	if (SelectedItems.Num() == 0) return nullptr;

	TSharedPtr<FStageTreeItem> Item = SelectedItems[0];
	if (!Item.IsValid()) return nullptr;

	FMenuBuilder MenuBuilder(true, nullptr);

	//----------------------------------------------------------------
	// Stage Context Menu
	//----------------------------------------------------------------
	if (Item->Type == EStageTreeItemType::Stage)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("RegisterSelectedActors", "Register Selected Actors"),
			LOCTEXT("RegisterSelectedActors_Tooltip", "Register currently selected level actors to this Stage"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Item]()
			{
				if (Controller.IsValid() && Item->StagePtr.IsValid())
				{
					Controller->SetActiveStage(Item->StagePtr.Get());
					OnRegisterSelectedEntitiesClicked(); 
				}
			}))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("EditStageBlueprint", "Edit Blueprint"),
			LOCTEXT("EditStageBlueprint_Tooltip", "Open the Blueprint Editor for this Stage"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Item]()
			{
				if (Item->StagePtr.IsValid())
				{
					if (UBlueprint* Blueprint = Cast<UBlueprint>(Item->StagePtr->GetClass()->ClassGeneratedBy))
					{
						GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
					}
				}
			}))
		);

		MenuBuilder.AddMenuSeparator();

		MenuBuilder.AddMenuEntry(
			LOCTEXT("UnregisterAllEntities", "Unregister All Entities"),
			LOCTEXT("UnregisterAllEntities_Tooltip", "Unregister all Entities from this Stage"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Item]()
			{
				if (Controller.IsValid() && Item->StagePtr.IsValid())
				{
					// Confirmation dialog
					FText ConfirmTitle = LOCTEXT("ConfirmUnregisterAll", "Confirm Unregister All");
					FText ConfirmMessage = LOCTEXT("ConfirmUnregisterAllMsg", "Are you sure you want to unregister all Entities from this Stage?\n\nThis will remove them from all Acts.");

					if (ShowConfirmDialog(ConfirmTitle, ConfirmMessage))
					{
						Controller->SetActiveStage(Item->StagePtr.Get());
						Controller->UnregisterAllEntities();
					}
				}
			}))
		);
	}

	//----------------------------------------------------------------
	// Act Context Menu
	//----------------------------------------------------------------
	else if (Item->Type == EStageTreeItemType::Act)
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateDataLayer", "Create Data Layer"),
			LOCTEXT("CreateDataLayer_Tooltip", "Create a new Data Layer for this Act"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.DataLayers"),
			FUIAction(FExecuteAction::CreateLambda([this, Item]()
			{
				if (Controller.IsValid())
				{
					if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(Item))
					{
						if (StageItem->StagePtr.IsValid())
						{
							Controller->SetActiveStage(StageItem->StagePtr.Get());
							Controller->CreateDataLayerForAct(Item->ID);
						}
					}
				}
			}))
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("LinkExistingDataLayer", "Link Existing DataLayer"),
			LOCTEXT("LinkExistingDataLayer_Tooltip", "Link an existing DataLayer to this Act"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Link"),
			FUIAction(FExecuteAction::CreateLambda([this, Item]()
			{
				if (Controller.IsValid())
				{
					if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(Item))
					{
						if (StageItem->StagePtr.IsValid())
						{
							Controller->SetActiveStage(StageItem->StagePtr.Get());
							ShowLinkDataLayerDialog(Item->ID);
						}
					}
				}
			}))
		);

		MenuBuilder.AddMenuSeparator();

		MenuBuilder.AddMenuEntry(
			LOCTEXT("RemoveAllEntitiesFromAct", "Remove All Entities from Act"),
			LOCTEXT("RemoveAllEntitiesFromAct_Tooltip", "Remove all Entities from this Act (they remain registered to the Stage)"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, Item]()
			{
				if (Controller.IsValid())
				{
					if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(Item))
					{
						if (StageItem->StagePtr.IsValid())
						{
							Controller->SetActiveStage(StageItem->StagePtr.Get());
							Controller->RemoveAllEntitiesFromAct(Item->ID);
						}
					}
				}
			}))
		);

		// Don't allow deleting Default Act (ID 0)
		if (Item->ID != 0)
		{
			MenuBuilder.AddMenuSeparator();

			MenuBuilder.AddMenuEntry(
				LOCTEXT("DeleteAct", "Delete Act"),
				LOCTEXT("DeleteAct_Tooltip", "Delete this Act"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
				FUIAction(FExecuteAction::CreateLambda([this, Item]()
				{
					if (Controller.IsValid())
					{
						if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(Item))
						{
							if (StageItem->StagePtr.IsValid())
							{
								Controller->SetActiveStage(StageItem->StagePtr.Get());

								// Confirmation dialog
								FText ConfirmTitle = LOCTEXT("ConfirmDeleteActMenu", "Confirm Delete Act");
								FText ConfirmMessage = FText::Format(
									LOCTEXT("ConfirmDeleteActMenuMsg", "Are you sure you want to delete Act '{0}'?"),
									FText::FromString(Item->DisplayName)
								);

								if (ShowConfirmDialog(ConfirmTitle, ConfirmMessage))
								{
									Controller->DeleteAct(Item->ID);
								}
							}
						}
					}
				}))
			);
		}
	}

	//----------------------------------------------------------------
	// Entity Context Menu
	//----------------------------------------------------------------
	else if (Item->Type == EStageTreeItemType::Entity)
	{
		// Check if Entity is under an Act or under Registered Entities folder
		TSharedPtr<FStageTreeItem> ParentItem = Item->Parent.Pin();
		bool bIsInAct = ParentItem.IsValid() && ParentItem->Type == EStageTreeItemType::Act;

		// Collect all selected Entities that are under the same Act (for batch operations)
		TArray<TSharedPtr<FStageTreeItem>> EntitiesInSameAct;
		int32 SharedActID = -1;
		TSharedPtr<FStageTreeItem> SharedActItem;

		if (bIsInAct)
		{
			SharedActID = ParentItem->ID;
			SharedActItem = ParentItem;

			for (const TSharedPtr<FStageTreeItem>& SelItem : SelectedItems)
			{
				if (SelItem.IsValid() && SelItem->Type == EStageTreeItemType::Entity)
				{
					TSharedPtr<FStageTreeItem> SelParent = SelItem->Parent.Pin();
					if (SelParent.IsValid() && SelParent->Type == EStageTreeItemType::Act && SelParent->ID == SharedActID)
					{
						EntitiesInSameAct.Add(SelItem);
					}
				}
			}
		}

		bool bMultipleEntitiesSelected = EntitiesInSameAct.Num() > 1;

		if (bIsInAct)
		{
			// Single Entity: Set State operation
			if (!bMultipleEntitiesSelected)
			{
				MenuBuilder.AddMenuEntry(
					LOCTEXT("SetEntityState", "Set State..."),
					LOCTEXT("SetEntityState_Tooltip", "Change the state value of this Entity in the Act"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, Item, ParentItem]()
					{
						if (Controller.IsValid())
						{
							// Simple input dialog
							TSharedRef<SWindow> InputWindow = SNew(SWindow)
								.Title(LOCTEXT("SetStateTitle", "Set Entity State"))
								.SizingRule(ESizingRule::Autosized)
								.SupportsMinimize(false)
								.SupportsMaximize(false);

							TSharedPtr<SEditableTextBox> TextBox;

							InputWindow->SetContent(
								SNew(SBorder)
								.Padding(10)
								[
									SNew(SVerticalBox)
									+ SVerticalBox::Slot()
									.AutoHeight()
									.Padding(0, 0, 0, 10)
									[
										SNew(STextBlock)
										.Text(LOCTEXT("EnterStateValue", "Enter new state value:"))
									]
									+ SVerticalBox::Slot()
									.AutoHeight()
									.Padding(0, 0, 0, 10)
									[
										SAssignNew(TextBox, SEditableTextBox)
										.Text(FText::FromString("0"))
									]
									+ SVerticalBox::Slot()
									.AutoHeight()
									[
										SNew(SHorizontalBox)
										+ SHorizontalBox::Slot()
										.FillWidth(1.0f)
										.HAlign(HAlign_Right)
										[
											SNew(SButton)
											.Text(LOCTEXT("OK", "OK"))
											.OnClicked_Lambda([this, Item, ParentItem, TextBox, InputWindow]()
											{
												FString InputText = TextBox->GetText().ToString();
												int32 NewState = FCString::Atoi(*InputText);

												if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(ParentItem))
												{
													if (StageItem->StagePtr.IsValid())
													{
														Controller->SetActiveStage(StageItem->StagePtr.Get());
														Controller->SetEntityStateInAct(Item->ID, ParentItem->ID, NewState);
													}
												}

												InputWindow->RequestDestroyWindow();
												return FReply::Handled();
											})
										]
										+ SHorizontalBox::Slot()
										.AutoWidth()
										.Padding(5, 0, 0, 0)
										[
											SNew(SButton)
											.Text(LOCTEXT("Cancel", "Cancel"))
											.OnClicked_Lambda([InputWindow]()
											{
												InputWindow->RequestDestroyWindow();
												return FReply::Handled();
											})
										]
									]
								]
							);

							FSlateApplication::Get().AddModalWindow(InputWindow, FSlateApplication::Get().GetActiveTopLevelWindow());
						}
					}))
				);
			}

			// Remove from Act - single or batch
			if (bMultipleEntitiesSelected)
			{
				// Batch remove: multiple Entities selected under same Act
				MenuBuilder.AddMenuEntry(
					FText::Format(LOCTEXT("RemoveSelectedEntitiesFromAct", "Remove {0} Entities from Act"), FText::AsNumber(EntitiesInSameAct.Num())),
					LOCTEXT("RemoveSelectedEntitiesFromAct_Tooltip", "Remove all selected Entities from this Act (they remain registered to the Stage)"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, EntitiesInSameAct, SharedActItem]()
					{
						if (Controller.IsValid() && SharedActItem.IsValid())
						{
							if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(SharedActItem))
							{
								if (StageItem->StagePtr.IsValid())
								{
									Controller->SetActiveStage(StageItem->StagePtr.Get());

									// Remove all selected Entities from the Act
									for (const TSharedPtr<FStageTreeItem>& EntityItem : EntitiesInSameAct)
									{
										if (EntityItem.IsValid())
										{
											Controller->RemoveEntityFromAct(EntityItem->ID, SharedActItem->ID);
										}
									}
								}
							}
						}
					}))
				);
			}
			else
			{
				// Single remove
				MenuBuilder.AddMenuEntry(
					LOCTEXT("RemoveEntityFromAct", "Remove from Act"),
					LOCTEXT("RemoveEntityFromAct_Tooltip", "Remove this Entity from the Act (it remains registered to the Stage)"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, Item, ParentItem]()
					{
						if (Controller.IsValid())
						{
							if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(ParentItem))
							{
								if (StageItem->StagePtr.IsValid())
								{
									Controller->SetActiveStage(StageItem->StagePtr.Get());
									Controller->RemoveEntityFromAct(Item->ID, ParentItem->ID);
								}
							}
						}
					}))
				);
			}

			MenuBuilder.AddMenuSeparator();
		}

		//----------------------------------------------------------------
		// Add to Act Operations (for all Entities, regardless of parent)
		//----------------------------------------------------------------
		
		// Collect all selected Entities (from any parent - Act or Registered Entities folder)
		TArray<TSharedPtr<FStageTreeItem>> AllSelectedEntities;
		for (const TSharedPtr<FStageTreeItem>& SelItem : SelectedItems)
		{
			if (SelItem.IsValid() && SelItem->Type == EStageTreeItemType::Entity)
			{
				AllSelectedEntities.Add(SelItem);
			}
		}

		if (AllSelectedEntities.Num() > 0)
		{
			MenuBuilder.AddMenuSeparator();

			// "Add to New Act" - Creates a new Act and adds selected Entities to it
			FText AddToNewActLabel = AllSelectedEntities.Num() > 1
				? FText::Format(LOCTEXT("AddEntitiesToNewAct", "Add {0} Entities to New Act"), FText::AsNumber(AllSelectedEntities.Num()))
				: LOCTEXT("AddEntityToNewAct", "Add to New Act");

			MenuBuilder.AddMenuEntry(
				AddToNewActLabel,
				LOCTEXT("AddToNewAct_Tooltip", "Create a new Act and add selected Entity(s) to it"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
				FUIAction(FExecuteAction::CreateLambda([this, AllSelectedEntities]()
				{
					if (Controller.IsValid() && AllSelectedEntities.Num() > 0)
					{
						if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(AllSelectedEntities[0]))
						{
							if (StageItem->StagePtr.IsValid())
							{
								Controller->SetActiveStage(StageItem->StagePtr.Get());

								// Create new Act
								int32 NewActID = Controller->CreateNewAct();
								if (NewActID != -1)
								{
									// Add all selected Entities to the new Act with default state 0
									for (const TSharedPtr<FStageTreeItem>& EntityItem : AllSelectedEntities)
									{
										if (EntityItem.IsValid())
										{
											Controller->SetEntityStateInAct(EntityItem->ID, NewActID, 0);
										}
									}

									FString Message = AllSelectedEntities.Num() > 1
										? FString::Printf(TEXT("Added %d Entities to new Act"), AllSelectedEntities.Num())
										: TEXT("Added Entity to new Act");
									DebugHeader::ShowNotifyInfo(Message);
								}
							}
						}
					}
				}))
			);

			// "Add to Existing Act" - Submenu with list of all Acts
			MenuBuilder.AddSubMenu(
				LOCTEXT("AddToExistingAct", "Add to Existing Act"),
				LOCTEXT("AddToExistingAct_Tooltip", "Add selected Entity(s) to an existing Act"),
				FNewMenuDelegate::CreateLambda([this, AllSelectedEntities](FMenuBuilder& SubMenuBuilder)
				{
					if (Controller.IsValid() && AllSelectedEntities.Num() > 0)
					{
						// Find parent Stage
						AStage* Stage = nullptr;
						if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(AllSelectedEntities[0]))
						{
							if (StageItem->StagePtr.IsValid())
							{
								Stage = StageItem->StagePtr.Get();
							}
						}

						if (Stage)
						{
							// Add menu entry for each Act
							for (const FAct& Act : Stage->Acts)
							{
								FText ActLabel = FText::FromString(Act.DisplayName);
								int32 ActID = Act.SUID.ActID;

								SubMenuBuilder.AddMenuEntry(
									ActLabel,
									FText::Format(LOCTEXT("AddToActTooltip", "Add selected Entity(s) to {0}"), ActLabel),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([this, AllSelectedEntities, Stage, ActID]()
									{
										if (Controller.IsValid())
										{
											Controller->SetActiveStage(Stage);
											
											// Add all selected Entities to the chosen Act with default state 0
											for (const TSharedPtr<FStageTreeItem>& EntityItem : AllSelectedEntities)
											{
												if (EntityItem.IsValid())
												{
													Controller->SetEntityStateInAct(EntityItem->ID, ActID, 0);
												}
											}
											
											FString Message = AllSelectedEntities.Num() > 1
												? FString::Printf(TEXT("Added %d Entities to Act"), AllSelectedEntities.Num())
												: TEXT("Added Entity to Act");
											DebugHeader::ShowNotifyInfo(Message);
										}
									}))
								);
							}
						}
					}
				}),
				false,
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Link")
			);
		}

		MenuBuilder.AddMenuSeparator();

		// Unregister from Stage - single or batch
		if (SelectedItems.Num() > 1)
		{
			// Count all Entities in selection (regardless of parent)
			TArray<TSharedPtr<FStageTreeItem>> AllSelectedEntitiesForUnregister;
			for (const TSharedPtr<FStageTreeItem>& SelItem : SelectedItems)
			{
				if (SelItem.IsValid() && SelItem->Type == EStageTreeItemType::Entity)
				{
					AllSelectedEntitiesForUnregister.Add(SelItem);
				}
			}

			if (AllSelectedEntitiesForUnregister.Num() > 1)
			{
				MenuBuilder.AddMenuEntry(
					FText::Format(LOCTEXT("UnregisterSelectedEntities", "Unregister {0} Entities from Stage"), FText::AsNumber(AllSelectedEntitiesForUnregister.Num())),
					LOCTEXT("UnregisterSelectedEntities_Tooltip", "Completely unregister all selected Entities from the Stage (removes from all Acts)"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, AllSelectedEntitiesForUnregister]()
					{
						if (Controller.IsValid())
						{
							// Confirmation dialog
							FText ConfirmTitle = LOCTEXT("ConfirmUnregisterMultiple", "Confirm Unregister");
							FText ConfirmMessage = FText::Format(
								LOCTEXT("ConfirmUnregisterMultipleMsg", "Are you sure you want to unregister {0} Entities from the Stage?\n\nThis will remove them from all Acts."),
								FText::AsNumber(AllSelectedEntitiesForUnregister.Num())
							);

							if (ShowConfirmDialog(ConfirmTitle, ConfirmMessage))
							{
								if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(AllSelectedEntitiesForUnregister[0]))
								{
									if (StageItem->StagePtr.IsValid())
									{
										Controller->SetActiveStage(StageItem->StagePtr.Get());

										// Unregister all selected Entities
										for (const TSharedPtr<FStageTreeItem>& EntityItem : AllSelectedEntitiesForUnregister)
										{
											if (EntityItem.IsValid())
											{
												Controller->UnregisterAllEntities(EntityItem->ID);
											}
										}
									}
								}
							}
						}
					}))
				);
			}
			else
			{
				// Single unregister
				MenuBuilder.AddMenuEntry(
					LOCTEXT("UnregisterEntity", "Unregister from Stage"),
					LOCTEXT("UnregisterEntity_Tooltip", "Completely unregister this Entity from the Stage (removes from all Acts)"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, Item]()
					{
						if (Controller.IsValid())
						{
							// Confirmation dialog
							FText ConfirmTitle = LOCTEXT("ConfirmUnregister", "Confirm Unregister");
							FText ConfirmMessage = FText::Format(
								LOCTEXT("ConfirmUnregisterMsg", "Are you sure you want to unregister '{0}' from the Stage?\n\nThis will remove it from all Acts."),
								FText::FromString(Item->DisplayName)
							);

							if (ShowConfirmDialog(ConfirmTitle, ConfirmMessage))
							{
								if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(Item))
								{
									if (StageItem->StagePtr.IsValid())
									{
										Controller->SetActiveStage(StageItem->StagePtr.Get());
										Controller->UnregisterAllEntities(Item->ID);
									}
								}
							}
						}
					}))
				);
			}
		}
		else
		{
			// Single unregister
			MenuBuilder.AddMenuEntry(
				LOCTEXT("UnregisterEntity", "Unregister from Stage"),
				LOCTEXT("UnregisterEntity_Tooltip", "Completely unregister this Entity from the Stage (removes from all Acts)"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, Item]()
				{
					if (Controller.IsValid())
					{
						// Confirmation dialog
						FText ConfirmTitle = LOCTEXT("ConfirmUnregister", "Confirm Unregister");
						FText ConfirmMessage = FText::Format(
							LOCTEXT("ConfirmUnregisterMsg", "Are you sure you want to unregister '{0}' from the Stage?\n\nThis will remove it from all Acts."),
							FText::FromString(Item->DisplayName)
						);

						if (ShowConfirmDialog(ConfirmTitle, ConfirmMessage))
						{
							if (TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(Item))
							{
								if (StageItem->StagePtr.IsValid())
								{
									Controller->SetActiveStage(StageItem->StagePtr.Get());
									Controller->UnregisterAllEntities(Item->ID);
								}
							}
						}
					}
				}))
			);
		}
	}

	return MenuBuilder.MakeWidget();
}
#pragma endregion Callbacks

#pragma region Drag & Drop Support
//----------------------------------------------------------------
// Drag & Drop Support
//----------------------------------------------------------------

void SStageEditorPanel::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	SCompoundWidget::OnDragEnter(MyGeometry, DragDropEvent);
}

void SStageEditorPanel::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	SCompoundWidget::OnDragLeave(DragDropEvent);
}

FReply SStageEditorPanel::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// Check if this is an Actor drag operation from World Outliner
	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (Operation.IsValid() && Operation->IsOfType<FActorDragDropOp>())
	{
		return FReply::Handled();
	}
	
	return FReply::Unhandled();
}

FReply SStageEditorPanel::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// Fallback drop handler (e.g. dropping on empty space)
	// For now, we can just ignore it or default to active stage
	return FReply::Unhandled();
}

/**
 * @brief Handles drop events on tree view rows
 * @details Processes actor drops from World Outliner, registers them to the appropriate
 *          Stage, and provides user feedback via notifications.
 * @param MyGeometry The geometry of the widget (unused, passed as default)
 * @param DragDropEvent The drag and drop event containing the dragged actors
 * @param TargetItem The tree item where the drop occurred
 * @return FReply::Handled() if drop was processed, FReply::Unhandled() otherwise
 */
FReply SStageEditorPanel::OnRowDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent, TSharedPtr<FStageTreeItem> TargetItem)
{
	if (DragDropHandler.IsValid())
	{
		return DragDropHandler->OnRowDrop(DragDropEvent, TargetItem);
	}
	return FReply::Unhandled();
}
#pragma endregion Drag & Drop Support

#pragma region Private Helpers

/**
 * @brief Checks if an item is the drag target or one of its descendants
 * @param Item The item to check
 * @param DragTarget The current drag target (Stage item)
 * @return true if Item is DragTarget or a descendant of DragTarget
 */
bool SStageEditorPanel::IsItemOrDescendantOf(TSharedPtr<FStageTreeItem> Item, TSharedPtr<FStageTreeItem> DragTarget)
{
	if (DragDropHandler.IsValid())
	{
		return DragDropHandler->IsItemOrDescendantOf(Item, DragTarget);
	}
	return false;
}

/**
 * @brief Handles drag enter events on tree view rows
 * @details Called when a drag operation enters a tree row. Identifies the parent Stage
 *          of the hovered row and sets it as the drag target for visual feedback.
 * @param DragDropEvent The drag and drop event containing operation details
 * @param TargetItem The tree item that was entered during the drag operation
 */
void SStageEditorPanel::OnRowDragEnter(const FDragDropEvent& DragDropEvent, TSharedPtr<FStageTreeItem> TargetItem)
{
	if (DragDropHandler.IsValid())
	{
		DragDropHandler->OnRowDragEnter(DragDropEvent, TargetItem);
	}
}

/**
 * @brief Handles drag leave events on tree view rows
 * @details Called when a drag operation leaves a tree row. Clears the drag target
 *          to remove visual highlighting feedback.
 * @param DragDropEvent The drag and drop event
 * @param TargetItem The tree item that was left during the drag operation
 */
void SStageEditorPanel::OnRowDragLeave(const FDragDropEvent& DragDropEvent, TSharedPtr<FStageTreeItem> TargetItem)
{
	if (DragDropHandler.IsValid())
	{
		DragDropHandler->OnRowDragLeave(DragDropEvent, TargetItem);
	}
}

void SStageEditorPanel::RegisterViewportSelectionListener()
{
	if (!GEditor)
	{
		return;
	}

	if (USelection* ActorSelection = GEditor->GetSelectedActors())
	{
		ActorSelectionPtr = ActorSelection;

		if (!ViewportSelectionDelegateHandle.IsValid())
		{
			ViewportSelectionDelegateHandle = ActorSelection->SelectObjectEvent.AddSP(this, &SStageEditorPanel::HandleViewportSelectionChanged);
		}

		HandleViewportSelectionChanged(nullptr);
	}
}

void SStageEditorPanel::UnregisterViewportSelectionListener()
{
	if (ActorSelectionPtr.IsValid() && ViewportSelectionDelegateHandle.IsValid())
	{
		ActorSelectionPtr->SelectObjectEvent.Remove(ViewportSelectionDelegateHandle);
	}

	ViewportSelectionDelegateHandle = FDelegateHandle();
	ActorSelectionPtr.Reset();
}

void SStageEditorPanel::HandleViewportSelectionChanged(UObject* SelectedObject)
{
	if (bUpdatingViewportSelectionFromPanel || !StageTreeView.IsValid())
	{
		return;
	}

	AActor* SelectedActor = Cast<AActor>(SelectedObject);

	if (!SelectedActor && GEditor)
	{
		if (USelection* SelectedActors = GEditor->GetSelectedActors())
		{
			for (FSelectionIterator It(*SelectedActors); It; ++It)
			{
				if (AActor* Actor = Cast<AActor>(*It))
				{
					SelectedActor = Actor;
					break;
				}
			}
		}
	}

	if (!SelectedActor)
	{
		TGuardValue<bool> Guard(bUpdatingTreeSelectionFromViewport, true);
		StageTreeView->ClearSelection();
		return;
	}

	const FString ActorPath = SelectedActor->GetPathName();
	if (ActorPath.IsEmpty())
	{
		return;
	}

	if (TWeakPtr<FStageTreeItem>* ItemPtr = ActorPathToTreeItem.Find(ActorPath))
	{
		if (TSharedPtr<FStageTreeItem> TreeItem = ItemPtr->Pin())
		{
			ExpandAncestors(TreeItem);
			TGuardValue<bool> Guard(bUpdatingTreeSelectionFromViewport, true);
			StageTreeView->SetSelection(TreeItem);
			StageTreeView->RequestScrollIntoView(TreeItem);
		}
		else
		{
			ActorPathToTreeItem.Remove(ActorPath);
		}
	}
}

void SStageEditorPanel::ExpandAncestors(TSharedPtr<FStageTreeItem> Item)
{
	if (!StageTreeView.IsValid() || !Item.IsValid())
	{
		return;
	}

	TSharedPtr<FStageTreeItem> CurrentParent = Item->Parent.Pin();
	while (CurrentParent.IsValid())
	{
		StageTreeView->SetItemExpansion(CurrentParent, true);
		CurrentParent = CurrentParent->Parent.Pin();
	}
}

TSharedPtr<FStageTreeItem> SStageEditorPanel::FindStageAncestor(TSharedPtr<FStageTreeItem> Item) const
{
	TSharedPtr<FStageTreeItem> CurrentItem = Item;
	while (CurrentItem.IsValid())
	{
		if (CurrentItem->Type == EStageTreeItemType::Stage)
		{
			return CurrentItem;
		}
		CurrentItem = CurrentItem->Parent.Pin();
	}

	return nullptr;
}

bool SStageEditorPanel::ShowConfirmDialog(const FText& Title, const FText& Message) const
{
	return FMessageDialog::Open(EAppMsgType::YesNo, Message, Title) == EAppReturnType::Yes;
}

void SStageEditorPanel::ApplyEntityStateChange(TSharedPtr<FStageTreeItem> EntityItem, TSharedPtr<FStageTreeItem> ParentActItem, int32 NewState)
{
	if (!Controller.IsValid() || !EntityItem.IsValid() || !ParentActItem.IsValid())
	{
		return;
	}

	if (EntityItem->bHasEntityState && EntityItem->EntityState == NewState)
	{
		return;
	}

	TSharedPtr<FStageTreeItem> StageItem = FindStageAncestor(ParentActItem);
	if (!StageItem.IsValid() || !StageItem->StagePtr.IsValid())
	{
		return;
	}

	Controller->SetActiveStage(StageItem->StagePtr.Get());
	Controller->SetEntityStateInAct(EntityItem->ID, ParentActItem->ID, NewState);
}

void SStageEditorPanel::SelectActorInViewport(AActor* ActorToSelect)
{
	if (!GEditor || !ActorToSelect)
	{
		return;
	}

	TGuardValue<bool> Guard(bUpdatingViewportSelectionFromPanel, true);
	GEditor->SelectNone(false, true);
	GEditor->SelectActor(ActorToSelect, true, true);
}

void SStageEditorPanel::ShowLinkDataLayerDialog(int32 ActID)
{
	if (!Controller.IsValid()) return;

	// TODO: Implement DataLayer linking dialog
	// This feature requires AnalyzeDataLayerHierarchy and LinkDataLayerToAct functions
	FMessageDialog::Open(EAppMsgType::Ok,
		LOCTEXT("LinkDataLayerNotImplemented", "DataLayer linking dialog is not yet implemented.\n\nUse the Details panel to assign DataLayers to Acts."));
}

/**
 * @brief Handles drop events on tree view rows
 * @details Processes actor drops from World Outliner, registers them to the appropriate
 *          Stage, and provides user feedback via notifications.
 * @param MyGeometry The geometry of the widget (unused, passed as default)
 * @param DragDropEvent The drag and drop event containing the dragged actors
 * @param TargetItem The tree item where the drop occurred
 * @return FReply::Handled() if drop was processed, FReply::Unhandled() otherwise
 */

bool SStageEditorPanel::IsWorldPartitionLevel() const
{
	if (!Controller.IsValid())
	{
		return false;
	}

	return Controller->IsWorldPartitionActive();
}

bool SStageEditorPanel::HasRegistryAsset() const
{
	return StateManager.IsValid() ? StateManager->HasRegistryAsset() : false;
}

UStageRegistryAsset* SStageEditorPanel::GetCachedRegistry() const
{
	return StateManager.IsValid() ? StateManager->GetCachedRegistry() : nullptr;
}

EVisibility SStageEditorPanel::GetRegistryWarningVisibility() const
{
	// Show warning if Registry is missing
	return HasRegistryAsset() ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SStageEditorPanel::GetRegistryPathText() const
{
	if (!StateManager.IsValid())
	{
		return LOCTEXT("RegistryPathUnknown", "Unknown");
	}

	FString RegistryPath = StateManager->GetRegistryAssetPath();
	return FText::FromString(RegistryPath);
}

FReply SStageEditorPanel::OnCreateRegistryClicked()
{
	return ActionHandlers.IsValid() ? ActionHandlers->OnCreateRegistryClicked() : FReply::Handled();
}

FReply SStageEditorPanel::OnSelectExistingRegistryClicked()
{
	return ActionHandlers.IsValid() ? ActionHandlers->OnSelectExistingRegistryClicked() : FReply::Handled();
}

//----------------------------------------------------------------
// Phase 13.5: Multi-User Registry Sync UI
//----------------------------------------------------------------

EVisibility SStageEditorPanel::GetLockStatusBarVisibility() const
{
	// Phase 18 Performance Fix: Read cached visibility (FAST, no query)
	return StateManager.IsValid() ? StateManager->GetCachedLockStatusBarVisibility() : EVisibility::Collapsed;
}

EVisibility SStageEditorPanel::GetSyncWarningVisibility() const
{
	// Phase 18 Performance Fix: Read cached visibility (FAST, no CalculateSyncStatus)
	// Before fix: Called 60 times/second with Controller->CalculateSyncStatus()
	// After fix: Reads cached value, updated only on explicit refresh
	return StateManager.IsValid() ? StateManager->GetCachedSyncWarningVisibility() : EVisibility::Collapsed;
}

FText SStageEditorPanel::GetLockStatusText() const
{
	return StateManager.IsValid() ? StateManager->GetCachedLockStatusText() : FText::GetEmpty();
}

FText SStageEditorPanel::GetSyncStatusText() const
{
	return StateManager.IsValid() ? StateManager->GetCachedSyncStatusText() : FText::GetEmpty();
}

FText SStageEditorPanel::GetSyncButtonText() const
{
	if (!Controller.IsValid())
	{
		return LOCTEXT("SyncRegistry", "Sync Registry");
	}

	// Get current world and registry
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return LOCTEXT("SyncRegistry", "Sync Registry");
	}

	UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (!EditorSubsystem)
	{
		return LOCTEXT("SyncRegistry", "Sync Registry");
	}

	UStageRegistryAsset* Registry = EditorSubsystem->GetOrLoadRegistryAsset(World);
	if (!Registry)
	{
		return LOCTEXT("SyncRegistry", "Sync Registry");
	}

	// Calculate sync status
	FRegistrySyncStatus SyncStatus = Controller->CalculateSyncStatus(World, Registry);

	// Phase 13.9: Priority to reconciliation if there are offline-created Stages
	int32 ReconcileCount = SyncStatus.GetPendingReconciliationCount();
	if (ReconcileCount > 0)
	{
		return FText::Format(LOCTEXT("ReconcileStages", "Reconcile {0} Stage(s)"), FText::AsNumber(ReconcileCount));
	}

	// Default: Sync Registry
	return LOCTEXT("SyncRegistry", "Sync Registry");
}

FText SStageEditorPanel::GetSyncButtonTooltip() const
{
	if (!Controller.IsValid())
	{
		return LOCTEXT("SyncRegistry_Tooltip", "Assign IDs to pending Stages and remove orphaned Registry entries");
	}

	// Get current world and registry
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return LOCTEXT("SyncRegistry_Tooltip", "Assign IDs to pending Stages and remove orphaned Registry entries");
	}

	UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (!EditorSubsystem)
	{
		return LOCTEXT("SyncRegistry_Tooltip", "Assign IDs to pending Stages and remove orphaned Registry entries");
	}

	UStageRegistryAsset* Registry = EditorSubsystem->GetOrLoadRegistryAsset(World);
	if (!Registry)
	{
		return LOCTEXT("SyncRegistry_Tooltip", "Assign IDs to pending Stages and remove orphaned Registry entries");
	}

	// Calculate sync status
	FRegistrySyncStatus SyncStatus = Controller->CalculateSyncStatus(World, Registry);

	// Phase 13.9: Priority to reconciliation if there are offline-created Stages
	int32 ReconcileCount = SyncStatus.GetPendingReconciliationCount();
	if (ReconcileCount > 0)
	{
		return LOCTEXT("ReconcileStages_Tooltip", "Convert temporary IDs to real IDs for offline-created Stages");
	}

	// Default: Sync Registry tooltip
	return LOCTEXT("SyncRegistry_Tooltip", "Assign IDs to pending Stages and remove orphaned Registry entries");
}

FReply SStageEditorPanel::OnSyncRegistryClicked()
{
	if (!Controller.IsValid())
	{
		return FReply::Handled();
	}

	// Phase 13.9: Check if we need to prioritize reconciliation
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
				int32 ReconcileCount = SyncStatus.GetPendingReconciliationCount();

				if (ReconcileCount > 0)
				{
					// Priority: Call ReconcilePendingStages() first
					Controller->ReconcilePendingStages();
					return FReply::Handled();
				}
			}
		}
	}

	// Default: Call SyncRegistry()
	return ActionHandlers.IsValid() ? ActionHandlers->OnSyncRegistryClicked() : FReply::Handled();
}

FReply SStageEditorPanel::OnViewChangelistClicked()
{
	return ActionHandlers.IsValid() ? ActionHandlers->OnViewChangelistClicked() : FReply::Handled();
}

FReply SStageEditorPanel::OnRefreshLockStatusClicked()
{
	// Phase 18: Manual refresh of Source Control status
	// Path 2 of update strategy: User explicitly triggers refresh

	if (StateManager.IsValid())
	{
		// Refresh all status caches (Lock + Sync)
		StateManager->RefreshAllStatusTexts();
	}

	return FReply::Handled();
}

//----------------------------------------------------------------
// Phase 13.10: Manual ID Recycling
//----------------------------------------------------------------

EVisibility SStageEditorPanel::GetRecycleIDsButtonVisibility() const
{
	// Always visible so user knows the feature exists
	// Button text will show "(0)" when no deleted IDs to recycle
	return EVisibility::Visible;
}

FText SStageEditorPanel::GetRecycleIDsButtonText() const
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return LOCTEXT("RecycleIDs_NoWorld", "Recycle IDs");
	}

	UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (!EditorSubsystem)
	{
		return LOCTEXT("RecycleIDs_NoSubsystem", "Recycle IDs");
	}

	UStageRegistryAsset* Registry = EditorSubsystem->GetOrLoadRegistryAsset(World);
	if (!Registry)
	{
		return LOCTEXT("RecycleIDs_NoRegistry", "Recycle IDs");
	}

	// Detect ID gaps (unused IDs that can be recycled)
	TArray<int32> GapIDs;
	Registry->DetectIDGaps(GapIDs);
	int32 GapCount = GapIDs.Num();
	int32 RecycledCount = Registry->GetRecycledIDCount();

	if (GapCount > 0)
	{
		return FText::Format(LOCTEXT("RecycleIDs_WithGaps", "Recycle IDs ({0})"), FText::AsNumber(GapCount));
	}
	else if (RecycledCount > 0)
	{
		return FText::Format(LOCTEXT("RecycleIDs_WithRecycled", "Recycle IDs [Pool: {0}]"), FText::AsNumber(RecycledCount));
	}
	return LOCTEXT("RecycleIDs_Empty", "Recycle IDs");
}

FText SStageEditorPanel::GetRecycleIDsButtonTooltip() const
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return LOCTEXT("RecycleIDs_Tooltip_Default", "Recycle unused StageIDs for reuse when creating new Stages");
	}

	UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (!EditorSubsystem)
	{
		return LOCTEXT("RecycleIDs_Tooltip_Default2", "Recycle unused StageIDs for reuse when creating new Stages");
	}

	UStageRegistryAsset* Registry = EditorSubsystem->GetOrLoadRegistryAsset(World);
	if (!Registry)
	{
		return LOCTEXT("RecycleIDs_Tooltip_Default3", "Recycle unused StageIDs for reuse when creating new Stages");
	}

	// Detect ID gaps
	TArray<int32> GapIDs;
	Registry->DetectIDGaps(GapIDs);
	int32 GapCount = GapIDs.Num();
	int32 RecycledCount = Registry->GetRecycledIDCount();

	if (GapCount == 0 && RecycledCount == 0)
	{
		return LOCTEXT("RecycleIDs_Tooltip_Empty",
			"No ID gaps to recycle.\n\n"
			"All StageIDs are sequential with no gaps.");
	}

	// Build ID preview strings
	FString GapPreview;
	for (int32 i = 0; i < FMath::Min(GapIDs.Num(), 5); ++i)
	{
		if (i > 0) GapPreview += TEXT(", ");
		GapPreview += FString::FromInt(GapIDs[i]);
	}
	if (GapIDs.Num() > 5)
	{
		GapPreview += FString::Printf(TEXT("... (+%d more)"), GapIDs.Num() - 5);
	}

	FString RecycledPreview;
	const TArray<int32>& RecycledIDs = Registry->GetRecycledIDs();
	for (int32 i = 0; i < FMath::Min(RecycledIDs.Num(), 5); ++i)
	{
		if (i > 0) RecycledPreview += TEXT(", ");
		RecycledPreview += FString::FromInt(RecycledIDs[i]);
	}
	if (RecycledIDs.Num() > 5)
	{
		RecycledPreview += FString::Printf(TEXT("... (+%d more)"), RecycledIDs.Num() - 5);
	}

	if (GapCount > 0)
	{
		return FText::Format(
			LOCTEXT("RecycleIDs_Tooltip_WithGaps",
				"Recycle {0} unused ID(s) for reuse.\n\n"
				"ID gaps (unused): [{1}]\n"
				"Recycled IDs (in pool): [{2}]\n\n"
				"Click to add gap IDs to the recycle pool.\n"
				"New Stages will reuse IDs from the pool."),
			FText::AsNumber(GapCount),
			FText::FromString(GapPreview.IsEmpty() ? TEXT("none") : GapPreview),
			FText::FromString(RecycledPreview.IsEmpty() ? TEXT("none") : RecycledPreview)
		);
	}
	else
	{
		return FText::Format(
			LOCTEXT("RecycleIDs_Tooltip_OnlyRecycled",
				"No ID gaps to recycle.\n\n"
				"Recycled IDs (available for reuse): [{0}]\n\n"
				"When creating new Stages, IDs from this pool will be used first."),
			FText::FromString(RecycledPreview)
		);
	}
}

FReply SStageEditorPanel::OnRecycleIDsClicked()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FReply::Handled();
	}

	UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (!EditorSubsystem)
	{
		return FReply::Handled();
	}

	UStageRegistryAsset* Registry = EditorSubsystem->GetOrLoadRegistryAsset(World);
	if (!Registry)
	{
		return FReply::Handled();
	}

	// Detect ID gaps
	TArray<int32> GapIDs;
	Registry->DetectIDGaps(GapIDs);
	int32 GapCount = GapIDs.Num();

	if (GapCount == 0)
	{
		int32 RecycledCount = Registry->GetRecycledIDCount();
		if (RecycledCount > 0)
		{
			DebugHeader::ShowNotifyInfo(FString::Printf(
				TEXT("No ID gaps to recycle.\n\nRecycle pool already has %d ID(s) available for reuse."),
				RecycledCount));
		}
		else
		{
			DebugHeader::ShowNotifyInfo(TEXT("No ID gaps to recycle.\n\nAll StageIDs are sequential with no gaps."));
		}
		return FReply::Handled();
	}

	// Multi-user mode: Check SC and CheckOut first
	bool bIsMultiMode = Registry->GetCollaborationMode() == ECollaborationMode::Multi;
	if (bIsMultiMode)
	{
		if (!EditorSubsystem->IsSourceControlEnabled())
		{
			DebugHeader::ShowNotifyInfo(TEXT("Recycle IDs failed: Source Control is offline. Please connect to P4 first."));
			return FReply::Handled();
		}

		FString ErrorMessage;
		if (!EditorSubsystem->CheckOutToChangelist(Registry, ErrorMessage))
		{
			DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Recycle IDs failed: Registry locked (%s)"), *ErrorMessage));
			return FReply::Handled();
		}
	}

	// Execute recycling with transaction support
	FScopedTransaction Transaction(LOCTEXT("RecycleStageIDs_Transaction", "Recycle Stage IDs"));

	int32 RecycledCount = Registry->RecycleIDGaps();

	if (RecycledCount > 0)
	{
		// Multi-user mode: Save Registry and open Changelist panel
		if (bIsMultiMode)
		{
			if (EditorSubsystem->SaveRegistryToDisk(Registry))
			{
				UE_LOG(LogTemp, Log, TEXT("RecycleIDs: Saved Registry after recycling"));
				EditorSubsystem->OpenChangelistPanel();
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("RecycleIDs: Failed to save Registry"));
			}
		}

		DebugHeader::ShowNotifyInfo(FString::Printf(
			TEXT("Recycled %d ID(s).\n\nThese IDs will be reused when creating new Stages."),
			RecycledCount));

		// Refresh status
		if (StateManager.IsValid())
		{
			StateManager->RefreshAllStatusTexts();
		}
	}

	return FReply::Handled();
}

//----------------------------------------------------------------
// Phase 13.10: Repair Duplicate IDs
//----------------------------------------------------------------

EVisibility SStageEditorPanel::GetRepairDuplicateIDsButtonVisibility() const
{
	// Always visible
	return EVisibility::Visible;
}

FText SStageEditorPanel::GetRepairDuplicateIDsButtonText() const
{
	return LOCTEXT("RepairDuplicateIDs_Text", "Repair Duplicate IDs");
}

FText SStageEditorPanel::GetRepairDuplicateIDsButtonTooltip() const
{
	return LOCTEXT("RepairDuplicateIDs_Tooltip",
		"Detect and repair duplicate StageIDs in the World.\n\n"
		"Duplicates can occur from copy-paste or broken undo operations.\n"
		"The first Stage keeps its ID, duplicates get new IDs.");
}

FReply SStageEditorPanel::OnRepairDuplicateIDsClicked()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FReply::Handled();
	}

	UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (!EditorSubsystem)
	{
		return FReply::Handled();
	}

	UStageRegistryAsset* Registry = EditorSubsystem->GetOrLoadRegistryAsset(World);
	if (!Registry)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Cannot repair duplicates: No Registry found for this Level."));
		return FReply::Handled();
	}

	// First detect duplicates
	TMap<int32, TArray<AStage*>> Duplicates;
	int32 DuplicateGroupCount = Registry->DetectDuplicateStageIDs(World, Duplicates);

	if (DuplicateGroupCount == 0)
	{
		DebugHeader::ShowNotifyInfo(TEXT("No duplicate StageIDs found. All Stages have unique IDs."));
		return FReply::Handled();
	}

	// Build confirmation message
	FString DuplicateInfo;
	int32 TotalDuplicates = 0;
	for (const auto& Pair : Duplicates)
	{
		TotalDuplicates += Pair.Value.Num() - 1; // First one keeps ID
		if (DuplicateInfo.Len() < 500) // Limit message size
		{
			DuplicateInfo += FString::Printf(TEXT("\n  StageID %d: %d Stages"), Pair.Key, Pair.Value.Num());
		}
	}

	FText ConfirmMessage = FText::Format(
		LOCTEXT("RepairDuplicateIDs_Confirm",
			"Found {0} duplicate StageID group(s) affecting {1} Stage(s).\n"
			"{2}\n\n"
			"Repair will assign new unique IDs to duplicate Stages.\n"
			"The first Stage found keeps its original ID.\n\n"
			"This action can be undone (Ctrl+Z).\n\n"
			"Continue?"),
		FText::AsNumber(DuplicateGroupCount),
		FText::AsNumber(TotalDuplicates),
		FText::FromString(DuplicateInfo)
	);

	EAppReturnType::Type Result = DebugHeader::ShowMsgDialog(
		EAppMsgType::YesNo,
		ConfirmMessage.ToString(),
		false
	);

	if (Result != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	// Multi-user mode: Check SC and CheckOut first
	bool bIsMultiMode = Registry->GetCollaborationMode() == ECollaborationMode::Multi;
	if (bIsMultiMode)
	{
		if (!EditorSubsystem->IsSourceControlEnabled())
		{
			DebugHeader::ShowNotifyInfo(TEXT("Repair Duplicate IDs failed: Source Control is offline."));
			return FReply::Handled();
		}

		FString ErrorMessage;
		if (!EditorSubsystem->CheckOutToChangelist(Registry, ErrorMessage))
		{
			DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Repair Duplicate IDs failed: Registry locked (%s)"), *ErrorMessage));
			return FReply::Handled();
		}
	}

	// Execute repair with transaction support
	FScopedTransaction Transaction(LOCTEXT("RepairDuplicateStageIDs_Transaction", "Repair Duplicate Stage IDs"));

	TArray<FString> RepairLog;
	int32 RepairedCount = Registry->RepairDuplicateStageIDs(World, RepairLog);

	if (RepairedCount > 0)
	{
		// Multi-user mode: Save Registry
		if (bIsMultiMode)
		{
			if (EditorSubsystem->SaveRegistryToDisk(Registry))
			{
				UE_LOG(LogTemp, Log, TEXT("RepairDuplicateIDs: Saved Registry after repair"));
				EditorSubsystem->OpenChangelistPanel();
			}
		}

		// Show summary
		FString Summary = FString::Printf(TEXT("Repaired %d duplicate Stage(s).\n"), RepairedCount);
		for (int32 i = 0; i < FMath::Min(RepairLog.Num(), 10); ++i)
		{
			Summary += TEXT("\n") + RepairLog[i];
		}
		if (RepairLog.Num() > 10)
		{
			Summary += FString::Printf(TEXT("\n... and %d more"), RepairLog.Num() - 10);
		}

		DebugHeader::ShowNotifyInfo(*Summary);

		// Refresh UI
		if (Controller.IsValid())
		{
			Controller->FindStageInWorld();
		}

		if (StateManager.IsValid())
		{
			StateManager->RefreshAllStatusTexts();
		}
	}
	else
	{
		DebugHeader::ShowNotifyInfo(TEXT("No Stages needed repair."));
	}

	return FReply::Handled();
}

FReply SStageEditorPanel::OnConvertToWorldPartitionClicked()
{
	FReply Result = ActionHandlers.IsValid() ? ActionHandlers->OnConvertToWorldPartitionClicked() : FReply::Handled();

	// Update cache after conversion
	if (IsWorldPartitionLevel())
	{
		bCachedIsWorldPartition = true;
	}

	return Result;
}

FReply SStageEditorPanel::OnRefreshWorldPartitionStatusClicked()
{
	const bool bNowIsWorldPartition = IsWorldPartitionLevel();

	if (bNowIsWorldPartition)
	{
		// World Partition is now active - show success and rebuild
		DebugHeader::ShowMsgDialog(
			EAppMsgType::Ok,
			TEXT("World Partition detected!\n\n"
				"Stage Editor features are now available."),
			false
		);

		bCachedIsWorldPartition = true;
		RebuildUI();
	}
	else
	{
		// Still not World Partition
		DebugHeader::ShowMsgDialog(
			EAppMsgType::Ok,
			TEXT("World Partition is still not active.\n\n"
				"Please complete the conversion process first.\n"
				"If you just converted, try saving and reloading the level."),
			true
		);
	}

	return FReply::Handled();
}

void SStageEditorPanel::OnAssetCreationSettingsChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	if (!Controller.IsValid() || !CreationSettings.IsValid())
	{
		return;
	}

	FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
	if (!Settings)
	{
		return;
	}

	// Update Controller's DataLayer asset folder path when settings change
	FString DataLayerPath;
	if (Settings->bIsCustomDataLayerAssetPath)
	{
		// Convert physical path to virtual path if needed
		FString PhysicalPath = Settings->DataLayerAssetFolderPath.Path;
		FString ProjectContentDir = FPaths::ProjectContentDir();

		if (PhysicalPath.StartsWith(ProjectContentDir))
		{
			FString RelativePath = PhysicalPath.RightChop(ProjectContentDir.Len());
			DataLayerPath = TEXT("/Game/") + RelativePath;
		}
		else
		{
			// Assume it's already a virtual path
			DataLayerPath = PhysicalPath;
		}
	}
	else
	{
		DataLayerPath = TEXT("/StageEditor/DataLayers");
	}

	Controller->SetDataLayerAssetFolderPath(DataLayerPath);

	// NOTE: Auto-save removed. Settings are now saved explicitly via Save button.
}

void SStageEditorPanel::LoadAssetCreationSettingsFromConfig()
{
	if (!CreationSettings.IsValid())
	{
		return;
	}

	// Try to load from JSON first (new format)
	FString JsonConfigPath = GetPersonalConfigFilePath();
	if (FPaths::FileExists(JsonConfigPath))
	{
		if (LoadSettingsFromJson(JsonConfigPath))
		{
			UE_LOG(LogTemp, Log, TEXT("StageEditor: Loaded settings from JSON: %s"), *JsonConfigPath);
			return;
		}
	}

	// Fallback: try to load from old INI format (backwards compatibility)
	const FString IniConfigFile = FPaths::GeneratedConfigDir() / TEXT("StageEditorUserSettings.ini");
	if (FPaths::FileExists(IniConfigFile))
	{
		FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
		if (!Settings)
		{
			return;
		}

		const FString SectionName = TEXT("AssetCreationSettings");

		// Load boolean flags
		GConfig->GetBool(*SectionName, TEXT("bIsCustomStageAssetFolderPath"), Settings->bIsCustomStageAssetFolderPath, IniConfigFile);
		GConfig->GetBool(*SectionName, TEXT("bIsCustomEntityActorAssetPath"), Settings->bIsCustomEntityActorAssetPath, IniConfigFile);
		GConfig->GetBool(*SectionName, TEXT("bIsCustomEntityComponentAssetPath"), Settings->bIsCustomEntityComponentAssetPath, IniConfigFile);
		GConfig->GetBool(*SectionName, TEXT("bIsCustomDataLayerAssetPath"), Settings->bIsCustomDataLayerAssetPath, IniConfigFile);

		// Load folder paths
		FString TempPath;
		if (GConfig->GetString(*SectionName, TEXT("StageAssetFolderPath"), TempPath, IniConfigFile) && !TempPath.IsEmpty())
		{
			Settings->StageAssetFolderPath.Path = TempPath;
		}
		if (GConfig->GetString(*SectionName, TEXT("EntityActorAssetFolderPath"), TempPath, IniConfigFile) && !TempPath.IsEmpty())
		{
			Settings->EntityActorAssetFolderPath.Path = TempPath;
		}
		if (GConfig->GetString(*SectionName, TEXT("EntityComponentAssetFolderPath"), TempPath, IniConfigFile) && !TempPath.IsEmpty())
		{
			Settings->EntityComponentAssetFolderPath.Path = TempPath;
		}
		if (GConfig->GetString(*SectionName, TEXT("DataLayerAssetFolderPath"), TempPath, IniConfigFile) && !TempPath.IsEmpty())
		{
			Settings->DataLayerAssetFolderPath.Path = TempPath;
		}

		// Load parent class paths
		if (GConfig->GetString(*SectionName, TEXT("DefaultStageBlueprintParentClass"), TempPath, IniConfigFile) && !TempPath.IsEmpty())
		{
			Settings->DefaultStageBlueprintParentClass = TSoftClassPtr<AStage>(FSoftObjectPath(TempPath));
		}
		if (GConfig->GetString(*SectionName, TEXT("DefaultEntityActorBlueprintParentClass"), TempPath, IniConfigFile) && !TempPath.IsEmpty())
		{
			Settings->DefaultEntityActorBlueprintParentClass = TSoftClassPtr<AStageEntity>(FSoftObjectPath(TempPath));
		}
		if (GConfig->GetString(*SectionName, TEXT("DefaultEntityComponentBlueprintParentClass"), TempPath, IniConfigFile) && !TempPath.IsEmpty())
		{
			Settings->DefaultEntityComponentBlueprintParentClass = TSoftClassPtr<UStageEntityComponent>(FSoftObjectPath(TempPath));
		}

		UE_LOG(LogTemp, Log, TEXT("StageEditor: Loaded settings from legacy INI: %s"), *IniConfigFile);

		// Migrate to new JSON format
		SaveSettingsToJson(JsonConfigPath);
		UE_LOG(LogTemp, Log, TEXT("StageEditor: Migrated settings to JSON format"));
	}
}

void SStageEditorPanel::SaveAssetCreationSettingsToConfig()
{
	if (!CreationSettings.IsValid())
	{
		return;
	}

	FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
	if (!Settings)
	{
		return;
	}

	// Use user-local config file in Saved/Config/ directory (not tracked by version control)
	// This prevents conflicts in multi-user collaboration scenarios
	const FString UserConfigFile = FPaths::GeneratedConfigDir() / TEXT("StageEditorUserSettings.ini");
	const FString SectionName = TEXT("AssetCreationSettings");

	UE_LOG(LogTemp, Log, TEXT("StageEditor: Saving to config file: %s"), *UserConfigFile);

	// Ensure the config directory exists
	FString ConfigDir = FPaths::GetPath(UserConfigFile);
	if (!IFileManager::Get().DirectoryExists(*ConfigDir))
	{
		IFileManager::Get().MakeDirectory(*ConfigDir, true);
		UE_LOG(LogTemp, Log, TEXT("StageEditor: Created config directory: %s"), *ConfigDir);
	}

	// Build INI content manually for reliability (GConfig->Flush may not work for new files)
	FString IniContent;
	IniContent += FString::Printf(TEXT("[%s]\r\n"), *SectionName);
	IniContent += FString::Printf(TEXT("bIsCustomStageAssetFolderPath=%s\r\n"), Settings->bIsCustomStageAssetFolderPath ? TEXT("True") : TEXT("False"));
	IniContent += FString::Printf(TEXT("bIsCustomEntityActorAssetPath=%s\r\n"), Settings->bIsCustomEntityActorAssetPath ? TEXT("True") : TEXT("False"));
	IniContent += FString::Printf(TEXT("bIsCustomEntityComponentAssetPath=%s\r\n"), Settings->bIsCustomEntityComponentAssetPath ? TEXT("True") : TEXT("False"));
	IniContent += FString::Printf(TEXT("bIsCustomDataLayerAssetPath=%s\r\n"), Settings->bIsCustomDataLayerAssetPath ? TEXT("True") : TEXT("False"));
	IniContent += FString::Printf(TEXT("StageAssetFolderPath=%s\r\n"), *Settings->StageAssetFolderPath.Path);
	IniContent += FString::Printf(TEXT("EntityActorAssetFolderPath=%s\r\n"), *Settings->EntityActorAssetFolderPath.Path);
	IniContent += FString::Printf(TEXT("EntityComponentAssetFolderPath=%s\r\n"), *Settings->EntityComponentAssetFolderPath.Path);
	IniContent += FString::Printf(TEXT("DataLayerAssetFolderPath=%s\r\n"), *Settings->DataLayerAssetFolderPath.Path);
	IniContent += FString::Printf(TEXT("DefaultStageBlueprintParentClass=%s\r\n"), *Settings->DefaultStageBlueprintParentClass.ToString());
	IniContent += FString::Printf(TEXT("DefaultEntityActorBlueprintParentClass=%s\r\n"), *Settings->DefaultEntityActorBlueprintParentClass.ToString());
	IniContent += FString::Printf(TEXT("DefaultEntityComponentBlueprintParentClass=%s\r\n"), *Settings->DefaultEntityComponentBlueprintParentClass.ToString());

	// Write directly to file (bypassing GConfig which may not work for new files)
	if (FFileHelper::SaveStringToFile(IniContent, *UserConfigFile))
	{
		UE_LOG(LogTemp, Log, TEXT("StageEditor: Successfully saved AssetCreationSettings to: %s"), *UserConfigFile);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("StageEditor: Failed to write config file: %s"), *UserConfigFile);
	}
}

FReply SStageEditorPanel::OnOpenSettingsClicked()
{
	// If window already exists and is valid, bring it to front
	if (SettingsWindow.IsValid())
	{
		TSharedPtr<SWindow> ExistingWindow = SettingsWindow.Pin();
		if (ExistingWindow.IsValid())
		{
			ExistingWindow->BringToFront();
			return FReply::Handled();
		}
	}

	// Cache current settings for Cancel functionality
	if (CreationSettings.IsValid())
	{
		FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
		if (Settings)
		{
			CachedCreationSettings = *Settings;
		}
	}

	// Check if project config exists
	bool bProjectConfigExists = FPaths::FileExists(GetProjectConfigFilePath());

	// Create a new settings window with full feature set
	TSharedRef<SWindow> NewWindow = SNew(SWindow)
		.Title(LOCTEXT("SettingsWindowTitle", "Asset Creation Settings"))
		.ClientSize(FVector2D(550, 0))
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

				// Config Source Selection
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 10)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
					.Padding(8)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ConfigSource", "Config Source:"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(15, 0, 0, 0)
						.VAlign(VAlign_Center)
						[
							SNew(SCheckBox)
							.Style(FAppStyle::Get(), "RadioButton")
							.IsChecked_Lambda([this]() {
								return CurrentConfigSource == EStageEditorConfigSource::Personal
									? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) {
								if (NewState == ECheckBoxState::Checked)
								{
									OnConfigSourceChanged(EStageEditorConfigSource::Personal);
								}
							})
							[
								SNew(STextBlock)
								.Text(LOCTEXT("PersonalSettings", "Personal"))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(15, 0, 0, 0)
						.VAlign(VAlign_Center)
						[
							SNew(SCheckBox)
							.Style(FAppStyle::Get(), "RadioButton")
							.IsEnabled(bProjectConfigExists)
							.IsChecked_Lambda([this]() {
								return CurrentConfigSource == EStageEditorConfigSource::Project
									? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) {
								if (NewState == ECheckBoxState::Checked)
								{
									OnConfigSourceChanged(EStageEditorConfigSource::Project);
								}
							})
							[
								SNew(STextBlock)
								.Text(bProjectConfigExists
									? LOCTEXT("ProjectSettingsRO", "Project (Read-only)")
									: LOCTEXT("ProjectSettingsNA", "Project (Not Available)"))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(10, 0, 0, 0)
						.VAlign(VAlign_Center)
						[
							SNew(SButton)
							.Text(LOCTEXT("CopyToPersonal", "Copy to Personal"))
							.IsEnabled(bProjectConfigExists)
							.OnClicked(this, &SStageEditorPanel::OnCopyToPersonalClicked)
							.ToolTipText(LOCTEXT("CopyToPersonalTip", "Copy project settings to your personal settings for customization"))
						]
					]
				]

				// Property Editor
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SettingsDetailsView->GetWidget().ToSharedRef()
				]

				// Button Row
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 10, 0, 0)
				[
					SNew(SHorizontalBox)
					// Left side: Import/Export/Reset
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0, 0, 5, 0)
						[
							SNew(SButton)
							.Text(LOCTEXT("ImportSettings", "Import"))
							.OnClicked(this, &SStageEditorPanel::OnImportSettingsClicked)
							.ToolTipText(LOCTEXT("ImportTip", "Import settings from a JSON file"))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0, 0, 5, 0)
						[
							SNew(SButton)
							.Text(LOCTEXT("ExportSettings", "Export"))
							.OnClicked(this, &SStageEditorPanel::OnExportSettingsClicked)
							.ToolTipText(LOCTEXT("ExportTip", "Export current settings to a JSON file"))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(LOCTEXT("ResetSettings", "Reset"))
							.OnClicked(this, &SStageEditorPanel::OnResetToDefaultsClicked)
							.ToolTipText(LOCTEXT("ResetTip", "Reset all settings to default values"))
						]
					]
					// Spacer
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SSpacer)
					]
					// Right side: Save/Cancel
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(0, 0, 5, 0)
						[
							SNew(SButton)
							.Text(LOCTEXT("SaveSettings", "Save"))
							.IsEnabled_Lambda([this]() { return IsConfigEditable(); })
							.OnClicked(this, &SStageEditorPanel::OnSaveSettingsClicked)
							.ToolTipText_Lambda([this]() {
								return IsConfigEditable()
									? LOCTEXT("SaveTip", "Save settings to personal configuration")
									: LOCTEXT("SaveDisabledTip", "Cannot save project settings. Use Export or Copy to Personal.");
							})
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.Text(LOCTEXT("CancelSettings", "Cancel"))
							.OnClicked(this, &SStageEditorPanel::OnCancelSettingsClicked)
						]
					]
				]
			]
		];

	// Store weak reference
	SettingsWindow = NewWindow;

	// Add as a standalone window
	FSlateApplication::Get().AddWindow(NewWindow);

	return FReply::Handled();
}

void SStageEditorPanel::CloseSettingsWindow()
{
	if (SettingsWindow.IsValid())
	{
		TSharedPtr<SWindow> Window = SettingsWindow.Pin();
		if (Window.IsValid())
		{
			Window->RequestDestroyWindow();
		}
	}
	SettingsWindow.Reset();
}

FReply SStageEditorPanel::OnSaveSettingsClicked()
{
	// Update Controller's DataLayer path before saving
	if (Controller.IsValid() && CreationSettings.IsValid())
	{
		FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
		if (Settings)
		{
			// Update Controller's DataLayer asset folder path
			FString DataLayerPath;
			if (Settings->bIsCustomDataLayerAssetPath)
			{
				FString PhysicalPath = Settings->DataLayerAssetFolderPath.Path;
				FString ProjectContentDir = FPaths::ProjectContentDir();

				if (PhysicalPath.StartsWith(ProjectContentDir))
				{
					FString RelativePath = PhysicalPath.RightChop(ProjectContentDir.Len());
					DataLayerPath = TEXT("/Game/") + RelativePath;
				}
				else
				{
					DataLayerPath = PhysicalPath;
				}
			}
			else
			{
				DataLayerPath = TEXT("/StageEditor/DataLayers");
			}
			Controller->SetDataLayerAssetFolderPath(DataLayerPath);
		}
	}

	// Save settings to JSON file (only Personal settings can be saved)
	if (!IsConfigEditable())
	{
		FNotificationInfo Info(LOCTEXT("CannotSaveProject", "Cannot save project settings. Use Export or Copy to Personal."));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return FReply::Handled();
	}

	bool bSuccess = SaveSettingsToJson(GetPersonalConfigFilePath());

	// Show notification
	if (bSuccess)
	{
		FNotificationInfo Info(LOCTEXT("SettingsSaved", "Settings saved successfully"));
		Info.ExpireDuration = 2.0f;
		Info.bUseSuccessFailIcons = true;
		FSlateNotificationManager::Get().AddNotification(Info);

		// Close window
		CloseSettingsWindow();
	}
	else
	{
		FNotificationInfo Info(LOCTEXT("SaveFailed", "Failed to save settings"));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	return FReply::Handled();
}

FReply SStageEditorPanel::OnCancelSettingsClicked()
{
	// Restore cached settings
	if (CreationSettings.IsValid())
	{
		FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
		if (Settings)
		{
			*Settings = CachedCreationSettings;

			// Also restore Controller's DataLayer path
			if (Controller.IsValid())
			{
				FString DataLayerPath;
				if (CachedCreationSettings.bIsCustomDataLayerAssetPath)
				{
					FString PhysicalPath = CachedCreationSettings.DataLayerAssetFolderPath.Path;
					FString ProjectContentDir = FPaths::ProjectContentDir();

					if (PhysicalPath.StartsWith(ProjectContentDir))
					{
						FString RelativePath = PhysicalPath.RightChop(ProjectContentDir.Len());
						DataLayerPath = TEXT("/Game/") + RelativePath;
					}
					else
					{
						DataLayerPath = PhysicalPath;
					}
				}
				else
				{
					DataLayerPath = TEXT("/StageEditor/DataLayers");
				}
				Controller->SetDataLayerAssetFolderPath(DataLayerPath);
			}
		}
	}

	// Close window without saving
	CloseSettingsWindow();

	return FReply::Handled();
}

// ---- Config Source Management (Phase 19.3) ----

FString SStageEditorPanel::GetPersonalConfigFilePath() const
{
	return FPaths::GeneratedConfigDir() / TEXT("StageEditorUserSettings.json");
}

FString SStageEditorPanel::GetProjectConfigFilePath() const
{
	return FPaths::ProjectConfigDir() / TEXT("StageEditor") / TEXT("StageEditorSettings.json");
}

bool SStageEditorPanel::LoadSettingsFromJson(const FString& FilePath)
{
	if (!FPaths::FileExists(FilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("StageEditor: Config file not found: %s"), *FilePath);
		return false;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("StageEditor: Failed to read config file: %s"), *FilePath);
		return false;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("StageEditor: Failed to parse JSON from: %s"), *FilePath);
		return false;
	}

	if (!CreationSettings.IsValid())
	{
		return false;
	}

	FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
	if (!Settings)
	{
		return false;
	}

	// Load boolean flags
	Settings->bIsCustomStageAssetFolderPath = JsonObject->GetBoolField(TEXT("bIsCustomStageAssetFolderPath"));
	Settings->bIsCustomEntityActorAssetPath = JsonObject->GetBoolField(TEXT("bIsCustomEntityActorAssetPath"));
	Settings->bIsCustomEntityComponentAssetPath = JsonObject->GetBoolField(TEXT("bIsCustomEntityComponentAssetPath"));
	Settings->bIsCustomDataLayerAssetPath = JsonObject->GetBoolField(TEXT("bIsCustomDataLayerAssetPath"));

	// Load folder paths
	Settings->StageAssetFolderPath.Path = JsonObject->GetStringField(TEXT("StageAssetFolderPath"));
	Settings->EntityActorAssetFolderPath.Path = JsonObject->GetStringField(TEXT("EntityActorAssetFolderPath"));
	Settings->EntityComponentAssetFolderPath.Path = JsonObject->GetStringField(TEXT("EntityComponentAssetFolderPath"));
	Settings->DataLayerAssetFolderPath.Path = JsonObject->GetStringField(TEXT("DataLayerAssetFolderPath"));

	// Load parent class paths
	FString TempPath;
	if (JsonObject->TryGetStringField(TEXT("DefaultStageBlueprintParentClass"), TempPath) && !TempPath.IsEmpty())
	{
		Settings->DefaultStageBlueprintParentClass = TSoftClassPtr<AStage>(FSoftObjectPath(TempPath));
	}
	if (JsonObject->TryGetStringField(TEXT("DefaultEntityActorBlueprintParentClass"), TempPath) && !TempPath.IsEmpty())
	{
		Settings->DefaultEntityActorBlueprintParentClass = TSoftClassPtr<AStageEntity>(FSoftObjectPath(TempPath));
	}
	if (JsonObject->TryGetStringField(TEXT("DefaultEntityComponentBlueprintParentClass"), TempPath) && !TempPath.IsEmpty())
	{
		Settings->DefaultEntityComponentBlueprintParentClass = TSoftClassPtr<UStageEntityComponent>(FSoftObjectPath(TempPath));
	}

	UE_LOG(LogTemp, Log, TEXT("StageEditor: Loaded settings from JSON: %s"), *FilePath);
	return true;
}

bool SStageEditorPanel::SaveSettingsToJson(const FString& FilePath)
{
	if (!CreationSettings.IsValid())
	{
		return false;
	}

	FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
	if (!Settings)
	{
		return false;
	}

	// Ensure directory exists
	FString ConfigDir = FPaths::GetPath(FilePath);
	if (!IFileManager::Get().DirectoryExists(*ConfigDir))
	{
		IFileManager::Get().MakeDirectory(*ConfigDir, true);
	}

	// Build JSON object
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

	// Boolean flags
	JsonObject->SetBoolField(TEXT("bIsCustomStageAssetFolderPath"), Settings->bIsCustomStageAssetFolderPath);
	JsonObject->SetBoolField(TEXT("bIsCustomEntityActorAssetPath"), Settings->bIsCustomEntityActorAssetPath);
	JsonObject->SetBoolField(TEXT("bIsCustomEntityComponentAssetPath"), Settings->bIsCustomEntityComponentAssetPath);
	JsonObject->SetBoolField(TEXT("bIsCustomDataLayerAssetPath"), Settings->bIsCustomDataLayerAssetPath);

	// Folder paths
	JsonObject->SetStringField(TEXT("StageAssetFolderPath"), Settings->StageAssetFolderPath.Path);
	JsonObject->SetStringField(TEXT("EntityActorAssetFolderPath"), Settings->EntityActorAssetFolderPath.Path);
	JsonObject->SetStringField(TEXT("EntityComponentAssetFolderPath"), Settings->EntityComponentAssetFolderPath.Path);
	JsonObject->SetStringField(TEXT("DataLayerAssetFolderPath"), Settings->DataLayerAssetFolderPath.Path);

	// Parent class paths
	JsonObject->SetStringField(TEXT("DefaultStageBlueprintParentClass"), Settings->DefaultStageBlueprintParentClass.ToString());
	JsonObject->SetStringField(TEXT("DefaultEntityActorBlueprintParentClass"), Settings->DefaultEntityActorBlueprintParentClass.ToString());
	JsonObject->SetStringField(TEXT("DefaultEntityComponentBlueprintParentClass"), Settings->DefaultEntityComponentBlueprintParentClass.ToString());

	// Serialize to string
	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	if (!FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer))
	{
		UE_LOG(LogTemp, Error, TEXT("StageEditor: Failed to serialize settings to JSON"));
		return false;
	}

	// Write to file
	if (!FFileHelper::SaveStringToFile(JsonString, *FilePath))
	{
		UE_LOG(LogTemp, Error, TEXT("StageEditor: Failed to write JSON file: %s"), *FilePath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("StageEditor: Saved settings to JSON: %s"), *FilePath);
	return true;
}

void SStageEditorPanel::OnConfigSourceChanged(EStageEditorConfigSource NewSource)
{
	if (CurrentConfigSource == NewSource)
	{
		return;
	}

	CurrentConfigSource = NewSource;

	// Load settings from the new source
	FString ConfigPath = (NewSource == EStageEditorConfigSource::Personal)
		? GetPersonalConfigFilePath()
		: GetProjectConfigFilePath();

	if (FPaths::FileExists(ConfigPath))
	{
		LoadSettingsFromJson(ConfigPath);
	}
	else if (NewSource == EStageEditorConfigSource::Personal)
	{
		// Personal config doesn't exist, reset to defaults
		if (CreationSettings.IsValid())
		{
			FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
			if (Settings)
			{
				*Settings = FAssetCreationSettings();
			}
		}
	}

	// Update the details view
	if (SettingsDetailsView.IsValid())
	{
		SettingsDetailsView->GetDetailsView()->ForceRefresh();
	}
}

FReply SStageEditorPanel::OnCopyToPersonalClicked()
{
	FString ProjectPath = GetProjectConfigFilePath();
	FString PersonalPath = GetPersonalConfigFilePath();

	if (!FPaths::FileExists(ProjectPath))
	{
		FNotificationInfo Info(LOCTEXT("NoProjectConfig", "No project configuration found"));
		Info.ExpireDuration = 3.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return FReply::Handled();
	}

	// Load project settings
	if (LoadSettingsFromJson(ProjectPath))
	{
		// Save to personal
		if (SaveSettingsToJson(PersonalPath))
		{
			CurrentConfigSource = EStageEditorConfigSource::Personal;

			FNotificationInfo Info(LOCTEXT("CopiedToPersonal", "Project settings copied to personal settings"));
			Info.ExpireDuration = 2.0f;
			Info.bUseSuccessFailIcons = true;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
	}

	return FReply::Handled();
}

FReply SStageEditorPanel::OnImportSettingsClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	TArray<FString> OutFiles;
	const FString DefaultPath = FPaths::ProjectDir();
	const FString FileTypes = TEXT("JSON Files (*.json)|*.json");

	bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Import Settings"),
		DefaultPath,
		TEXT(""),
		FileTypes,
		EFileDialogFlags::None,
		OutFiles
	);

	if (bOpened && OutFiles.Num() > 0)
	{
		if (LoadSettingsFromJson(OutFiles[0]))
		{
			FNotificationInfo Info(LOCTEXT("SettingsImported", "Settings imported successfully"));
			Info.ExpireDuration = 2.0f;
			Info.bUseSuccessFailIcons = true;
			FSlateNotificationManager::Get().AddNotification(Info);

			// Refresh the details view
			if (SettingsDetailsView.IsValid())
			{
				SettingsDetailsView->GetDetailsView()->ForceRefresh();
			}
		}
		else
		{
			FNotificationInfo Info(LOCTEXT("ImportFailed", "Failed to import settings"));
			Info.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
	}

	return FReply::Handled();
}

FReply SStageEditorPanel::OnExportSettingsClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}

	TArray<FString> OutFiles;
	const FString DefaultPath = FPaths::ProjectDir();
	const FString DefaultFileName = TEXT("StageEditorSettings.json");
	const FString FileTypes = TEXT("JSON Files (*.json)|*.json");

	bool bSaved = DesktopPlatform->SaveFileDialog(
		FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
		TEXT("Export Settings"),
		DefaultPath,
		DefaultFileName,
		FileTypes,
		EFileDialogFlags::None,
		OutFiles
	);

	if (bSaved && OutFiles.Num() > 0)
	{
		if (SaveSettingsToJson(OutFiles[0]))
		{
			FNotificationInfo Info(LOCTEXT("SettingsExported", "Settings exported successfully"));
			Info.ExpireDuration = 2.0f;
			Info.bUseSuccessFailIcons = true;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
		else
		{
			FNotificationInfo Info(LOCTEXT("ExportFailed", "Failed to export settings"));
			Info.ExpireDuration = 3.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
	}

	return FReply::Handled();
}

FReply SStageEditorPanel::OnResetToDefaultsClicked()
{
	if (!CreationSettings.IsValid())
	{
		return FReply::Handled();
	}

	// Confirm with user
	EAppReturnType::Type Result = FMessageDialog::Open(
		EAppMsgType::YesNo,
		LOCTEXT("ConfirmReset", "Are you sure you want to reset all settings to default values?")
	);

	if (Result == EAppReturnType::Yes)
	{
		FAssetCreationSettings* Settings = (FAssetCreationSettings*)CreationSettings->GetStructMemory();
		if (Settings)
		{
			*Settings = FAssetCreationSettings();

			// Refresh the details view
			if (SettingsDetailsView.IsValid())
			{
				SettingsDetailsView->GetDetailsView()->ForceRefresh();
			}

			FNotificationInfo Info(LOCTEXT("SettingsReset", "Settings reset to defaults"));
			Info.ExpireDuration = 2.0f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
	}

	return FReply::Handled();
}

bool SStageEditorPanel::IsConfigEditable() const
{
	// Personal settings are always editable
	// Project settings are read-only (can only be modified via Export)
	return CurrentConfigSource == EStageEditorConfigSource::Personal;
}

#undef LOCTEXT_NAMESPACE
#pragma endregion Private Helpers
