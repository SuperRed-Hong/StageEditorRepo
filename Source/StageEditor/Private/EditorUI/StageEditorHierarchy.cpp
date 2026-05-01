// Copyright Stage Editor Plugin. All Rights Reserved.

#include "EditorUI/StageEditorHierarchy.h"
#include "EditorUI/StageEditorTreeItems.h"
#include "EditorUI/StageEditorMode.h"
#include "Actors/Stage.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "EngineUtils.h"
#include "Engine/World.h"

FStageEditorHierarchy::~FStageEditorHierarchy()
{
	// Unbind delegates
	if (UWorld* World = RepresentingWorld.Get())
	{
		if (UStageManagerSubsystem* StageManager = World->GetSubsystem<UStageManagerSubsystem>())
		{
			StageManager->OnStageRegistered.Remove(StageRegisteredHandle);
			StageManager->OnStageUnregistered.Remove(StageUnregisteredHandle);
		}
	}
}

TUniquePtr<FStageEditorHierarchy> FStageEditorHierarchy::Create(FStageEditorMode* Mode, const TWeakObjectPtr<UWorld>& World)
{
	return TUniquePtr<FStageEditorHierarchy>(new FStageEditorHierarchy(Mode, World));
}

FStageEditorHierarchy::FStageEditorHierarchy(FStageEditorMode* Mode, const TWeakObjectPtr<UWorld>& World)
	: ISceneOutlinerHierarchy(Mode)
	, RepresentingWorld(World)
{
	// Bind to Stage events
	if (UWorld* WorldPtr = World.Get())
	{
		if (UStageManagerSubsystem* StageManager = WorldPtr->GetSubsystem<UStageManagerSubsystem>())
		{
			StageRegisteredHandle = StageManager->OnStageRegistered.AddRaw(this, &FStageEditorHierarchy::OnStageRegistered);
			StageUnregisteredHandle = StageManager->OnStageUnregistered.AddRaw(this, &FStageEditorHierarchy::OnStageUnregistered);
		}
	}
}

void FStageEditorHierarchy::CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const
{
	UWorld* World = GetOwningWorld();
	if (!World)
	{
		return;
	}

	// Iterate all Stage actors in the world
	for (TActorIterator<AStage> It(World); It; ++It)
	{
		AStage* Stage = *It;
		if (Stage && Stage->GetStageID() > 0)
		{
			OutItems.Add(MakeShared<FStageEditorStageTreeItem>(Stage));
		}
	}
}

void FStageEditorHierarchy::CreateChildren(const FSceneOutlinerTreeItemPtr& Item, TArray<FSceneOutlinerTreeItemPtr>& OutChildren) const
{
	if (!Item.IsValid())
	{
		return;
	}

	// Stage -> ActsFolder + EntitiesFolder
	if (FStageEditorStageTreeItem* StageItem = Item->CastTo<FStageEditorStageTreeItem>())
	{
		AStage* Stage = StageItem->GetStage();
		if (Stage)
		{
			OutChildren.Add(MakeShared<FStageEditorActsFolderTreeItem>(Stage));
			OutChildren.Add(MakeShared<FStageEditorEntitiesFolderTreeItem>(Stage));
		}
	}
	// ActsFolder -> All Acts
	else if (FStageEditorActsFolderTreeItem* ActsFolderItem = Item->CastTo<FStageEditorActsFolderTreeItem>())
	{
		AStage* Stage = ActsFolderItem->GetOwnerStage();
		if (Stage)
		{
			for (int32 i = 0; i < Stage->Acts.Num(); ++i)
			{
				OutChildren.Add(MakeShared<FStageEditorActTreeItem>(Stage, i));
			}
		}
	}
	// Act -> Entities with state overrides
	else if (FStageEditorActTreeItem* ActItem = Item->CastTo<FStageEditorActTreeItem>())
	{
		FAct* Act = ActItem->GetAct();
		AStage* Stage = ActItem->GetOwnerStage();
		if (Act && Stage)
		{
			// Create Entity items for each EntityStateOverride
			for (const auto& Pair : Act->EntityStateOverrides)
			{
				int32 EntityID = Pair.Key;
				int32 EntityState = Pair.Value;

				// Find the Entity actor
				AActor* EntityActor = Stage->GetEntityByID(EntityID);
				if (EntityActor)
				{
					OutChildren.Add(MakeShared<FStageEditorEntityUnderActTreeItem>(
						EntityActor, EntityID, Stage, Act->SUID, EntityState));
				}
			}
		}
	}
	// EntitiesFolder -> All registered Entities (flat list)
	else if (FStageEditorEntitiesFolderTreeItem* EntitiesFolderItem = Item->CastTo<FStageEditorEntitiesFolderTreeItem>())
	{
		AStage* Stage = EntitiesFolderItem->GetOwnerStage();
		if (Stage)
		{
			for (const auto& Pair : Stage->EntityRegistry)
			{
				int32 EntityID = Pair.Key;
				AActor* EntityActor = Pair.Value.Get();
				if (EntityActor)
				{
					OutChildren.Add(MakeShared<FStageEditorEntityTreeItem>(EntityActor, EntityID, Stage));
				}
			}
		}
	}
	// Entity items have no children
}

FSceneOutlinerTreeItemPtr FStageEditorHierarchy::FindOrCreateParentItem(
	const ISceneOutlinerTreeItem& Item,
	const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items,
	bool bCreate)
{
	// ActsFolder/EntitiesFolder -> Stage
	if (const FStageEditorActsFolderTreeItem* ActsFolderItem = Item.CastTo<FStageEditorActsFolderTreeItem>())
	{
		AStage* Stage = ActsFolderItem->GetOwnerStage();
		if (Stage)
		{
			FSceneOutlinerTreeItemID StageID(static_cast<const UObject*>(Stage));
			if (const FSceneOutlinerTreeItemPtr* Found = Items.Find(StageID))
			{
				return *Found;
			}
			if (bCreate)
			{
				return MakeShared<FStageEditorStageTreeItem>(Stage);
			}
		}
	}
	else if (const FStageEditorEntitiesFolderTreeItem* EntitiesFolderItem = Item.CastTo<FStageEditorEntitiesFolderTreeItem>())
	{
		AStage* Stage = EntitiesFolderItem->GetOwnerStage();
		if (Stage)
		{
			FSceneOutlinerTreeItemID StageID(static_cast<const UObject*>(Stage));
			if (const FSceneOutlinerTreeItemPtr* Found = Items.Find(StageID))
			{
				return *Found;
			}
			if (bCreate)
			{
				return MakeShared<FStageEditorStageTreeItem>(Stage);
			}
		}
	}
	// Act -> ActsFolder
	else if (const FStageEditorActTreeItem* ActItem = Item.CastTo<FStageEditorActTreeItem>())
	{
		AStage* Stage = ActItem->GetOwnerStage();
		if (Stage)
		{
			// Calculate ActsFolder ID
			uint64 ActsFolderHash = HashCombine(
				GetTypeHash(Stage->GetStageID()),
				GetTypeHash(StageEditorTreeItemIDs::ActsFolderMagic));
			FSceneOutlinerTreeItemID ActsFolderID(ActsFolderHash);

			if (const FSceneOutlinerTreeItemPtr* Found = Items.Find(ActsFolderID))
			{
				return *Found;
			}
			if (bCreate)
			{
				return MakeShared<FStageEditorActsFolderTreeItem>(Stage);
			}
		}
	}
	// EntityUnderAct -> Act
	else if (const FStageEditorEntityUnderActTreeItem* EntityUnderActItem = Item.CastTo<FStageEditorEntityUnderActTreeItem>())
	{
		// Find parent Act by SUID
		uint64 ActHash = GetTypeHash(EntityUnderActItem->GetActSUID());
		FSceneOutlinerTreeItemID ActID(ActHash);

		if (const FSceneOutlinerTreeItemPtr* Found = Items.Find(ActID))
		{
			return *Found;
		}
		// Cannot create Act item without index - need to search
		if (bCreate)
		{
			AStage* Stage = EntityUnderActItem->GetOwnerStage();
			if (Stage)
			{
				const FSUID& ActSUID = EntityUnderActItem->GetActSUID();
				for (int32 i = 0; i < Stage->Acts.Num(); ++i)
				{
					if (Stage->Acts[i].SUID == ActSUID)
					{
						return MakeShared<FStageEditorActTreeItem>(Stage, i);
					}
				}
			}
		}
	}
	// EntityFlat -> EntitiesFolder
	else if (const FStageEditorEntityTreeItem* EntityItem = Item.CastTo<FStageEditorEntityTreeItem>())
	{
		AStage* Stage = EntityItem->GetOwnerStage();
		if (Stage)
		{
			// Calculate EntitiesFolder ID
			uint64 EntitiesFolderHash = HashCombine(
				GetTypeHash(Stage->GetStageID()),
				GetTypeHash(StageEditorTreeItemIDs::EntitiesFolderMagic));
			FSceneOutlinerTreeItemID EntitiesFolderID(EntitiesFolderHash);

			if (const FSceneOutlinerTreeItemPtr* Found = Items.Find(EntitiesFolderID))
			{
				return *Found;
			}
			if (bCreate)
			{
				return MakeShared<FStageEditorEntitiesFolderTreeItem>(Stage);
			}
		}
	}

	return nullptr;
}

UWorld* FStageEditorHierarchy::GetOwningWorld() const
{
	return RepresentingWorld.Get();
}

AStage* FStageEditorHierarchy::FindStageByID(int32 StageID) const
{
	UWorld* World = GetOwningWorld();
	if (!World)
	{
		return nullptr;
	}

	if (UStageManagerSubsystem* StageManager = World->GetSubsystem<UStageManagerSubsystem>())
	{
		return StageManager->GetStage(StageID);
	}
	return nullptr;
}

void FStageEditorHierarchy::FullRefreshEvent()
{
	FSceneOutlinerHierarchyChangedData EventData;
	EventData.Type = FSceneOutlinerHierarchyChangedData::FullRefresh;
	HierarchyChangedEvent.Broadcast(EventData);
}

void FStageEditorHierarchy::OnStageRegistered(AStage* Stage)
{
	// Trigger full refresh when Stage is added
	FullRefreshEvent();
}

void FStageEditorHierarchy::OnStageUnregistered(AStage* Stage, int32 StageID)
{
	// Trigger full refresh when Stage is removed
	FullRefreshEvent();
}

void FStageEditorHierarchy::OnStageDataChanged(AStage* Stage)
{
	// Trigger full refresh when Stage data changes
	FullRefreshEvent();
}

void FStageEditorHierarchy::OnLevelActorListChanged()
{
	// Trigger full refresh when level changes
	FullRefreshEvent();
}
