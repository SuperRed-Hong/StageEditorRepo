// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/StageRegistryAsset.h"
#include "Actors/Stage.h"
#include "Engine/World.h"
#include "EngineUtils.h" // For TActorIterator
#include "WorldPartition/DataLayer/DataLayerAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogStageRegistry, Log, All);

//----------------------------------------------------------------
// Lifecycle
//----------------------------------------------------------------

UStageRegistryAsset::UStageRegistryAsset()
	: NextStageID(1)
	, CollaborationMode(ECollaborationMode::Solo)
	, CreationTime(FDateTime::Now())
{
}

#if WITH_EDITOR
void UStageRegistryAsset::PostEditImport()
{
	Super::PostEditImport();

	// StageRegistryAsset should NOT be manually duplicated.
	// If it was, reset to empty state to prevent ID conflicts and data corruption.
	UE_LOG(LogStageRegistry, Error,
		TEXT("StageRegistryAsset was duplicated! This is NOT supported. "
		     "All registry data has been reset. Please delete this copy and use the original, "
		     "or re-initialize via StageEditorPanel."));

	NextStageID = 1;
	RecycledStageIDs.Empty();
	RegisteredStages.Empty();
	OwnerLevel = nullptr;
	CreationTime = FDateTime::Now();
}
#endif

void UStageRegistryAsset::Initialize(const TSoftObjectPtr<UWorld>& InOwnerLevel, ECollaborationMode InMode)
{
	OwnerLevel = InOwnerLevel;
	CollaborationMode = InMode;
	CreationTime = FDateTime::Now();
	NextStageID = 1;
	RegisteredStages.Empty();

	UE_LOG(LogStageRegistry, Log, TEXT("Initialized StageRegistry for Level '%s', Mode=%s"),
		*OwnerLevel.GetAssetName(),
		CollaborationMode == ECollaborationMode::Solo ? TEXT("Solo") : TEXT("Multi"));
}

//----------------------------------------------------------------
// Core API
//----------------------------------------------------------------

int32 UStageRegistryAsset::AllocateAndRegister(AStage* Stage)
{
	if (!Stage)
	{
		UE_LOG(LogStageRegistry, Error, TEXT("AllocateAndRegister failed: Stage is nullptr"));
		return 0;
	}

	// NOTE: SC Offline handling moved to Controller layer (Phase 13.9)
	// Registry layer no longer checks SC status - allows offline work with temporary IDs
	// Controller decides whether to call this method or allocate temporary negative ID

	// Phase 13.10: Allocate StageID - first try recycled IDs (user-approved), then increment
	int32 NewStageID;
	if (RecycledStageIDs.Num() > 0)
	{
		// Reuse smallest recycled ID (take from beginning for sequential order)
		// RecycledStageIDs is sorted ascending, so index 0 is smallest
		NewStageID = RecycledStageIDs[0];
		RecycledStageIDs.RemoveAt(0);
		UE_LOG(LogStageRegistry, Log, TEXT("Reusing recycled StageID=%d (%d IDs still in recycle pool)"),
			NewStageID, RecycledStageIDs.Num());
	}
	else
	{
		// No recycled IDs available, allocate new one
		NewStageID = NextStageID++;
	}

	// Create registry entry
	FStageRegistryEntry NewEntry = CreateEntryFromStage(Stage, NewStageID);

	// Add to registry
	RegisteredStages.Add(NewStageID, NewEntry);

	// Mark package dirty (important for persistence)
	MarkPackageDirty();

	UE_LOG(LogStageRegistry, Log, TEXT("Registered Stage '%s' with StageID=%d (NextStageID now %d)"),
		*NewEntry.StageName, NewStageID, NextStageID);

	return NewStageID;
}

bool UStageRegistryAsset::Unregister(int32 StageID)
{
	if (!IsStageIDRegistered(StageID))
	{
		UE_LOG(LogStageRegistry, Warning, TEXT("Unregister failed: StageID=%d not found"), StageID);
		return false;
	}

	const FStageRegistryEntry* Entry = GetStageEntry(StageID);
	FString StageName = Entry ? Entry->StageName : TEXT("Unknown");

	// Remove from registry
	int32 RemovedCount = RegisteredStages.Remove(StageID);

	if (RemovedCount > 0)
	{
		// Note: We don't add to recycle pool automatically.
		// User must click "Recycle IDs" button to detect and recycle ID gaps.
		UE_LOG(LogStageRegistry, Log, TEXT("Unregistered Stage '%s' (StageID=%d). Use 'Recycle IDs' to reclaim this ID."),
			*StageName, StageID);

		MarkPackageDirty();
		return true;
	}

	return false;
}

bool UStageRegistryAsset::IsStageIDRegistered(int32 StageID) const
{
	return RegisteredStages.Contains(StageID);
}

const FStageRegistryEntry* UStageRegistryAsset::GetStageEntry(int32 StageID) const
{
	return RegisteredStages.Find(StageID);
}

FStageRegistryEntry* UStageRegistryAsset::GetMutableStageEntry(int32 StageID)
{
	return RegisteredStages.Find(StageID);
}

int32 UStageRegistryAsset::FindStageIDByDataLayer(const UDataLayerAsset* DataLayerAsset) const
{
	if (!DataLayerAsset)
	{
		return 0;
	}

	// Only check Stage-level DataLayer in Registry
	// Act-level DataLayers are NOT stored in Registry (to avoid frequent modifications)
	for (const auto& Pair : RegisteredStages)
	{
		const FStageRegistryEntry& Entry = Pair.Value;

		// Check Stage-level DataLayer
		// Compare using TSoftObjectPtr directly to avoid loading issues
		if (Entry.StageDataLayerAsset.Get() == DataLayerAsset)
		{
			UE_LOG(LogStageRegistry, Verbose, TEXT("FindStageIDByDataLayer: Found match for '%s' in StageID=%d"),
				*DataLayerAsset->GetName(), Entry.StageID);
			return Entry.StageID;
		}

		// Debug: Log comparison for debugging
		if (Entry.StageDataLayerAsset.IsValid())
		{
			UE_LOG(LogStageRegistry, VeryVerbose, TEXT("FindStageIDByDataLayer: Comparing '%s' with StageID=%d DataLayer='%s' - NO MATCH"),
				*DataLayerAsset->GetName(), Entry.StageID, *Entry.StageDataLayerAsset.Get()->GetName());
		}
		else
		{
			UE_LOG(LogStageRegistry, Log, TEXT("FindStageIDByDataLayer: StageID=%d has null StageDataLayerAsset in Registry (may not be created yet or not saved)"),
				Entry.StageID);
		}
	}

	// NOTE: This is expected for Act-level DataLayers (they are NOT stored in Registry)
	UE_LOG(LogStageRegistry, Verbose, TEXT("FindStageIDByDataLayer: No match found for DataLayer '%s' in %d registered stages (this is expected for Act DataLayers)"),
		*DataLayerAsset->GetName(), RegisteredStages.Num());
	return 0; // Not found
}

//----------------------------------------------------------------
// ID Recycling (Manual - Detect ID Gaps)
//----------------------------------------------------------------

int32 UStageRegistryAsset::DetectIDGaps(TArray<int32>& OutGapIDs) const
{
	OutGapIDs.Empty();

	// Scan IDs from 1 to NextStageID-1
	// Any ID not in RegisteredStages and not in RecycledStageIDs is a gap
	for (int32 ID = 1; ID < NextStageID; ++ID)
	{
		if (!RegisteredStages.Contains(ID) && !RecycledStageIDs.Contains(ID))
		{
			OutGapIDs.Add(ID);
		}
	}

	return OutGapIDs.Num();
}

int32 UStageRegistryAsset::RecycleIDGaps()
{
	TArray<int32> GapIDs;
	DetectIDGaps(GapIDs);

	if (GapIDs.Num() == 0)
	{
		UE_LOG(LogStageRegistry, Log, TEXT("RecycleIDGaps: No ID gaps to recycle"));
		return 0;
	}

	int32 RecycledCount = GapIDs.Num();

	// Add all gap IDs to recycled pool
	for (int32 ID : GapIDs)
	{
		RecycledStageIDs.Add(ID);
	}

	// Sort recycled IDs for cleaner display and predictable allocation order
	RecycledStageIDs.Sort();

	UE_LOG(LogStageRegistry, Log, TEXT("RecycleIDGaps: Recycled %d ID gap(s). Recycle pool now has %d IDs available for reuse."),
		RecycledCount, RecycledStageIDs.Num());

	MarkPackageDirty();

	return RecycledCount;
}

//----------------------------------------------------------------
// Validation & Integrity
//----------------------------------------------------------------

bool UStageRegistryAsset::ValidateIntegrity(TArray<FString>& OutErrors) const
{
	OutErrors.Empty();

	// Check 1: NextStageID should be greater than all registered StageIDs
	int32 MaxStageID = 0;
	for (const auto& Pair : RegisteredStages)
	{
		MaxStageID = FMath::Max(MaxStageID, Pair.Key);
	}

	if (NextStageID <= MaxStageID)
	{
		OutErrors.Add(FString::Printf(
			TEXT("NextStageID (%d) is not greater than max registered StageID (%d). Risk of ID collision!"),
			NextStageID, MaxStageID));
	}

	// Check 2: Detect duplicate StageIDs (should never happen, but paranoia check)
	TSet<int32> SeenIDs;
	for (const auto& Pair : RegisteredStages)
	{
		if (SeenIDs.Contains(Pair.Key))
		{
			OutErrors.Add(FString::Printf(TEXT("Duplicate StageID detected: %d"), Pair.Key));
		}
		SeenIDs.Add(Pair.Key);
	}

	// Check 3: All OwnerLevels should be valid soft references
	for (const auto& Pair : RegisteredStages)
	{
		if (Pair.Value.OwnerLevel.IsNull())
		{
			OutErrors.Add(FString::Printf(
				TEXT("Stage '%s' (StageID=%d) has invalid OwnerLevel reference"),
				*Pair.Value.StageName, Pair.Key));
		}
	}

	// Check 4: OwnerLevel should match Registry's OwnerLevel (for consistency)
	if (!OwnerLevel.IsNull())
	{
		for (const auto& Pair : RegisteredStages)
		{
			if (!Pair.Value.OwnerLevel.IsNull() && Pair.Value.OwnerLevel != OwnerLevel)
			{
				OutErrors.Add(FString::Printf(
					TEXT("Stage '%s' (StageID=%d) has mismatched OwnerLevel (expected '%s', got '%s')"),
					*Pair.Value.StageName, Pair.Key,
					*OwnerLevel.GetAssetName(), *Pair.Value.OwnerLevel.GetAssetName()));
			}
		}
	}

	return OutErrors.Num() == 0;
}

TArray<int32> UStageRegistryAsset::DetectConflicts() const
{
	TArray<int32> Conflicts;
	TMap<int32, int32> StageIDCount;

	// Count occurrences of each StageID
	for (const auto& Pair : RegisteredStages)
	{
		int32 StageID = Pair.Key;
		StageIDCount.FindOrAdd(StageID, 0)++;
	}

	// Find StageIDs with count > 1
	for (const auto& CountPair : StageIDCount)
	{
		if (CountPair.Value > 1)
		{
			Conflicts.Add(CountPair.Key);
		}
	}

	return Conflicts;
}

//----------------------------------------------------------------
// Debugging
//----------------------------------------------------------------

FString UStageRegistryAsset::GetDebugSummary() const
{
	FString ModeString = (CollaborationMode == ECollaborationMode::Solo) ? TEXT("Solo") : TEXT("Multi");

	// Detect ID gaps (unused IDs not yet recycled)
	TArray<int32> GapIDs;
	DetectIDGaps(GapIDs);
	int32 GapCount = GapIDs.Num();
	int32 RecycledCount = GetRecycledIDCount();

	if (GapCount > 0 || RecycledCount > 0)
	{
		return FString::Printf(TEXT("NextStageID=%d, Registered=%d, Gaps=%d, Recycled=%d, Mode=%s, Level='%s'"),
			NextStageID, RegisteredStages.Num(), GapCount, RecycledCount, *ModeString, *OwnerLevel.GetAssetName());
	}
	return FString::Printf(TEXT("NextStageID=%d, Registered=%d, Mode=%s, Level='%s'"),
		NextStageID, RegisteredStages.Num(), *ModeString, *OwnerLevel.GetAssetName());
}

void UStageRegistryAsset::DumpToLog() const
{
	UE_LOG(LogStageRegistry, Display, TEXT("========== StageRegistry Dump =========="));
	UE_LOG(LogStageRegistry, Display, TEXT("Summary: %s"), *GetDebugSummary());
	UE_LOG(LogStageRegistry, Display, TEXT("Creation Time: %s"), *CreationTime.ToString());

	// Show ID gaps (can be recycled)
	TArray<int32> GapIDs;
	DetectIDGaps(GapIDs);
	if (GapIDs.Num() > 0)
	{
		FString GapList;
		for (int32 i = 0; i < FMath::Min(GapIDs.Num(), 10); ++i)
		{
			if (i > 0) GapList += TEXT(", ");
			GapList += FString::FromInt(GapIDs[i]);
		}
		if (GapIDs.Num() > 10)
		{
			GapList += FString::Printf(TEXT("... (+%d more)"), GapIDs.Num() - 10);
		}
		UE_LOG(LogStageRegistry, Display, TEXT("ID Gaps (can be recycled): [%s] (use 'Recycle IDs' to recycle)"), *GapList);
	}

	// Show recycled IDs (available for reuse)
	if (RecycledStageIDs.Num() > 0)
	{
		FString RecycledList;
		for (int32 i = 0; i < FMath::Min(RecycledStageIDs.Num(), 10); ++i)
		{
			if (i > 0) RecycledList += TEXT(", ");
			RecycledList += FString::FromInt(RecycledStageIDs[i]);
		}
		if (RecycledStageIDs.Num() > 10)
		{
			RecycledList += FString::Printf(TEXT("... (+%d more)"), RecycledStageIDs.Num() - 10);
		}
		UE_LOG(LogStageRegistry, Display, TEXT("Recycled IDs (available for reuse): [%s]"), *RecycledList);
	}

	UE_LOG(LogStageRegistry, Display, TEXT("Registered Stages:"));

	if (RegisteredStages.Num() == 0)
	{
		UE_LOG(LogStageRegistry, Display, TEXT("  (No stages registered)"));
	}
	else
	{
		for (const auto& Pair : RegisteredStages)
		{
			const FStageRegistryEntry& Entry = Pair.Value;
			FString LevelInstanceInfo = Entry.LevelInstanceID.IsValid()
				? FString::Printf(TEXT(", LevelInstanceID=%s"), *Entry.LevelInstanceID.ToString())
				: TEXT("");

			UE_LOG(LogStageRegistry, Display, TEXT("  [%d] '%s' - Level='%s'%s (Registered: %s)"),
				Entry.StageID,
				*Entry.StageName,
				*Entry.OwnerLevel.GetAssetName(),
				*LevelInstanceInfo,
				*Entry.RegistrationTime.ToString());
		}
	}

	// Validate integrity
	TArray<FString> Errors;
	bool bValid = ValidateIntegrity(Errors);
	if (bValid)
	{
		UE_LOG(LogStageRegistry, Display, TEXT("Integrity: ✅ VALID"));
	}
	else
	{
		UE_LOG(LogStageRegistry, Warning, TEXT("Integrity: ❌ INVALID (%d errors)"), Errors.Num());
		for (const FString& Error : Errors)
		{
			UE_LOG(LogStageRegistry, Warning, TEXT("  - %s"), *Error);
		}
	}

	UE_LOG(LogStageRegistry, Display, TEXT("========================================"));
}

//----------------------------------------------------------------
// Duplicate ID Detection & Repair
//----------------------------------------------------------------

int32 UStageRegistryAsset::DetectDuplicateStageIDs(UWorld* WorldContext, TMap<int32, TArray<AStage*>>& OutDuplicates) const
{
	OutDuplicates.Empty();

	if (!WorldContext)
	{
		UE_LOG(LogStageRegistry, Error, TEXT("DetectDuplicateStageIDs: WorldContext is null"));
		return 0;
	}

	// Collect all Stages grouped by StageID
	TMap<int32, TArray<AStage*>> StagesByID;
	for (TActorIterator<AStage> It(WorldContext); It; ++It)
	{
		AStage* Stage = *It;
		if (Stage && Stage->GetStageID() > 0)
		{
			StagesByID.FindOrAdd(Stage->GetStageID()).Add(Stage);
		}
	}

	// Find duplicates (groups with more than 1 Stage)
	int32 DuplicateGroupCount = 0;
	for (const auto& Pair : StagesByID)
	{
		if (Pair.Value.Num() > 1)
		{
			OutDuplicates.Add(Pair.Key, Pair.Value);
			DuplicateGroupCount++;

			UE_LOG(LogStageRegistry, Warning, TEXT("DetectDuplicateStageIDs: Found %d Stages with StageID=%d"),
				Pair.Value.Num(), Pair.Key);
		}
	}

	if (DuplicateGroupCount > 0)
	{
		UE_LOG(LogStageRegistry, Warning, TEXT("DetectDuplicateStageIDs: Total %d duplicate ID groups found"), DuplicateGroupCount);
	}
	else
	{
		UE_LOG(LogStageRegistry, Log, TEXT("DetectDuplicateStageIDs: No duplicate StageIDs found"));
	}

	return DuplicateGroupCount;
}

int32 UStageRegistryAsset::RepairDuplicateStageIDs(UWorld* WorldContext, TArray<FString>& OutRepairLog)
{
	OutRepairLog.Empty();

	if (!WorldContext)
	{
		OutRepairLog.Add(TEXT("Error: WorldContext is null"));
		return 0;
	}

	// Detect duplicates
	TMap<int32, TArray<AStage*>> Duplicates;
	int32 DuplicateGroupCount = DetectDuplicateStageIDs(WorldContext, Duplicates);

	if (DuplicateGroupCount == 0)
	{
		OutRepairLog.Add(TEXT("No duplicate StageIDs found. Registry is healthy."));
		return 0;
	}

	int32 RepairedCount = 0;

	// Process each duplicate group
	for (auto& Pair : Duplicates)
	{
		int32 OriginalID = Pair.Key;
		TArray<AStage*>& DuplicateStages = Pair.Value;

		// First Stage keeps its original ID
		AStage* FirstStage = DuplicateStages[0];
		OutRepairLog.Add(FString::Printf(TEXT("StageID %d: '%s' keeps original ID"),
			OriginalID, *FirstStage->GetStageName()));

		// Remaining Stages get new IDs
		for (int32 i = 1; i < DuplicateStages.Num(); ++i)
		{
			AStage* DuplicateStage = DuplicateStages[i];

			// Allocate new unique ID (using recycled pool or NextStageID)
			int32 NewStageID;
			if (RecycledStageIDs.Num() > 0)
			{
				NewStageID = RecycledStageIDs.Pop();
			}
			else
			{
				NewStageID = NextStageID++;
			}

			// Update Stage actor
			DuplicateStage->Modify();
			int32 OldID = DuplicateStage->GetStageID();
			DuplicateStage->SUID.StageID = NewStageID;

			// Update Registry entry (remove old, add new)
			if (FStageRegistryEntry* OldEntry = RegisteredStages.Find(OriginalID))
			{
				// Check if this entry matches the duplicate Stage (by name or other identifier)
				// For safety, we create a new entry for the duplicate
				FStageRegistryEntry NewEntry = CreateEntryFromStage(DuplicateStage, NewStageID);
				RegisteredStages.Add(NewStageID, NewEntry);
			}
			else
			{
				// Entry not in registry, add it
				FStageRegistryEntry NewEntry = CreateEntryFromStage(DuplicateStage, NewStageID);
				RegisteredStages.Add(NewStageID, NewEntry);
			}

			RepairedCount++;

			OutRepairLog.Add(FString::Printf(TEXT("StageID %d: '%s' reassigned to new ID %d"),
				OldID, *DuplicateStage->GetStageName(), NewStageID));

			UE_LOG(LogStageRegistry, Log, TEXT("RepairDuplicateStageIDs: Reassigned Stage '%s' from ID=%d to ID=%d"),
				*DuplicateStage->GetStageName(), OldID, NewStageID);
		}
	}

	MarkPackageDirty();

	OutRepairLog.Add(FString::Printf(TEXT("Repair complete: %d Stages reassigned new IDs"), RepairedCount));
	UE_LOG(LogStageRegistry, Log, TEXT("RepairDuplicateStageIDs: Repaired %d duplicate Stages"), RepairedCount);

	return RepairedCount;
}

//----------------------------------------------------------------
// Internal Helpers
//----------------------------------------------------------------

FStageRegistryEntry UStageRegistryAsset::CreateEntryFromStage(AStage* Stage, int32 StageID) const
{
	check(Stage);

	FStageRegistryEntry Entry;
	Entry.StageID = StageID;
	Entry.StageName = Stage->GetStageName();
	Entry.RegistrationTime = FDateTime::Now();

	// Get OwnerLevel
	if (UWorld* World = Stage->GetWorld())
	{
		Entry.OwnerLevel = TSoftObjectPtr<UWorld>(World);
	}

	// Populate Stage-level DataLayer (low-frequency data, safe for Registry)
	Entry.StageDataLayerAsset = TSoftObjectPtr<UDataLayerAsset>(Stage->StageDataLayerAsset);

	// NOTE: We do NOT store Act DataLayers in Registry
	// Reason: Acts are frequently added/removed by users, storing them would cause
	// frequent Registry modifications and increase Source Control conflicts in multi-user mode.
	// Act DataLayers should be queried directly from Stage->Acts at runtime.

	// TODO (Phase 13.2): Populate LevelInstanceID if Stage is in a Level Instance
	// This requires P0-1 validation of FLevelInstanceID stability
	// For now, leave it as default (invalid Guid)

	return Entry;
}
