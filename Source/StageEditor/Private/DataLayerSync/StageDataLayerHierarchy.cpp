// Copyright Stage Editor Plugin. All Rights Reserved.

#include "DataLayerSync/StageDataLayerHierarchy.h"
#include "DataLayerSync/StageDataLayerTreeItem.h"
#include "DataLayerSync/StageDataLayerMode.h"

#include "DataLayer/DataLayerEditorSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "ISceneOutlinerMode.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

#define LOCTEXT_NAMESPACE "StageDataLayerHierarchy"

//----------------------------------------------------------------
// Factory Method
//----------------------------------------------------------------

TUniquePtr<FStageDataLayerHierarchy> FStageDataLayerHierarchy::Create(FStageDataLayerMode* Mode, const TWeakObjectPtr<UWorld>& World)
{
	return TUniquePtr<FStageDataLayerHierarchy>(new FStageDataLayerHierarchy(Mode, World));
}

//----------------------------------------------------------------
// Construction & Destruction
//----------------------------------------------------------------

FStageDataLayerHierarchy::FStageDataLayerHierarchy(FStageDataLayerMode* Mode, const TWeakObjectPtr<UWorld>& World)
	: ISceneOutlinerHierarchy(Mode)
	, RepresentingWorld(World)
	, bHighlightSelectedDataLayers(false)
{
	// Subscribe to engine events for actor list changes
	if (GEngine)
	{
		GEngine->OnLevelActorListChanged().AddRaw(this, &FStageDataLayerHierarchy::OnLevelActorListChanged);
	}

	// Subscribe to DataLayer change events
	if (UDataLayerEditorSubsystem* DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get())
	{
		DataLayerEditorSubsystem->OnDataLayerChanged().AddRaw(this, &FStageDataLayerHierarchy::OnDataLayerChanged);
		DataLayerEditorSubsystem->OnActorDataLayersChanged().AddRaw(this, &FStageDataLayerHierarchy::OnActorDataLayersChanged);
	}
}

FStageDataLayerHierarchy::~FStageDataLayerHierarchy()
{
	// Unsubscribe from engine events
	if (GEngine)
	{
		GEngine->OnLevelActorListChanged().RemoveAll(this);
	}

	// Unsubscribe from DataLayer events
	if (UDataLayerEditorSubsystem* DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get())
	{
		DataLayerEditorSubsystem->OnDataLayerChanged().RemoveAll(this);
		DataLayerEditorSubsystem->OnActorDataLayersChanged().RemoveAll(this);
	}
}

//----------------------------------------------------------------
// ISceneOutlinerHierarchy Implementation
//----------------------------------------------------------------

void FStageDataLayerHierarchy::CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const
{
	UWorld* OwningWorld = GetOwningWorld();
	if (!OwningWorld)
	{
		return;
	}

	// Get the DataLayer manager for the world
	UDataLayerManager* DataLayerManager = OwningWorld->GetDataLayerManager();
	if (!DataLayerManager)
	{
		return;
	}

	// Iterate all DataLayers and create tree items for each
	DataLayerManager->ForEachDataLayerInstance([this, &OutItems](UDataLayerInstance* DataLayerInstance)
	{
		if (DataLayerInstance)
		{
			if (FSceneOutlinerTreeItemPtr DataLayerItem = CreateDataLayerTreeItem(DataLayerInstance))
			{
				OutItems.Add(DataLayerItem);
			}
		}
		return true; // Continue iteration
	});
}

void FStageDataLayerHierarchy::CreateChildren(const FSceneOutlinerTreeItemPtr& Item, TArray<FSceneOutlinerTreeItemPtr>& OutChildren) const
{
	// We don't show Actor children in this hierarchy
	// This is a design decision to keep the browser focused on DataLayer management
}

FSceneOutlinerTreeItemPtr FStageDataLayerHierarchy::FindOrCreateParentItem(
	const ISceneOutlinerTreeItem& Item,
	const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items,
	bool bCreate)
{
	// Handle DataLayer parent hierarchy
	if (const FStageDataLayerTreeItem* DataLayerTreeItem = Item.CastTo<FStageDataLayerTreeItem>())
	{
		if (UDataLayerInstance* DataLayerInstance = DataLayerTreeItem->GetDataLayer())
		{
			// Check if this DataLayer has a parent
			if (UDataLayerInstance* ParentDataLayer = DataLayerInstance->GetParent())
			{
				// Try to find existing parent item
				if (const FSceneOutlinerTreeItemPtr* ParentItem = Items.Find(ParentDataLayer))
				{
					return *ParentItem;
				}
				else if (bCreate)
				{
					// Create parent item if requested
					return CreateDataLayerTreeItem(ParentDataLayer, /*bForce=*/ true);
				}
			}
		}
	}

	return nullptr;
}

//----------------------------------------------------------------
// Helpers
//----------------------------------------------------------------

UWorld* FStageDataLayerHierarchy::GetOwningWorld() const
{
	UWorld* World = RepresentingWorld.Get();
	return World ? World->PersistentLevel->GetWorld() : nullptr;
}

FSceneOutlinerTreeItemPtr FStageDataLayerHierarchy::CreateDataLayerTreeItem(UDataLayerInstance* InDataLayer, bool bForce) const
{
	// Use the Mode's CreateItemFor helper to handle filtering
	FSceneOutlinerTreeItemPtr Item = Mode->CreateItemFor<FStageDataLayerTreeItem>(InDataLayer, bForce);

	// Configure highlight behavior
	if (FStageDataLayerTreeItem* DataLayerTreeItem = Item ? Item->CastTo<FStageDataLayerTreeItem>() : nullptr)
	{
		DataLayerTreeItem->SetIsHighlightedIfSelected(bHighlightSelectedDataLayers);
	}

	return Item;
}

void FStageDataLayerHierarchy::FullRefreshEvent()
{
	FSceneOutlinerHierarchyChangedData EventData;
	EventData.Type = FSceneOutlinerHierarchyChangedData::FullRefresh;
	HierarchyChangedEvent.Broadcast(EventData);
}

//----------------------------------------------------------------
// Event Handlers
//----------------------------------------------------------------

void FStageDataLayerHierarchy::OnDataLayerChanged(
	const EDataLayerAction Action,
	const TWeakObjectPtr<const UDataLayerInstance>& ChangedDataLayer,
	const FName& ChangedProperty)
{
	// Refresh the tree on any DataLayer change
	// This is simpler and more robust than trying to handle individual add/remove events
	FullRefreshEvent();
}

void FStageDataLayerHierarchy::OnActorDataLayersChanged(const TWeakObjectPtr<AActor>& InActor)
{
	// Refresh when actor memberships change (affects highlighting)
	if (bHighlightSelectedDataLayers)
	{
		FullRefreshEvent();
	}
}

void FStageDataLayerHierarchy::OnLevelActorListChanged()
{
	// Refresh when level actors change (may affect DataLayer visibility)
	FullRefreshEvent();
}

#undef LOCTEXT_NAMESPACE
