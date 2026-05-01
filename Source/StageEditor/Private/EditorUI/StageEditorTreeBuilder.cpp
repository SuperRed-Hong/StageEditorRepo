// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorUI/StageEditorTreeBuilder.h"
#include "EditorLogic/StageEditorController.h"
#include "DataModels/StageTreeItem.h"
#include "Actors/Stage.h"
#include "Core/StageCoreTypes.h"

//----------------------------------------------------------------
// Constructor / Destructor
//----------------------------------------------------------------

FStageEditorTreeBuilder::FStageEditorTreeBuilder(TWeakPtr<FStageEditorController> InController)
	: WeakController(InController)
{
}

FStageEditorTreeBuilder::~FStageEditorTreeBuilder()
{
}

//----------------------------------------------------------------
// Tree Building
//----------------------------------------------------------------

void FStageEditorTreeBuilder::RebuildTreeItems(
	TArray<TSharedPtr<FStageTreeItem>>& OutRootItems,
	TMap<FString, TWeakPtr<FStageTreeItem>>& OutActorPathToTreeItem)
{
	OutRootItems.Empty();
	OutActorPathToTreeItem.Empty();

	TSharedPtr<FStageEditorController> Controller = WeakController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}

	const TArray<TWeakObjectPtr<AStage>>& FoundStages = Controller->GetFoundStages();

	// Collect valid Stage pointers and sort by StageID ascending
	TArray<AStage*> SortedStages;
	for (const auto& StagePtr : FoundStages)
	{
		if (AStage* Stage = StagePtr.Get())
		{
			SortedStages.Add(Stage);
		}
	}
	SortedStages.Sort([](const AStage& A, const AStage& B)
	{
		return A.GetStageID() < B.GetStageID();
	});

	for (AStage* Stage : SortedStages)
	{
		TSharedPtr<FStageTreeItem> StageItem = BuildStageItem(Stage, OutActorPathToTreeItem);
		if (StageItem.IsValid())
		{
			OutRootItems.Add(StageItem);
		}
	}
}

//----------------------------------------------------------------
// Tree View Callbacks
//----------------------------------------------------------------

void FStageEditorTreeBuilder::OnGetChildren(
	TSharedPtr<FStageTreeItem> Item,
	TArray<TSharedPtr<FStageTreeItem>>& OutChildren)
{
	if (Item.IsValid())
	{
		OutChildren = Item->Children;
	}
}

//----------------------------------------------------------------
// Tree Building Helpers
//----------------------------------------------------------------

TSharedPtr<FStageTreeItem> FStageEditorTreeBuilder::BuildStageItem(
	AStage* Stage,
	TMap<FString, TWeakPtr<FStageTreeItem>>& OutActorPathToTreeItem)
{
	if (!Stage)
	{
		return nullptr;
	}

	// Create Stage Root Item
	FString StageName = Stage->GetActorLabel();
	TSharedPtr<FStageTreeItem> StageItem = MakeShared<FStageTreeItem>(
		EStageTreeItemType::Stage, StageName, Stage->GetStageID(), nullptr, Stage);
	StageItem->ActorPtr = Stage;
	StageItem->ActorPath = Stage->GetPathName();

	// Add to actor path map
	if (!StageItem->ActorPath.IsEmpty())
	{
		OutActorPathToTreeItem.Add(StageItem->ActorPath, StageItem);
	}

	// Build Acts Folder
	TSharedPtr<FStageTreeItem> ActsFolder = BuildActsFolder(Stage, StageItem, OutActorPathToTreeItem);
	if (ActsFolder.IsValid())
	{
		StageItem->Children.Add(ActsFolder);
		ActsFolder->Parent = StageItem;
	}

	// Build Entities Folder
	TSharedPtr<FStageTreeItem> EntitiesFolder = BuildEntitiesFolder(Stage, StageItem, OutActorPathToTreeItem);
	if (EntitiesFolder.IsValid())
	{
		StageItem->Children.Add(EntitiesFolder);
		EntitiesFolder->Parent = StageItem;
	}

	return StageItem;
}

TSharedPtr<FStageTreeItem> FStageEditorTreeBuilder::BuildActsFolder(
	AStage* Stage,
	TSharedPtr<FStageTreeItem> StageItem,
	TMap<FString, TWeakPtr<FStageTreeItem>>& OutActorPathToTreeItem)
{
	if (!Stage)
	{
		return nullptr;
	}

	// Create "Acts" Folder
	TSharedPtr<FStageTreeItem> ActsFolder = MakeShared<FStageTreeItem>(EStageTreeItemType::ActsFolder, TEXT("Acts"));

	// Collect Act pointers and sort by ActID ascending
	TArray<FAct*> SortedActs;
	for (FAct& Act : Stage->Acts)
	{
		SortedActs.Add(&Act);
	}
	SortedActs.Sort([](const FAct& A, const FAct& B)
	{
		return A.SUID.ActID < B.SUID.ActID;
	});

	for (FAct* Act : SortedActs)
	{
		TSharedPtr<FStageTreeItem> ActItem = MakeShared<FStageTreeItem>(
			EStageTreeItemType::Act, Act->DisplayName, Act->SUID.ActID);
		ActsFolder->Children.Add(ActItem);
		ActItem->Parent = ActsFolder;

		// Collect EntityIDs and sort ascending (TMap iteration order is undefined)
		TArray<int32> SortedEntityIDs;
		Act->EntityStateOverrides.GetKeys(SortedEntityIDs);
		SortedEntityIDs.Sort();

		for (int32 EntityID : SortedEntityIDs)
		{
			int32 State = Act->EntityStateOverrides[EntityID];

			AActor* EntityActor = Stage->GetEntityByID(EntityID);
			FString EntityDisplayName = GetEntityDisplayName(EntityActor, EntityID);

			TSharedPtr<FStageTreeItem> EntityItem = MakeShared<FStageTreeItem>(
				EStageTreeItemType::Entity, EntityDisplayName, EntityID, EntityActor);
			ActItem->Children.Add(EntityItem);
			EntityItem->Parent = ActItem;
			EntityItem->StagePtr = Stage;
			EntityItem->EntityState = State;
			EntityItem->bHasEntityState = true;

			if (EntityActor)
			{
				EntityItem->ActorPath = EntityActor->GetPathName();
				if (!EntityItem->ActorPath.IsEmpty() && !OutActorPathToTreeItem.Contains(EntityItem->ActorPath))
				{
					OutActorPathToTreeItem.Add(EntityItem->ActorPath, EntityItem);
				}
			}
		}
	}

	return ActsFolder;
}

TSharedPtr<FStageTreeItem> FStageEditorTreeBuilder::BuildEntitiesFolder(
	AStage* Stage,
	TSharedPtr<FStageTreeItem> StageItem,
	TMap<FString, TWeakPtr<FStageTreeItem>>& OutActorPathToTreeItem)
{
	if (!Stage)
	{
		return nullptr;
	}

	// Create "Registered Entities" Folder
	TSharedPtr<FStageTreeItem> EntitiesFolder = MakeShared<FStageTreeItem>(
		EStageTreeItemType::EntitiesFolder, TEXT("Registered Entities"));

	// Collect EntityIDs and sort ascending (TMap iteration order is undefined)
	TArray<int32> SortedEntityIDs;
	Stage->EntityRegistry.GetKeys(SortedEntityIDs);
	SortedEntityIDs.Sort();

	for (int32 EntityID : SortedEntityIDs)
	{
		if (AActor* Actor = Stage->EntityRegistry[EntityID].Get())
		{
			FString EntityDisplayName = GetEntityDisplayName(Actor, EntityID);
			TSharedPtr<FStageTreeItem> EntityItem = MakeShared<FStageTreeItem>(
				EStageTreeItemType::Entity, EntityDisplayName, EntityID, Actor);
			EntitiesFolder->Children.Add(EntityItem);
			EntityItem->Parent = EntitiesFolder;
			EntityItem->StagePtr = Stage;
			EntityItem->ActorPath = Actor->GetPathName();

			if (!EntityItem->ActorPath.IsEmpty() && !OutActorPathToTreeItem.Contains(EntityItem->ActorPath))
			{
				OutActorPathToTreeItem.Add(EntityItem->ActorPath, EntityItem);
			}
		}
	}

	return EntitiesFolder;
}

FString FStageEditorTreeBuilder::GetEntityDisplayName(AActor* EntityActor, int32 EntityID) const
{
	return EntityActor ? EntityActor->GetActorLabel() : FString::Printf(TEXT("Invalid Entity (ID: %d)"), EntityID);
}
