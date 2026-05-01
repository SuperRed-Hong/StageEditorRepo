// Copyright Stage Editor Plugin. All Rights Reserved.

#include "DataLayerSync/SStageDataLayerOutliner.h"
#include "DataLayerSync/SDataLayerImportPreviewDialog.h"
#include "DataLayerSync/SBatchImportPreviewDialog.h"
#include "DataLayerSync/SSyncPreviewDialog.h"
#include "DataLayerSync/StageDataLayerNameParser.h"
#include "DataLayerSync/StageDataLayerMode.h"
#include "DataLayerSync/StageDataLayerColumns.h"
#include "DataLayerSync/DataLayerImporter.h"
#include "DataLayerSync/DataLayerSynchronizer.h"
#include "DataLayerSync/DataLayerSyncStatus.h"
#include "DataLayerSync/DataLayerSyncStatusCache.h"

#include "DataLayer/DataLayerEditorSubsystem.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "SceneOutlinerModule.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "SSceneOutliner.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

#define LOCTEXT_NAMESPACE "StageDataLayerOutliner"

//----------------------------------------------------------------
// Construction & Destruction
//----------------------------------------------------------------

void SStageDataLayerOutliner::Construct(const FArguments& InArgs)
{
	// Get the editor world
	RepresentingWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;

	// Subscribe to DataLayer events for synchronization
	SubscribeToEvents();

	// Initialize the custom SceneOutliner and build UI
	InitializeOutliner();
	RebuildUI();
}

SStageDataLayerOutliner::~SStageDataLayerOutliner()
{
	UnsubscribeFromEvents();
}

//----------------------------------------------------------------
// Initialization
//----------------------------------------------------------------

void SStageDataLayerOutliner::InitializeOutliner()
{
	// Get the SceneOutliner module
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");

	// Configure the SceneOutliner initialization options
	FSceneOutlinerInitializationOptions InitOptions;
	InitOptions.bShowHeaderRow = true;
	InitOptions.bShowSearchBox = true;
	InitOptions.bShowCreateNewFolder = false;
	InitOptions.bFocusSearchBoxWhenOpened = false;

	// Create mode factory - this creates our custom mode when SceneOutliner initializes
	// Note: ModeFactory delegate returns raw pointer (ISceneOutlinerMode*), SceneOutliner takes ownership
	InitOptions.ModeFactory = FCreateSceneOutlinerMode::CreateLambda([this](SSceneOutliner* Outliner) -> ISceneOutlinerMode*
	{
		FStageDataLayerModeParams Params(Outliner, this, RepresentingWorld);
		FStageDataLayerMode* NewMode = new FStageDataLayerMode(Params);
		DataLayerMode = NewMode;
		return NewMode;
	});

	// Register columns (in display order from left to right)
	// Column order: Visibility | LoadedInEditor | SyncStatus | Label (FillWidth) | SUID | Actions

	// Visibility column (eye icon) - leftmost gutter column
	InitOptions.ColumnMap.Add(
		FStageDataLayerVisibilityColumn::GetID(),
		FSceneOutlinerColumnInfo(
			ESceneOutlinerColumnVisibility::Visible,
			0, // Leftmost
			FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& Outliner)
			{
				return TSharedRef<ISceneOutlinerColumn>(MakeShared<FStageDataLayerVisibilityColumn>(Outliner));
			}),
			false // Cannot be hidden
		)
	);

	// LoadedInEditor column (checkbox)
	InitOptions.ColumnMap.Add(
		FStageDataLayerLoadedInEditorColumn::GetID(),
		FSceneOutlinerColumnInfo(
			ESceneOutlinerColumnVisibility::Visible,
			1,
			FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& Outliner)
			{
				return TSharedRef<ISceneOutlinerColumn>(MakeShared<FStageDataLayerLoadedInEditorColumn>(Outliner));
			}),
			true // Can be hidden
		)
	);

	// SyncStatus column - shows sync state icons (before Label for better visibility)
	InitOptions.ColumnMap.Add(
		FStageDataLayerSyncStatusColumn::GetID(),
		FSceneOutlinerColumnInfo(
			ESceneOutlinerColumnVisibility::Visible,
			2,
			FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& Outliner)
			{
				return TSharedRef<ISceneOutlinerColumn>(MakeShared<FStageDataLayerSyncStatusColumn>(Outliner));
			}),
			true // bCanBeHidden
		)
	);

	// Label column (built-in) - shows DataLayer name with icon, uses FillWidth to auto-fill remaining space
	InitOptions.ColumnMap.Add(
		FSceneOutlinerBuiltInColumnTypes::Label(),
		FSceneOutlinerColumnInfo(
			ESceneOutlinerColumnVisibility::Visible,
			3,
			FCreateSceneOutlinerColumn(),
			true,  // bCanBeHidden
			1.0f   // FillSize - fills remaining space (100%)
		)
	);

	// SUID column - shows Stage Unique ID (S:X or A:X.Y)
	InitOptions.ColumnMap.Add(
		FStageDataLayerSUIDColumn::GetID(),
		FSceneOutlinerColumnInfo(
			ESceneOutlinerColumnVisibility::Visible,
			4,
			FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& Outliner)
			{
				return TSharedRef<ISceneOutlinerColumn>(MakeShared<FStageDataLayerSUIDColumn>(Outliner));
			}),
			true,  // bCanBeHidden
			0.2f   // FillSize - 10% of total width (replaces ManualWidth)
		)
	);

	// Actions column - shows Rename/Import/Sync buttons
	InitOptions.ColumnMap.Add(
		FStageDataLayerActionsColumn::GetID(),
		FSceneOutlinerColumnInfo(
			ESceneOutlinerColumnVisibility::Visible,
			5,
			FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& Outliner)
			{
				return TSharedRef<ISceneOutlinerColumn>(MakeShared<FStageDataLayerActionsColumn>(Outliner));
			}),
			true,  // bCanBeHidden
			0.4f   // FillSize - 20% of total width (replaces ManualWidth)
		)
	);

	// Create the SceneOutliner widget - returns TSharedRef<ISceneOutliner>
	TSharedRef<ISceneOutliner> OutlinerRef = SceneOutlinerModule.CreateSceneOutliner(InitOptions);

	// Cast to SSceneOutliner for storage
	SceneOutliner = StaticCastSharedRef<SSceneOutliner>(OutlinerRef);
}

void SStageDataLayerOutliner::SubscribeToEvents()
{
	if (UDataLayerEditorSubsystem* Subsystem = UDataLayerEditorSubsystem::Get())
	{
		OnDataLayerChangedHandle = Subsystem->OnDataLayerChanged().AddSP(
			this, &SStageDataLayerOutliner::OnDataLayerChanged);

		OnActorDataLayersChangedHandle = Subsystem->OnActorDataLayersChanged().AddSP(
			this, &SStageDataLayerOutliner::OnActorDataLayersChanged);
	}

	// Subscribe to map change events to update when switching levels
	OnMapChangedHandle = FEditorDelegates::MapChange.AddSP(
		this, &SStageDataLayerOutliner::OnMapChanged);
}

void SStageDataLayerOutliner::UnsubscribeFromEvents()
{
	if (UDataLayerEditorSubsystem* Subsystem = UDataLayerEditorSubsystem::Get())
	{
		if (OnDataLayerChangedHandle.IsValid())
		{
			Subsystem->OnDataLayerChanged().Remove(OnDataLayerChangedHandle);
			OnDataLayerChangedHandle.Reset();
		}

		if (OnActorDataLayersChangedHandle.IsValid())
		{
			Subsystem->OnActorDataLayersChanged().Remove(OnActorDataLayersChangedHandle);
			OnActorDataLayersChangedHandle.Reset();
		}
	}

	// Unsubscribe from map change events
	if (OnMapChangedHandle.IsValid())
	{
		FEditorDelegates::MapChange.Remove(OnMapChangedHandle);
		OnMapChangedHandle.Reset();
	}
}

//----------------------------------------------------------------
// Public API
//----------------------------------------------------------------

void SStageDataLayerOutliner::RefreshTree()
{
	if (SceneOutliner.IsValid())
	{
		SceneOutliner->FullRefresh();
	}
}

void SStageDataLayerOutliner::SyncToDataLayer(const UDataLayerInstance* DataLayer)
{
	if (!DataLayer || !SceneOutliner.IsValid())
	{
		return;
	}

	// TODO: Implement selection sync when needed
	// For now just refresh to ensure the DataLayer is visible
	RefreshTree();
}

TArray<UDataLayerInstance*> SStageDataLayerOutliner::GetSelectedDataLayers() const
{
	if (DataLayerMode)
	{
		return DataLayerMode->GetSelectedDataLayers();
	}

	// Fallback to cached selection
	TArray<UDataLayerInstance*> Result;
	for (const auto& WeakDataLayer : SelectedDataLayersSet)
	{
		if (WeakDataLayer.IsValid())
		{
			Result.Add(const_cast<UDataLayerInstance*>(WeakDataLayer.Get()));
		}
	}
	return Result;
}

//----------------------------------------------------------------
// Event Handlers
//----------------------------------------------------------------

void SStageDataLayerOutliner::OnDataLayerChanged(
	const EDataLayerAction Action,
	const TWeakObjectPtr<const UDataLayerInstance>& ChangedDataLayer,
	const FName& ChangedProperty)
{
	// Invalidate relevant cache entries to ensure fresh status detection
	const UDataLayerInstance* Instance = ChangedDataLayer.Get();
	if (Instance)
	{
		// Invalidate the changed DataLayer's cache
		if (const UDataLayerAsset* Asset = Instance->GetAsset())
		{
			FDataLayerSyncStatusCache::Get().InvalidateCache(Asset);
		}

		// Also invalidate parent DataLayer (hierarchy changes affect parent's status)
		if (const UDataLayerInstance* Parent = Instance->GetParent())
		{
			if (const UDataLayerAsset* ParentAsset = Parent->GetAsset())
			{
				FDataLayerSyncStatusCache::Get().InvalidateCache(ParentAsset);
			}
		}
	}
	else
	{
		// Cannot determine specific change, invalidate all
		FDataLayerSyncStatusCache::Get().InvalidateAll();
	}

	// Refresh the outliner to show updated sync status
	RefreshTree();
}

void SStageDataLayerOutliner::OnActorDataLayersChanged(const TWeakObjectPtr<AActor>& ChangedActor)
{
	// Invalidate cache for affected DataLayers
	AActor* Actor = ChangedActor.Get();
	if (Actor && Actor->HasDataLayers())
	{
		for (const UDataLayerInstance* Instance : Actor->GetDataLayerInstances())
		{
			if (!Instance) continue;

			if (const UDataLayerAsset* Asset = Instance->GetAsset())
			{
				FDataLayerSyncStatusCache::Get().InvalidateCache(Asset);
			}

			// Also invalidate parent DataLayer (Stage level)
			if (const UDataLayerInstance* Parent = Instance->GetParent())
			{
				if (const UDataLayerAsset* ParentAsset = Parent->GetAsset())
				{
					FDataLayerSyncStatusCache::Get().InvalidateCache(ParentAsset);
				}
			}
		}
	}

	// Refresh the outliner to show updated sync status
	RefreshTree();
}

void SStageDataLayerOutliner::OnMapChanged(uint32 MapChangeFlags)
{
	// Update the representing world when map changes
	UWorld* NewWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;

	if (NewWorld != RepresentingWorld.Get())
	{
		RepresentingWorld = NewWorld;

		// Clear cached selection as it may reference old world's DataLayers
		SelectedDataLayersSet.Empty();

		// Reinitialize the outliner with the new world and rebuild UI
		InitializeOutliner();
		RebuildUI();
	}
}

void SStageDataLayerOutliner::OnSelectionChanged(TSet<TWeakObjectPtr<const UDataLayerInstance>>& InSelectedDataLayersSet)
{
	SelectedDataLayersSet = InSelectedDataLayersSet;
	// Toolbar buttons will query this set via CanImportSelected()
}

//----------------------------------------------------------------
// Toolbar Actions
//----------------------------------------------------------------

FReply SStageDataLayerOutliner::OnRefreshClicked()
{
	// Step 1: Invalidate all cache to force re-detection
	FDataLayerSyncStatusCache::Get().InvalidateAll();

	// Step 2: Refresh the outliner display (which will re-query sync status)
	if (SceneOutliner.IsValid())
	{
		SceneOutliner->FullRefresh();
	}

	UE_LOG(LogTemp, Log, TEXT("Refresh: Cache invalidated and outliner refreshed"));

	return FReply::Handled();
}

FReply SStageDataLayerOutliner::OnSyncAllClicked()
{
	// Show sync preview dialog
	// The dialog will:
	// 1. Collect all OutOfSync DataLayers
	// 2. Show naming warnings for any non-conforming DataLayers
	// 3. Allow user to confirm or cancel the sync operation
	bool bSyncExecuted = SSyncPreviewDialog::ShowDialog();

	// Refresh the outliner display after dialog closes
	if (SceneOutliner.IsValid())
	{
		SceneOutliner->FullRefresh();
	}

	return FReply::Handled();
}

bool SStageDataLayerOutliner::CanSyncAll() const
{
	// 按钮常驻可点击
	// 点击时会先刷新缓存，然后执行同步，最后刷新 Outliner 显示
	// 这样用户可以随时点击来手动触发全局刷新
	return true;
}

FReply SStageDataLayerOutliner::OnImportSelectedClicked()
{
	TArray<UDataLayerAsset*> ImportableAssets = GetSelectedImportableDataLayerAssets();

	if (ImportableAssets.Num() == 0)
	{
		return FReply::Handled();
	}

	if (ImportableAssets.Num() == 1)
	{
		// Single selection - use single-item preview dialog
		SDataLayerImportPreviewDialog::ShowDialog(ImportableAssets[0]);
	}
	else
	{
		// Multiple selection - use batch preview dialog
		SBatchImportPreviewDialog::ShowDialog(ImportableAssets);
	}

	return FReply::Handled();
}

bool SStageDataLayerOutliner::CanImportSelected() const
{
	return GetSelectedImportableDataLayerAssets().Num() > 0;
}

TArray<UDataLayerAsset*> SStageDataLayerOutliner::GetSelectedImportableDataLayerAssets() const
{
	TArray<UDataLayerAsset*> Result;

	// Check selected DataLayers for importable Stage DataLayers
	for (const auto& WeakDataLayer : SelectedDataLayersSet)
	{
		if (!WeakDataLayer.IsValid())
		{
			continue;
		}

		const UDataLayerInstance* Instance = WeakDataLayer.Get();
		if (!Instance)
		{
			continue;
		}

		UDataLayerAsset* Asset = const_cast<UDataLayerAsset*>(Instance->GetAsset());
		if (!Asset)
		{
			continue;
		}

		// Check naming convention - only Stage-level DataLayers can be imported
		FDataLayerNameParseResult ParseResult = FStageDataLayerNameParser::Parse(Asset->GetName());
		if (ParseResult.bIsValid && ParseResult.bIsStageLayer)
		{
			// Check if not already imported
			FText ErrorMessage;
			if (FDataLayerImporter::CanImport(Asset, nullptr, ErrorMessage))
			{
				Result.Add(Asset);
			}
		}
	}

	return Result;
}

//----------------------------------------------------------------
// UI Construction
//----------------------------------------------------------------

void SStageDataLayerOutliner::RebuildUI()
{
	ChildSlot
	[
		SNew(SVerticalBox)

		// Header with title
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("StageDataLayerOutlinerTitle", "Stage DataLayer Outliner"))
			.TextStyle(FAppStyle::Get(), "LargeText")
		]

		// Toolbar with action buttons
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f)
		[
			BuildToolbar()
		]

		// Separator
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
		]

		// Custom SceneOutliner
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SceneOutliner.IsValid() ? SceneOutliner.ToSharedRef() : SNullWidget::NullWidget
		]
	];
}

TSharedRef<SWidget> SStageDataLayerOutliner::BuildToolbar()
{
	return SNew(SHorizontalBox)

		// Refresh Button - only refresh cache and UI, no sync
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("RefreshButton", "Refresh"))
			.ToolTipText(LOCTEXT("RefreshTooltip", "Refresh sync status cache and update the display without performing any sync operations"))
			.OnClicked(this, &SStageDataLayerOutliner::OnRefreshClicked)
		]

		// Sync All Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("SyncAllButton", "Sync All"))
			.ToolTipText(LOCTEXT("SyncAllTooltip", "Synchronize all out-of-sync DataLayers (refresh + sync)"))
			.OnClicked(this, &SStageDataLayerOutliner::OnSyncAllClicked)
			.IsEnabled(this, &SStageDataLayerOutliner::CanSyncAll)
		]

		// Import Selected Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("ImportSelectedButton", "Import Selected"))
			.ToolTipText(LOCTEXT("ImportSelectedTooltip", "Import selected Stage DataLayer (DL_Stage_*) as a new Stage with Acts and Entities"))
			.OnClicked(this, &SStageDataLayerOutliner::OnImportSelectedClicked)
			.IsEnabled(this, &SStageDataLayerOutliner::CanImportSelected)
		]

		// Spacer
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNullWidget::NullWidget
		]

		// Help/Info text
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NamingHint", "Use DL_Stage_<name> or DL_Act_<stage>_<name> naming"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}

#undef LOCTEXT_NAMESPACE
