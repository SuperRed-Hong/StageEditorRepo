// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerSync/DataLayerSynchronizer.h"
#include "DataLayerSync/DataLayerSyncUtils.h"
#include "DataLayerSync/DataLayerSyncStatus.h"
#include "DataLayerSync/DataLayerSyncStatusCache.h"
#include "DataLayerSync/StageDataLayerNameParser.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "Actors/Stage.h"
#include "Core/StageCoreTypes.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "StageEditorDataLayerSync"

// Use shared utilities
using namespace StageDataLayerSyncUtils;

//----------------------------------------------------------------
// Helper Functions
//----------------------------------------------------------------

namespace
{
	/**
	 * Find DataLayerAsset by name from the current child DataLayers
	 */
	UDataLayerAsset* FindChildDataLayerAssetByName(UDataLayerAsset* ParentAsset, const FString& ChildName, UWorld* World)
	{
		if (!ParentAsset || !World)
		{
			return nullptr;
		}

		UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(World);
		if (!Manager)
		{
			return nullptr;
		}

		UDataLayerAsset* FoundAsset = nullptr;

		Manager->ForEachDataLayerInstance([&](UDataLayerInstance* Instance)
		{
			if (Instance && Instance->GetParent())
			{
				if (Instance->GetParent()->GetAsset() == ParentAsset)
				{
					if (Instance->GetDataLayerShortName() == ChildName ||
						(Instance->GetAsset() && Instance->GetAsset()->GetName() == ChildName))
					{
						FoundAsset = const_cast<UDataLayerAsset*>(Instance->GetAsset());
						return false; // Stop iteration
					}
				}
			}
			return true; // Continue iteration
		});

		return FoundAsset;
	}
}

//----------------------------------------------------------------
// FDataLayerSynchronizer Implementation
//----------------------------------------------------------------

bool FDataLayerSynchronizer::CanSync(UDataLayerAsset* DataLayerAsset, FText& OutErrorMessage)
{
	if (!DataLayerAsset)
	{
		OutErrorMessage = LOCTEXT("ErrorNullAsset", "DataLayerAsset is null");
		return false;
	}

	UStageManagerSubsystem* Subsystem = GetStageManagerSubsystem(nullptr);
	if (!Subsystem)
	{
		OutErrorMessage = LOCTEXT("ErrorNoSubsystem", "StageManagerSubsystem not available");
		return false;
	}

	// Check if DataLayer is imported
	AStage* Stage = Subsystem->FindStageByDataLayer(DataLayerAsset);
	if (!Stage)
	{
		OutErrorMessage = LOCTEXT("ErrorNotImported", "DataLayer has not been imported. Use Import first.");
		return false;
	}

	// Check sync status
	FDataLayerSyncStatusInfo StatusInfo = FDataLayerSyncStatusDetector::DetectStatus(DataLayerAsset);
	if (StatusInfo.Status == EDataLayerSyncStatus::Synced)
	{
		OutErrorMessage = LOCTEXT("ErrorAlreadySynced", "DataLayer is already in sync");
		return false;
	}

	return true;
}

FDataLayerSyncResult FDataLayerSynchronizer::SyncDataLayer(UDataLayerAsset* DataLayerAsset, UWorld* World)
{
	FDataLayerSyncResult Result;

	if (!World)
	{
		World = GetEditorWorld();
	}

	// Validate
	FText ErrorMessage;
	if (!CanSync(DataLayerAsset, ErrorMessage))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = ErrorMessage;
		return Result;
	}

	UStageManagerSubsystem* Subsystem = GetStageManagerSubsystem(World);
	if (!Subsystem)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = LOCTEXT("ErrorNoSubsystem", "StageManagerSubsystem not available");
		return Result;
	}

	// Find associated Stage
	AStage* Stage = Subsystem->FindStageByDataLayer(DataLayerAsset);
	if (!Stage)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = LOCTEXT("ErrorNoStage", "Cannot find associated Stage");
		return Result;
	}

	// Get sync status info
	FDataLayerSyncStatusInfo StatusInfo = FDataLayerSyncStatusDetector::DetectStatus(DataLayerAsset);

	// Begin transaction
	FScopedTransaction Transaction(LOCTEXT("SyncDataLayer", "Sync DataLayer"));
	Stage->Modify();

	// Determine sync type
	if (Stage->StageDataLayerAsset == DataLayerAsset)
	{
		// Stage-level sync
		Result = SyncStageLevelChanges(Stage, StatusInfo, World);
	}
	else
	{
		// Act-level sync
		int32 ActID = Subsystem->FindActIDByDataLayer(Stage, DataLayerAsset);
		Result = SyncActLevelChanges(Stage, ActID, DataLayerAsset, StatusInfo, World);
	}

	// Invalidate cache
	FDataLayerSyncStatusCache::Get().InvalidateCache(DataLayerAsset);

	// Broadcast stage data changed to notify StageEditorPanel to refresh
	if (Result.bSuccess)
	{
		Subsystem->BroadcastStageDataChanged(Stage);
	}

	return Result;
}

FDataLayerBatchSyncResult FDataLayerSynchronizer::SyncAllOutOfSync(UWorld* World)
{
	FDataLayerBatchSyncResult BatchResult;

	if (!World)
	{
		World = GetEditorWorld();
	}
	if (!World)
	{
		return BatchResult;
	}

	UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(World);
	if (!Manager)
	{
		return BatchResult;
	}

	// Collect all DataLayers that are out of sync
	TArray<UDataLayerAsset*> OutOfSyncAssets;

	Manager->ForEachDataLayerInstance([&](UDataLayerInstance* Instance)
	{
		if (Instance)
		{
			UDataLayerAsset* Asset = const_cast<UDataLayerAsset*>(Instance->GetAsset());
			if (Asset)
			{
				FDataLayerSyncStatusInfo StatusInfo = FDataLayerSyncStatusDetector::DetectStatus(Asset);
				if (StatusInfo.Status == EDataLayerSyncStatus::OutOfSync)
				{
					OutOfSyncAssets.AddUnique(Asset);
				}
				else if (StatusInfo.Status == EDataLayerSyncStatus::Synced ||
						 StatusInfo.Status == EDataLayerSyncStatus::NotImported)
				{
					BatchResult.SkippedCount++;
				}
			}
		}
		return true; // Continue iteration
	});

	// Sync each out-of-sync DataLayer
	for (UDataLayerAsset* Asset : OutOfSyncAssets)
	{
		FDataLayerSyncResult SingleResult = SyncDataLayer(Asset, World);
		if (SingleResult.bSuccess)
		{
			BatchResult.SyncedCount++;
			BatchResult.TotalActChanges += SingleResult.AddedActCount + SingleResult.RemovedActCount;
			BatchResult.TotalEntityChanges += SingleResult.AddedEntityCount + SingleResult.RemovedEntityCount;
		}
		else
		{
			BatchResult.FailedCount++;
		}
	}

	return BatchResult;
}

FDataLayerSyncResult FDataLayerSynchronizer::SyncStageLevelChanges(
	AStage* Stage,
	const FDataLayerSyncStatusInfo& StatusInfo,
	UWorld* World)
{
	FDataLayerSyncResult Result;
	Result.bSuccess = true;

	if (!Stage || !World)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = LOCTEXT("ErrorInvalidParams", "Invalid Stage or World");
		return Result;
	}

	// Handle new child DataLayers -> Create Acts
	for (const FString& NewChildName : StatusInfo.NewChildDataLayers)
	{
		UDataLayerAsset* ChildAsset = FindChildDataLayerAssetByName(Stage->StageDataLayerAsset, NewChildName, World);
		if (ChildAsset)
		{
			// Parse name to get Act name
			FDataLayerNameParseResult ParseResult = FStageDataLayerNameParser::Parse(ChildAsset->GetName());
			FString ActName = ParseResult.bIsValid ? ParseResult.ActName : NewChildName;

			if (CreateActFromDataLayer(Stage, ChildAsset, ActName, World))
			{
				Result.AddedActCount++;
			}
		}
	}

	// Handle removed child DataLayers -> Remove Acts
	// Note: We iterate backwards to safely remove from array
	for (const FString& RemovedChildName : StatusInfo.RemovedChildDataLayers)
	{
		// Find Act with this DataLayer name
		for (int32 i = Stage->Acts.Num() - 1; i >= 0; --i)
		{
			const FAct& Act = Stage->Acts[i];
			if (Act.AssociatedDataLayer && Act.AssociatedDataLayer->GetName() == RemovedChildName)
			{
				// Don't remove default Act (index 0)
				if (i > 0)
				{
					Stage->Acts.RemoveAt(i);
					Result.RemovedActCount++;
				}
				break;
			}
		}
	}

	return Result;
}

FDataLayerSyncResult FDataLayerSynchronizer::SyncActLevelChanges(
	AStage* Stage,
	int32 ActID,
	UDataLayerAsset* ActDataLayer,
	const FDataLayerSyncStatusInfo& StatusInfo,
	UWorld* World)
{
	FDataLayerSyncResult Result;
	Result.bSuccess = true;

	if (!Stage || !World || ActID < 0)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = LOCTEXT("ErrorInvalidActParams", "Invalid Stage, World, or ActID");
		return Result;
	}

	// Get current actors in this DataLayer
	TArray<AActor*> CurrentActors = GetActorsInDataLayer(ActDataLayer, World);

	// Collect current Entity paths for comparison
	TSet<FSoftObjectPath> CurrentActorPaths;
	for (AActor* Actor : CurrentActors)
	{
		if (Actor && Actor != Stage)
		{
			CurrentActorPaths.Add(FSoftObjectPath(Actor));
		}
	}

	// Collect registered Entity paths
	TSet<FSoftObjectPath> RegisteredEntityPaths;
	for (const auto& Pair : Stage->EntityRegistry)
	{
		if (Pair.Value.IsValid())
		{
			RegisteredEntityPaths.Add(Pair.Value.ToSoftObjectPath());
		}
	}

	// Register new actors as Entitys
	for (AActor* Actor : CurrentActors)
	{
		if (Actor && Actor != Stage)
		{
			FSoftObjectPath ActorPath(Actor);
			if (!RegisteredEntityPaths.Contains(ActorPath))
			{
				int32 EntityID = Stage->RegisterEntity(Actor);
				if (EntityID >= 0)
				{
					// Set default state in this Act
					if (ActID < Stage->Acts.Num())
					{
						Stage->Acts[ActID].EntityStateOverrides.Add(EntityID, 0);
					}
					Result.AddedEntityCount++;
				}
			}
		}
	}

	// Unregister removed actors
	TArray<int32> EntitysToRemove;
	for (const auto& Pair : Stage->EntityRegistry)
	{
		if (Pair.Value.IsValid())
		{
			FSoftObjectPath EntityPath = Pair.Value.ToSoftObjectPath();
			if (!CurrentActorPaths.Contains(EntityPath))
			{
				EntitysToRemove.Add(Pair.Key);
			}
		}
	}

	for (int32 EntityID : EntitysToRemove)
	{
		Stage->UnregisterEntity(EntityID);
		Result.RemovedEntityCount++;
	}

	return Result;
}

bool FDataLayerSynchronizer::CreateActFromDataLayer(
	AStage* Stage,
	UDataLayerAsset* ActDataLayerAsset,
	const FString& ActName,
	UWorld* World)
{
	if (!Stage || !ActDataLayerAsset)
	{
		return false;
	}

	// Create new Act
	int32 NewActIndex = Stage->Acts.Num();
	FAct NewAct;
	NewAct.SUID = FSUID::MakeActID(Stage->SUID.StageID, NewActIndex + 1);
	NewAct.DisplayName = ActName;
	NewAct.AssociatedDataLayer = ActDataLayerAsset;

	Stage->Acts.Add(NewAct);

	// Register actors in this DataLayer as Entitys
	TArray<AActor*> ActorsInAct = GetActorsInDataLayer(ActDataLayerAsset, World);
	for (AActor* Actor : ActorsInAct)
	{
		if (Actor && Actor != Stage)
		{
			int32 EntityID = Stage->RegisterEntity(Actor);
			if (EntityID >= 0)
			{
				Stage->Acts[NewActIndex].EntityStateOverrides.Add(EntityID, 0);
			}
		}
	}

	// Invalidate cache for this DataLayer
	FDataLayerSyncStatusCache::Get().InvalidateCache(ActDataLayerAsset);

	return true;
}

bool FDataLayerSynchronizer::RemoveActForDataLayer(
	AStage* Stage,
	UDataLayerAsset* RemovedDataLayerAsset)
{
	if (!Stage || !RemovedDataLayerAsset)
	{
		return false;
	}

	// Find and remove the Act
	for (int32 i = Stage->Acts.Num() - 1; i > 0; --i) // Start from 1 to skip default Act
	{
		if (Stage->Acts[i].AssociatedDataLayer == RemovedDataLayerAsset)
		{
			Stage->Acts.RemoveAt(i);
			return true;
		}
	}

	return false;
}

TArray<AActor*> FDataLayerSynchronizer::GetActorsInDataLayer(UDataLayerAsset* Asset, UWorld* World)
{
	TArray<AActor*> Result;

	if (!Asset || !World)
	{
		return Result;
	}

	UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(World);
	if (!Manager)
	{
		return Result;
	}

	const UDataLayerInstance* Instance = Manager->GetDataLayerInstance(Asset);
	if (!Instance)
	{
		return Result;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->HasDataLayers())
		{
			TArray<const UDataLayerInstance*> ActorDataLayers = Actor->GetDataLayerInstances();
			if (ActorDataLayers.Contains(Instance))
			{
				Result.Add(Actor);
			}
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
