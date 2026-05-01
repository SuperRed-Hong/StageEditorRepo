// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerSync/DataLayerSyncStatus.h"
#include "DataLayerSync/DataLayerSyncUtils.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "Subsystems/StageEditorSubsystem.h"
#include "Actors/Stage.h"
#include "Core/StageCoreTypes.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "StageEditorDataLayerSync"

// Use shared utilities
using namespace StageDataLayerSyncUtils;

//----------------------------------------------------------------
// Helper functions for status detection
//----------------------------------------------------------------

namespace
{
	// Define log category for this file
	DEFINE_LOG_CATEGORY_STATIC(LogStageDataLayerSync, Log, All);

	/**
	 * Detect Stage-level changes: compare current child DataLayers with registered Acts.
	 * Also checks if any child Act DataLayers are in NotImported state.
	 * Optimized: Uses pointer comparison instead of string matching for O(A) instead of O(A²)
	 */
	void DetectStageLevelChanges(const AStage* Stage, const UDataLayerAsset* Asset, FDataLayerSyncStatusInfo& OutInfo)
	{
		if (!Stage || !Asset)
		{
			return;
		}

		UWorld* World = GetEditorWorld();
		if (!World)
		{
			return;
		}

		UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(World);
		if (!Manager)
		{
			return;
		}

		UStageManagerSubsystem* Subsystem = GetStageManagerSubsystem();

		// Collect current child DataLayer Assets (pointer set)
		TSet<UDataLayerAsset*> CurrentChildAssets;
		TMap<UDataLayerAsset*, FString> AssetToName;  // For display names

		Manager->ForEachDataLayerInstance([&](UDataLayerInstance* Instance)
		{
			if (Instance && Instance->GetParent())
			{
				if (Instance->GetParent()->GetAsset() == Asset)
				{
					// GetAsset() returns const, use const_cast for set operations
					// This is safe as we're only using for pointer comparison
					UDataLayerAsset* ChildAsset = const_cast<UDataLayerAsset*>(Instance->GetAsset());
					if (ChildAsset)
					{
						CurrentChildAssets.Add(ChildAsset);
						AssetToName.Add(ChildAsset, Instance->GetDataLayerShortName());
					}
				}
			}
			return true; // Continue iteration
		});

		// Collect registered Act DataLayer Assets (pointer set)
		TSet<UDataLayerAsset*> RegisteredActAssets;
		for (const FAct& Act : Stage->Acts)
		{
			if (Act.AssociatedDataLayer)
			{
				RegisteredActAssets.Add(Act.AssociatedDataLayer);
			}
		}

		// Detect new child DataLayers: in current but not in registered - O(A)
		for (UDataLayerAsset* ChildAsset : CurrentChildAssets)
		{
			if (!RegisteredActAssets.Contains(ChildAsset))
			{
				FString* Name = AssetToName.Find(ChildAsset);
				OutInfo.NewChildDataLayers.Add(Name ? *Name : ChildAsset->GetName());
			}
		}

		// Detect removed child DataLayers: in registered but not in current - O(A)
		for (UDataLayerAsset* RegAsset : RegisteredActAssets)
		{
			if (!CurrentChildAssets.Contains(RegAsset))
			{
				OutInfo.RemovedChildDataLayers.Add(RegAsset->GetName());
			}
		}

		// Check if any registered child Act DataLayers are in NotImported state
		// This happens when:
		// 1. Child DataLayer is registered to an Act
		// 2. But the child's associated Stage is not loaded (WP streaming)
		// 3. Or the child DataLayer has no corresponding Stage registration
		for (UDataLayerAsset* RegAsset : RegisteredActAssets)
		{
			if (CurrentChildAssets.Contains(RegAsset))
			{
				// Child exists in scene and is registered - check its status recursively
				// Note: We use a non-recursive check to avoid infinite loops
				// Just check if it can find an associated Stage
				AStage* ChildStage = Subsystem ? Subsystem->FindStageByDataLayer(RegAsset) : nullptr;
				if (!ChildStage)
				{
					// Child DataLayer exists but has no associated Stage loaded
					FString* Name = AssetToName.Find(RegAsset);
					OutInfo.NotImportedChildDataLayers.Add(Name ? *Name : RegAsset->GetName());
				}
			}
		}
	}

	/**
	 * Detect Act-level changes: compare current Actor members with EntityRegistry.
	 */
	void DetectActLevelChanges(const AStage* Stage, int32 ActID, const UDataLayerAsset* Asset, FDataLayerSyncStatusInfo& OutInfo)
	{
		if (!Stage || !Asset)
		{
			return;
		}

		UWorld* World = GetEditorWorld();
		if (!World)
		{
			return;
		}

		UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(World);
		if (!Manager)
		{
			return;
		}

		// Get the DataLayerInstance for this Asset
		const UDataLayerInstance* Instance = Manager->GetDataLayerInstance(Asset);
		if (!Instance)
		{
			return;
		}

		// Get current actors in this DataLayer
		TSet<FSoftObjectPath> CurrentActorPaths;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && Actor->HasDataLayers())
			{
				TArray<const UDataLayerInstance*> ActorDataLayers = Actor->GetDataLayerInstances();
				if (ActorDataLayers.Contains(Instance))
				{
					CurrentActorPaths.Add(FSoftObjectPath(Actor));
				}
			}
		}

		// Get registered Entitys from Stage's EntityRegistry
		TSet<FSoftObjectPath> RegisteredEntityPaths;
		for (const auto& Pair : Stage->EntityRegistry)
		{
			if (Pair.Value.IsValid())
			{
				RegisteredEntityPaths.Add(Pair.Value.ToSoftObjectPath());
			}
		}

		// Count differences
		for (const FSoftObjectPath& Path : CurrentActorPaths)
		{
			if (!RegisteredEntityPaths.Contains(Path))
			{
				OutInfo.AddedActorCount++;
			}
		}

		for (const FSoftObjectPath& Path : RegisteredEntityPaths)
		{
			if (!CurrentActorPaths.Contains(Path))
			{
				OutInfo.RemovedActorCount++;
			}
		}
	}
}

//----------------------------------------------------------------
// FDataLayerSyncStatusDetector Implementation
//----------------------------------------------------------------

FDataLayerSyncStatusInfo FDataLayerSyncStatusDetector::DetectStatus(const UDataLayerAsset* Asset)
{
	FDataLayerSyncStatusInfo Info;

	if (!Asset)
	{
		UE_LOG(LogStageDataLayerSync, Warning, TEXT("[SyncStatus] DetectStatus: Asset is null"));
		Info.Status = EDataLayerSyncStatus::NotImported;
		return Info;
	}

	// Use Registry-based lookup instead of runtime cache (optimized for SyncStatus detection)
	UStageEditorSubsystem* EditorSub = GEditor ? GEditor->GetEditorSubsystem<UStageEditorSubsystem>() : nullptr;
	if (!EditorSub)
	{
		UE_LOG(LogStageDataLayerSync, Warning, TEXT("[SyncStatus] DetectStatus: StageEditorSubsystem is null for Asset '%s'"), *Asset->GetName());
		Info.Status = EDataLayerSyncStatus::NotImported;
		return Info;
	}

	// Reverse lookup: find Stage by DataLayer using Registry (works even if Stage not yet loaded)
	AStage* Stage = EditorSub->FindStageByDataLayerInRegistry(const_cast<UDataLayerAsset*>(Asset));
	if (!Stage)
	{
		UE_LOG(LogStageDataLayerSync, Verbose, TEXT("[SyncStatus] DetectStatus: FindStageByDataLayerInRegistry returned null for Asset '%s' (Stage not imported or not yet loaded)"), *Asset->GetName());
		Info.Status = EDataLayerSyncStatus::NotImported;
		return Info;
	}

	UE_LOG(LogStageDataLayerSync, Verbose, TEXT("[SyncStatus] DetectStatus: Found Stage '%s' (ID:%d) for Asset '%s'"),
		*Stage->GetActorLabel(), Stage->GetStageID(), *Asset->GetName());

	// Determine if this is Stage-level or Act-level DataLayer
	if (Stage->StageDataLayerAsset == Asset)
	{
		// Stage-level: compare child DataLayers with registered Acts
		DetectStageLevelChanges(Stage, Asset, Info);
	}
	else
	{
		// Act-level: compare Actor members with EntityRegistry
		UStageManagerSubsystem* RuntimeSub = GetStageManagerSubsystem();
		int32 ActID = RuntimeSub ? RuntimeSub->FindActIDByDataLayer(Stage, const_cast<UDataLayerAsset*>(Asset)) : INDEX_NONE;
		DetectActLevelChanges(Stage, ActID, Asset, Info);
	}

	// Determine final status
	Info.Status = Info.HasChanges()
		? EDataLayerSyncStatus::OutOfSync
		: EDataLayerSyncStatus::Synced;

	return Info;
}

FText FDataLayerSyncStatusDetector::GenerateTooltip(const FDataLayerSyncStatusInfo& StatusInfo)
{
	switch (StatusInfo.Status)
	{
	case EDataLayerSyncStatus::Synced:
		return LOCTEXT("StatusSynced", "Synced - No changes detected");

	case EDataLayerSyncStatus::NotImported:
		return LOCTEXT("StatusNotImported", "Not Imported, or associated Stage Actor not loaded (WP streaming)");

	case EDataLayerSyncStatus::OutOfSync:
		break; // Continue to generate detailed tooltip
	}

	// Generate detailed change description
	TArray<FText> Lines;

	// New child DataLayers
	for (const FString& Name : StatusInfo.NewChildDataLayers)
	{
		Lines.Add(FText::Format(
			LOCTEXT("NewChildDetected", "New child detected: {0}"),
			FText::FromString(Name)));
	}

	// Removed child DataLayers
	for (const FString& Name : StatusInfo.RemovedChildDataLayers)
	{
		Lines.Add(FText::Format(
			LOCTEXT("ChildRemoved", "Child removed: {0}"),
			FText::FromString(Name)));
	}

	// NotImported child DataLayers (registered but Stage not loaded)
	for (const FString& Name : StatusInfo.NotImportedChildDataLayers)
	{
		Lines.Add(FText::Format(
			LOCTEXT("ChildNotImported", "Child not imported: {0}"),
			FText::FromString(Name)));
	}

	// Actor changes
	if (StatusInfo.AddedActorCount > 0 || StatusInfo.RemovedActorCount > 0)
	{
		Lines.Add(FText::Format(
			LOCTEXT("ActorChanges", "Actor changes: +{0} / -{1}"),
			FText::AsNumber(StatusInfo.AddedActorCount),
			FText::AsNumber(StatusInfo.RemovedActorCount)));
	}

	// Action hint
	Lines.Add(LOCTEXT("ClickToSync", "Click Sync to update"));

	return FText::Join(FText::FromString(TEXT("\n")), Lines);
}

FLinearColor FDataLayerSyncStatusDetector::GetStatusColor(EDataLayerSyncStatus Status)
{
	switch (Status)
	{
	case EDataLayerSyncStatus::NotImported:
		return FLinearColor(0.3f, 0.5f, 0.8f); // Blue

	case EDataLayerSyncStatus::Synced:
		return FLinearColor(0.3f, 0.8f, 0.3f); // Green

	case EDataLayerSyncStatus::OutOfSync:
		return FLinearColor(0.9f, 0.6f, 0.2f); // Orange

	default:
		return FLinearColor::White;
	}
}

FName FDataLayerSyncStatusDetector::GetStatusIconName(EDataLayerSyncStatus Status)
{
	switch (Status)
	{
	case EDataLayerSyncStatus::NotImported:
		return FName("Icons.Info");

	case EDataLayerSyncStatus::Synced:
		return FName("Icons.Check");

	case EDataLayerSyncStatus::OutOfSync:
		return FName("Icons.Warning");

	default:
		return FName("Icons.Help");
	}
}

TArray<FString> FDataLayerSyncStatusDetector::GetCurrentChildDataLayerNames(const UDataLayerAsset* Asset)
{
	TArray<FString> Result;

	if (!Asset)
	{
		return Result;
	}

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Result;
	}

	UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(World);
	if (!Manager)
	{
		return Result;
	}

	// Iterate all DataLayerInstances, find children of this Asset
	Manager->ForEachDataLayerInstance([&](UDataLayerInstance* Instance)
	{
		if (Instance && Instance->GetParent())
		{
			if (Instance->GetParent()->GetAsset() == Asset)
			{
				Result.Add(Instance->GetDataLayerShortName());
			}
		}
		return true; // Continue iteration
	});

	return Result;
}

TArray<TSoftObjectPtr<AActor>> FDataLayerSyncStatusDetector::GetCurrentActors(const UDataLayerAsset* Asset)
{
	TArray<TSoftObjectPtr<AActor>> Result;

	if (!Asset)
	{
		return Result;
	}

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Result;
	}

	UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(World);
	if (!Manager)
	{
		return Result;
	}

	// Get the DataLayerInstance for this Asset
	const UDataLayerInstance* Instance = Manager->GetDataLayerInstance(Asset);
	if (!Instance)
	{
		return Result;
	}

	// Iterate all Actors in the world, check if they belong to this DataLayer
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->HasDataLayers())
		{
			TArray<const UDataLayerInstance*> ActorDataLayers = Actor->GetDataLayerInstances();
			if (ActorDataLayers.Contains(Instance))
			{
				Result.Add(TSoftObjectPtr<AActor>(Actor));
			}
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
