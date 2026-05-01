// Copyright Stage Editor Plugin. All Rights Reserved.

#include "DataLayerSync/StageDataLayerMode.h"
#include "DataLayerSync/StageDataLayerHierarchy.h"
#include "DataLayerSync/StageDataLayerTreeItem.h"
#include "DataLayerSync/SStageDataLayerOutliner.h"

#include "DataLayer/DataLayerEditorSubsystem.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ISceneOutlinerHierarchy.h"
#include "ScopedTransaction.h"
#include "SceneOutlinerFilters.h"
#include "SceneOutlinerMenuContext.h"
#include "SSceneOutliner.h"
#include "Selection.h"
#include "ToolMenu.h"
#include "ToolMenuContext.h"
#include "ToolMenus.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerInstanceWithAsset.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

#define LOCTEXT_NAMESPACE "StageDataLayerMode"

//----------------------------------------------------------------
// Params
//----------------------------------------------------------------

FStageDataLayerModeParams::FStageDataLayerModeParams(
	SSceneOutliner* InSceneOutliner,
	SStageDataLayerOutliner* InDataLayerOutliner,
	const TWeakObjectPtr<UWorld>& InSpecifiedWorldToDisplay)
	: SpecifiedWorldToDisplay(InSpecifiedWorldToDisplay)
	, DataLayerOutliner(InDataLayerOutliner)
	, SceneOutliner(InSceneOutliner)
{
}

//----------------------------------------------------------------
// Construction & Destruction
//----------------------------------------------------------------

FStageDataLayerMode::FStageDataLayerMode(const FStageDataLayerModeParams& Params)
	: ISceneOutlinerMode(Params.SceneOutliner)
	, SpecifiedWorldToDisplay(Params.SpecifiedWorldToDisplay)
	, DataLayerOutliner(Params.DataLayerOutliner)
	, FilteredDataLayerCount(0)
{
	// Subscribe to selection events
	USelection::SelectionChangedEvent.AddRaw(this, &FStageDataLayerMode::OnLevelSelectionChanged);
	USelection::SelectObjectEvent.AddRaw(this, &FStageDataLayerMode::OnLevelSelectionChanged);

	// Get the DataLayerEditorSubsystem
	DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get();

	// Initial build
	Rebuild();

	// Register context menu
	RegisterContextMenu();
}

FStageDataLayerMode::~FStageDataLayerMode()
{
	// Unsubscribe from selection events
	USelection::SelectionChangedEvent.RemoveAll(this);
	USelection::SelectObjectEvent.RemoveAll(this);
}

//----------------------------------------------------------------
// ISceneOutlinerMode Implementation - Core
//----------------------------------------------------------------

void FStageDataLayerMode::Rebuild()
{
	// Choose which world to represent
	ChooseRepresentingWorld();

	// Reset counters
	FilteredDataLayerCount = 0;
	ApplicableDataLayers.Reset();

	// Create hierarchy if needed
	if (!Hierarchy.IsValid())
	{
		Hierarchy = CreateHierarchy();
	}
}

TUniquePtr<ISceneOutlinerHierarchy> FStageDataLayerMode::CreateHierarchy()
{
	TUniquePtr<FStageDataLayerHierarchy> NewHierarchy = FStageDataLayerHierarchy::Create(this, RepresentingWorld);
	NewHierarchy->SetHighlightSelectedDataLayers(bHighlightSelectedDataLayers);
	return NewHierarchy;
}

int32 FStageDataLayerMode::GetTypeSortPriority(const ISceneOutlinerTreeItem& Item) const
{
	if (Item.IsA<FStageDataLayerTreeItem>())
	{
		return static_cast<int32>(EItemSortOrder::DataLayer);
	}

	// Unknown type
	return -1;
}

//----------------------------------------------------------------
// ISceneOutlinerMode Implementation - Rename
//----------------------------------------------------------------

bool FStageDataLayerMode::CanRenameItem(const ISceneOutlinerTreeItem& Item) const
{
	if (const FStageDataLayerTreeItem* DataLayerItem = Item.CastTo<FStageDataLayerTreeItem>())
	{
		if (const UDataLayerInstance* DataLayer = DataLayerItem->GetDataLayer())
		{
			return !DataLayer->IsReadOnly() && DataLayer->CanEditDataLayerShortName();
		}
	}
	return false;
}

//----------------------------------------------------------------
// ISceneOutlinerMode Implementation - Status
//----------------------------------------------------------------

FText FStageDataLayerMode::GetStatusText() const
{
	const int32 TotalDataLayerCount = ApplicableDataLayers.Num();
	const int32 SelectedCount = SceneOutliner->GetSelection().Num<FStageDataLayerTreeItem>();

	if (!SceneOutliner->IsTextFilterActive())
	{
		if (SelectedCount == 0)
		{
			return FText::Format(LOCTEXT("ShowingAllFmt", "{0} DataLayers"), FText::AsNumber(FilteredDataLayerCount));
		}
		else
		{
			return FText::Format(LOCTEXT("ShowingAllSelectedFmt", "{0} DataLayers ({1} selected)"),
				FText::AsNumber(FilteredDataLayerCount), FText::AsNumber(SelectedCount));
		}
	}
	else if (FilteredDataLayerCount == 0)
	{
		return FText::Format(LOCTEXT("NoMatchingFmt", "No matching DataLayers ({0} total)"),
			FText::AsNumber(TotalDataLayerCount));
	}
	else
	{
		return FText::Format(LOCTEXT("ShowingSomeFmt", "Showing {0} of {1} DataLayers"),
			FText::AsNumber(FilteredDataLayerCount), FText::AsNumber(TotalDataLayerCount));
	}
}

//----------------------------------------------------------------
// ISceneOutlinerMode Implementation - Events
//----------------------------------------------------------------

void FStageDataLayerMode::OnItemAdded(FSceneOutlinerTreeItemPtr Item)
{
	if (FStageDataLayerTreeItem* DataLayerItem = Item->CastTo<FStageDataLayerTreeItem>())
	{
		if (!Item->Flags.bIsFilteredOut)
		{
			++FilteredDataLayerCount;
		}

		// Restore selection if this was a previously selected item
		if (SelectedDataLayersSet.Contains(DataLayerItem->GetDataLayer()))
		{
			SceneOutliner->AddToSelection({Item});
		}
	}
}

void FStageDataLayerMode::OnItemRemoved(FSceneOutlinerTreeItemPtr Item)
{
	if (FStageDataLayerTreeItem* DataLayerItem = Item->CastTo<FStageDataLayerTreeItem>())
	{
		if (!Item->Flags.bIsFilteredOut)
		{
			--FilteredDataLayerCount;
		}
	}
}

void FStageDataLayerMode::OnItemPassesFilters(const ISceneOutlinerTreeItem& Item)
{
	if (const FStageDataLayerTreeItem* DataLayerItem = Item.CastTo<FStageDataLayerTreeItem>())
	{
		ApplicableDataLayers.Add(DataLayerItem->GetDataLayer());
	}
}

void FStageDataLayerMode::OnItemSelectionChanged(
	FSceneOutlinerTreeItemPtr TreeItem,
	ESelectInfo::Type SelectionType,
	const FSceneOutlinerItemSelection& Selection)
{
	// Cache selection for restoration on refresh
	CacheSelectedItems(Selection);

	// Notify the outliner of selection change
	if (DataLayerOutliner)
	{
		// Get selected DataLayers
		TSet<TWeakObjectPtr<const UDataLayerInstance>> NewSelection;
		TArray<FSceneOutlinerTreeItemPtr> SelectedItems;
		Selection.Get(SelectedItems);
		for (const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
		{
			if (const FStageDataLayerTreeItem* DataLayerItem = Item->CastTo<FStageDataLayerTreeItem>())
			{
				if (UDataLayerInstance* DataLayer = DataLayerItem->GetDataLayer())
				{
					NewSelection.Add(DataLayer);
				}
			}
		}

		// Notify outliner for Import Selected functionality
		DataLayerOutliner->OnSelectionChanged(NewSelection);
	}
}

void FStageDataLayerMode::OnItemDoubleClick(FSceneOutlinerTreeItemPtr Item)
{
	// No double-click action for Stage DataLayer Outliner
}

FReply FStageDataLayerMode::OnKeyDown(const FKeyEvent& InKeyEvent)
{
	const FSceneOutlinerItemSelection& Selection = SceneOutliner->GetSelection();

	// F2 - Rename selected item
	if (InKeyEvent.GetKey() == EKeys::F2)
	{
		if (Selection.Num() == 1)
		{
			FSceneOutlinerTreeItemPtr ItemToRename = Selection.SelectedItems[0].Pin();
			if (ItemToRename.IsValid() && CanRenameItem(*ItemToRename) && ItemToRename->CanInteract())
			{
				SceneOutliner->SetPendingRenameItem(ItemToRename);
				SceneOutliner->ScrollItemIntoView(ItemToRename);
			}
			return FReply::Handled();
		}
	}

	// F5 - Force full refresh
	else if (InKeyEvent.GetKey() == EKeys::F5)
	{
		SceneOutliner->FullRefresh();
		return FReply::Handled();
	}

	// Ctrl+B - Find in Content Browser
	else if (InKeyEvent.GetKey() == EKeys::B && InKeyEvent.IsControlDown())
	{
		FindInContentBrowser();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void FStageDataLayerMode::SynchronizeSelection()
{
	// Synchronize the tree selection with any external selection state
	// This is called when the outliner needs to sync with an external source
}

//----------------------------------------------------------------
// ISceneOutlinerMode Implementation - Context Menu
//----------------------------------------------------------------

void FStageDataLayerMode::RegisterContextMenu()
{
	// Register the context menu in ToolMenus
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	static const FName ContextMenuName = TEXT("StageDataLayerOutliner.ContextMenu");

	if (!ToolMenus->IsMenuRegistered(ContextMenuName))
	{
		UToolMenu* Menu = ToolMenus->RegisterMenu(ContextMenuName);
		Menu->AddDynamicSection(NAME_None, FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
		{
			// Get the outliner context
			USceneOutlinerMenuContext* Context = InMenu->FindContext<USceneOutlinerMenuContext>();
			if (!Context)
			{
				return;
			}

			TSharedPtr<SSceneOutliner> SceneOutliner = Context->SceneOutliner.Pin();
			if (!SceneOutliner.IsValid())
			{
				return;
			}

			// Get selected DataLayers
			TArray<UDataLayerInstance*> SelectedDataLayers;
			TArray<FSceneOutlinerTreeItemPtr> SelectedItems;
			SceneOutliner->GetSelection().Get(SelectedItems);
			for (const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
			{
				if (const FStageDataLayerTreeItem* DataLayerItem = Item->CastTo<FStageDataLayerTreeItem>())
				{
					if (UDataLayerInstance* DataLayer = DataLayerItem->GetDataLayer())
					{
						SelectedDataLayers.Add(DataLayer);
					}
				}
			}

			if (SelectedDataLayers.Num() == 0)
			{
				return;
			}

			// Add visibility section
			{
				FToolMenuSection& Section = InMenu->AddSection(TEXT("Visibility"), LOCTEXT("VisibilitySection", "Visibility"));

				// Toggle Loaded In Editor
				Section.AddMenuEntry(
					TEXT("ToggleLoadedInEditor"),
					LOCTEXT("ToggleLoadedInEditor", "Toggle Loaded In Editor"),
					LOCTEXT("ToggleLoadedInEditorTooltip", "Toggle whether selected DataLayers are loaded in editor"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([SelectedDataLayers]()
					{
						if (UDataLayerEditorSubsystem* Subsystem = UDataLayerEditorSubsystem::Get())
						{
							const FScopedTransaction Transaction(LOCTEXT("ToggleDataLayersLoadedInEditor", "Toggle Data Layers Loaded In Editor"));
							Subsystem->ToggleDataLayersIsLoadedInEditor(SelectedDataLayers, true);
						}
					})));

				// Make Loaded
				Section.AddMenuEntry(
					TEXT("MakeLoaded"),
					LOCTEXT("MakeLoaded", "Load In Editor"),
					LOCTEXT("MakeLoadedTooltip", "Load selected DataLayers in editor"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([SelectedDataLayers]()
					{
						if (UDataLayerEditorSubsystem* Subsystem = UDataLayerEditorSubsystem::Get())
						{
							const FScopedTransaction Transaction(LOCTEXT("LoadDataLayersInEditor", "Load Data Layers In Editor"));
							for (UDataLayerInstance* DataLayer : SelectedDataLayers)
							{
								Subsystem->SetDataLayerIsLoadedInEditor(DataLayer, true, true);
							}
						}
					})));

				// Make Unloaded
				Section.AddMenuEntry(
					TEXT("MakeUnloaded"),
					LOCTEXT("MakeUnloaded", "Unload In Editor"),
					LOCTEXT("MakeUnloadedTooltip", "Unload selected DataLayers in editor"),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([SelectedDataLayers]()
					{
						if (UDataLayerEditorSubsystem* Subsystem = UDataLayerEditorSubsystem::Get())
						{
							const FScopedTransaction Transaction(LOCTEXT("UnloadDataLayersInEditor", "Unload Data Layers In Editor"));
							for (UDataLayerInstance* DataLayer : SelectedDataLayers)
							{
								Subsystem->SetDataLayerIsLoadedInEditor(DataLayer, false, true);
							}
						}
					})));
			}

			// Add asset options section
			{
				FToolMenuSection& Section = InMenu->AddSection(TEXT("AssetOptions"), LOCTEXT("AssetOptionsSection", "Asset Options"));

				// Find in Content Browser
				Section.AddMenuEntry(
					TEXT("FindInContentBrowser"),
					LOCTEXT("FindInContentBrowser", "Find in Content Browser"),
					LOCTEXT("FindInContentBrowserTooltip", "Browse to the DataLayer asset in Content Browser"),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "SystemWideCommands.FindInContentBrowser"),
					FUIAction(
						FExecuteAction::CreateLambda([SelectedDataLayers]()
						{
							TArray<UObject*> Objects;
							for (UDataLayerInstance* DataLayer : SelectedDataLayers)
							{
								if (const UDataLayerInstanceWithAsset* DataLayerWithAsset = Cast<UDataLayerInstanceWithAsset>(DataLayer))
								{
									if (const UDataLayerAsset* Asset = DataLayerWithAsset->GetAsset())
									{
										Objects.Add(const_cast<UDataLayerAsset*>(Asset));
									}
								}
							}
							if (!Objects.IsEmpty() && GEditor)
							{
								GEditor->SyncBrowserToObjects(Objects);
							}
						}),
						FCanExecuteAction::CreateLambda([SelectedDataLayers]()
						{
							for (UDataLayerInstance* DataLayer : SelectedDataLayers)
							{
								if (const UDataLayerInstanceWithAsset* DataLayerWithAsset = Cast<UDataLayerInstanceWithAsset>(DataLayer))
								{
									if (DataLayerWithAsset->GetAsset())
									{
										return true;
									}
								}
							}
							return false;
						})
					));

				// Rename (only for single selection)
				if (SelectedDataLayers.Num() == 1)
				{
					UDataLayerInstance* SingleDataLayer = SelectedDataLayers[0];
					const bool bCanRename = SingleDataLayer && !SingleDataLayer->IsReadOnly() && SingleDataLayer->CanEditDataLayerShortName();

					Section.AddMenuEntry(
						TEXT("RenameDataLayer"),
						LOCTEXT("RenameDataLayer", "Rename"),
						LOCTEXT("RenameDataLayerTooltip", "Rename the selected DataLayer (F2)"),
						FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([SceneOutliner, SelectedItems]()
							{
								if (SceneOutliner.IsValid() && SelectedItems.Num() == 1)
								{
									SceneOutliner->SetPendingRenameItem(SelectedItems[0]);
									SceneOutliner->ScrollItemIntoView(SelectedItems[0]);
								}
							}),
							FCanExecuteAction::CreateLambda([bCanRename]() { return bCanRename; })
						));
				}
			}
		}));
	}
}

TSharedPtr<SWidget> FStageDataLayerMode::CreateContextMenu()
{
	// Register context menu if not already
	RegisterContextMenu();

	// Get the menu
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return nullptr;
	}

	static const FName ContextMenuName = TEXT("StageDataLayerOutliner.ContextMenu");

	// Create context
	USceneOutlinerMenuContext* ContextObject = NewObject<USceneOutlinerMenuContext>();
	ContextObject->SceneOutliner = StaticCastSharedRef<SSceneOutliner>(SceneOutliner->AsShared());
	ContextObject->bShowParentTree = false;
	ContextObject->NumSelectedItems = SceneOutliner->GetSelection().Num();

	FToolMenuContext Context(ContextObject);

	return ToolMenus->GenerateWidget(ContextMenuName, Context);
}

//----------------------------------------------------------------
// Public API
//----------------------------------------------------------------

TArray<UDataLayerInstance*> FStageDataLayerMode::GetSelectedDataLayers() const
{
	TArray<UDataLayerInstance*> Result;

	TArray<FSceneOutlinerTreeItemPtr> SelectedItems;
	SceneOutliner->GetSelection().Get(SelectedItems);
	for (const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
	{
		if (const FStageDataLayerTreeItem* DataLayerItem = Item->CastTo<FStageDataLayerTreeItem>())
		{
			if (UDataLayerInstance* DataLayer = DataLayerItem->GetDataLayer())
			{
				Result.Add(DataLayer);
			}
		}
	}

	return Result;
}

//----------------------------------------------------------------
// Helpers
//----------------------------------------------------------------

void FStageDataLayerMode::ChooseRepresentingWorld()
{
	// If a specific world was specified, use it
	if (SpecifiedWorldToDisplay.IsValid())
	{
		RepresentingWorld = SpecifiedWorldToDisplay;
		return;
	}

	// Otherwise, use the editor world
	if (GEditor)
	{
		RepresentingWorld = GEditor->GetEditorWorldContext().World();
	}
}

void FStageDataLayerMode::OnLevelSelectionChanged(UObject* Obj)
{
	// Refresh highlighting if enabled
	if (bHighlightSelectedDataLayers)
	{
		RefreshSelection();
	}
}

UWorld* FStageDataLayerMode::GetOwningWorld() const
{
	UWorld* World = RepresentingWorld.Get();
	return World ? World->PersistentLevel->GetWorld() : nullptr;
}

UDataLayerInstance* FStageDataLayerMode::GetDataLayerFromTreeItem(const ISceneOutlinerTreeItem& TreeItem) const
{
	if (const FStageDataLayerTreeItem* DataLayerItem = TreeItem.CastTo<FStageDataLayerTreeItem>())
	{
		return DataLayerItem->GetDataLayer();
	}
	return nullptr;
}

void FStageDataLayerMode::CacheSelectedItems(const FSceneOutlinerItemSelection& Selection)
{
	SelectedDataLayersSet.Reset();

	TArray<FSceneOutlinerTreeItemPtr> SelectedItems;
	Selection.Get(SelectedItems);
	for (const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
	{
		if (const FStageDataLayerTreeItem* DataLayerItem = Item->CastTo<FStageDataLayerTreeItem>())
		{
			if (UDataLayerInstance* DataLayer = DataLayerItem->GetDataLayer())
			{
				SelectedDataLayersSet.Add(DataLayer);
			}
		}
	}
}

void FStageDataLayerMode::RefreshSelection()
{
	// Trigger a rebuild to refresh highlighting
	SceneOutliner->FullRefresh();
}

void FStageDataLayerMode::FindInContentBrowser()
{
	if (SceneOutliner)
	{
		TArray<UObject*> Objects;
		for (const TWeakObjectPtr<const UDataLayerInstance>& DataLayerInstance : SelectedDataLayersSet)
		{
			if (const UDataLayerInstanceWithAsset* DataLayerInstanceWithAsset = Cast<UDataLayerInstanceWithAsset>(DataLayerInstance.Get()))
			{
				if (const UDataLayerAsset* Asset = DataLayerInstanceWithAsset->GetAsset())
				{
					Objects.Add(const_cast<UDataLayerAsset*>(Asset));
				}
			}
		}
		if (!Objects.IsEmpty())
		{
			GEditor->SyncBrowserToObjects(Objects);
		}
	}
}

#undef LOCTEXT_NAMESPACE
