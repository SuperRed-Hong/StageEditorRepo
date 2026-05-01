// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerSync/StageMigrationAnalyzer.h"
#include "Actors/Stage.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Logging/StageEditorLog.h"

//----------------------------------------------------------------
// Public API
//----------------------------------------------------------------

FMigrationAnalysisResult FStageMigrationAnalyzer::AnalyzeStages(UWorld* World)
{
	FMigrationAnalysisResult Result;

	if (!World)
	{
		UE_LOG(LogStageMigration, Error, TEXT("AnalyzeStages: World is nullptr"));
		return Result;
	}

	// Step 1: Find all Stage Actors
	TArray<AStage*> AllStages = FindAllStages(World);

	if (AllStages.Num() == 0)
	{
		UE_LOG(LogStageMigration, Log, TEXT("AnalyzeStages: No Stages found in World '%s'"), *World->GetName());
		return Result;
	}

	UE_LOG(LogStageMigration, Log, TEXT("AnalyzeStages: Found %d Stages in World '%s'"), AllStages.Num(), *World->GetName());

	// Step 2: Detect StageID conflicts
	TMap<int32, TArray<AStage*>> ConflictMap = DetectStageIDConflicts(AllStages);

	// Step 3: Calculate next available StageID
	int32 MaxStageID = 0;
	for (AStage* Stage : AllStages)
	{
		if (Stage->SUID.StageID > MaxStageID)
		{
			MaxStageID = Stage->SUID.StageID;
		}
	}
	Result.NextAvailableStageID = MaxStageID + 1;

	UE_LOG(LogStageMigration, Log, TEXT("AnalyzeStages: Next available StageID: %d"), Result.NextAvailableStageID);

	// Step 4: Analyze each Stage
	TSet<int32> ProcessedConflicts; // Track which conflicts we've already handled

	for (AStage* Stage : AllStages)
	{
		FStageMigrationAnalysis Analysis;
		Analysis.Stage = Stage;
		Analysis.CurrentStageID = Stage->SUID.StageID;
		Analysis.NewStageID = Stage->SUID.StageID; // Default: keep current ID
		Analysis.StageName = Stage->GetStageName();

		// Check 1: Uninitialized (StageID = 0)
		if (Stage->SUID.StageID == 0)
		{
			Analysis.Status = EStageMigrationStatus::Uninitialized;
			Analysis.IssueDescription = TEXT("StageID is uninitialized (0)");
			Analysis.SuggestedAction = FString::Printf(TEXT("Assign new ID: %d"), Result.NextAvailableStageID);
			Analysis.NewStageID = Result.NextAvailableStageID;
			Result.NextAvailableStageID++;
			Result.UninitializedStageCount++;
		}
		// Check 2: Conflict (duplicate StageID)
		else if (ConflictMap.Contains(Stage->SUID.StageID) && ConflictMap[Stage->SUID.StageID].Num() > 1)
		{
			TArray<AStage*>& ConflictingStages = ConflictMap[Stage->SUID.StageID];

			// First Stage with this ID keeps it, others get reassigned
			bool bIsFirst = (ConflictingStages[0] == Stage);

			if (bIsFirst)
			{
				// First Stage keeps its ID
				Analysis.Status = EStageMigrationStatus::Conflict;
				Analysis.IssueDescription = FString::Printf(
					TEXT("StageID %d is used by %d Stages (this one will keep it)"),
					Stage->SUID.StageID,
					ConflictingStages.Num());
				Analysis.SuggestedAction = TEXT("Keep current ID (first occurrence)");
				// NewStageID remains unchanged
			}
			else
			{
				// Subsequent Stages get reassigned
				Analysis.Status = EStageMigrationStatus::Conflict;
				Analysis.IssueDescription = FString::Printf(
					TEXT("StageID %d conflicts with another Stage"),
					Stage->SUID.StageID);
				Analysis.SuggestedAction = FString::Printf(TEXT("Reassign to new ID: %d"), Result.NextAvailableStageID);
				Analysis.NewStageID = Result.NextAvailableStageID;
				Result.NextAvailableStageID++;
			}

			// Count conflict only once per StageID
			if (!ProcessedConflicts.Contains(Stage->SUID.StageID))
			{
				Result.ConflictStageCount += ConflictingStages.Num();
				ProcessedConflicts.Add(Stage->SUID.StageID);
			}
		}
		// Check 3: Valid
		else
		{
			Analysis.Status = EStageMigrationStatus::Valid;
			Analysis.IssueDescription = TEXT("Stage is correctly initialized");
			Analysis.SuggestedAction = TEXT("No action needed");
			Result.ValidStageCount++;
		}

		Result.StageAnalyses.Add(Analysis);
	}

	// Log summary
	UE_LOG(LogStageMigration, Log, TEXT("AnalyzeStages: Analysis complete - %s"), *Result.GetSummary());

	return Result;
}

//----------------------------------------------------------------
// Private Helpers
//----------------------------------------------------------------

TArray<AStage*> FStageMigrationAnalyzer::FindAllStages(UWorld* World)
{
	TArray<AStage*> Stages;

	if (!World)
	{
		UE_LOG(LogStageMigration, Error, TEXT("FindAllStages: World is nullptr"));
		return Stages;
	}

	UE_LOG(LogStageMigration, Log, TEXT("FindAllStages: Searching for Stages in World '%s'"), *World->GetName());

	// Use TActorIterator to find all AStage instances
	int32 TotalActorsChecked = 0;
	for (TActorIterator<AStage> It(World); It; ++It)
	{
		TotalActorsChecked++;
		AStage* Stage = *It;
		if (IsValid(Stage))
		{
			UE_LOG(LogStageMigration, Log, TEXT("FindAllStages:   Found Stage: '%s' (StageID: %d)"),
				*Stage->GetActorLabel(), Stage->SUID.StageID);
			Stages.Add(Stage);
		}
	}

	UE_LOG(LogStageMigration, Log, TEXT("FindAllStages: Checked %d actors, found %d valid Stages"),
		TotalActorsChecked, Stages.Num());

	// Debug: If no Stages found in World Partition, check if actors are unloaded
	if (Stages.Num() == 0 && World->IsPartitionedWorld())
	{
		UE_LOG(LogStageMigration, Warning,
			TEXT("FindAllStages: No Stages found in World Partition level. Stages may be in unloaded regions."));
		UE_LOG(LogStageMigration, Warning,
			TEXT("FindAllStages:   Solution: Load the region containing the Stage Actor, or place Stage in always-loaded area."));
	}

	return Stages;
}

TMap<int32, TArray<AStage*>> FStageMigrationAnalyzer::DetectStageIDConflicts(const TArray<AStage*>& Stages)
{
	TMap<int32, TArray<AStage*>> ConflictMap;

	for (AStage* Stage : Stages)
	{
		if (!Stage)
		{
			continue;
		}

		int32 StageID = Stage->SUID.StageID;

		// Add to conflict map
		if (!ConflictMap.Contains(StageID))
		{
			ConflictMap.Add(StageID, TArray<AStage*>());
		}

		ConflictMap[StageID].Add(Stage);
	}

	// Filter out non-conflicts (only 1 Stage with that ID)
	TArray<int32> KeysToRemove;
	for (const auto& Pair : ConflictMap)
	{
		if (Pair.Value.Num() <= 1)
		{
			KeysToRemove.Add(Pair.Key);
		}
	}

	for (int32 Key : KeysToRemove)
	{
		ConflictMap.Remove(Key);
	}

	if (ConflictMap.Num() > 0)
	{
		UE_LOG(LogStageMigration, Warning, TEXT("DetectStageIDConflicts: Detected %d StageID conflicts"), ConflictMap.Num());
		for (const auto& Pair : ConflictMap)
		{
			UE_LOG(LogStageMigration, Warning, TEXT("DetectStageIDConflicts:   StageID %d: %d Stages"), Pair.Key, Pair.Value.Num());
		}
	}

	return ConflictMap;
}

int32 FStageMigrationAnalyzer::AllocateNewStageID(const TArray<AStage*>& Stages, int32& NextID)
{
	// Find a StageID that is not already used
	TSet<int32> UsedIDs;
	for (AStage* Stage : Stages)
	{
		if (Stage)
		{
			UsedIDs.Add(Stage->SUID.StageID);
		}
	}

	// Increment NextID until we find an unused one
	while (UsedIDs.Contains(NextID))
	{
		NextID++;
	}

	int32 NewID = NextID;
	NextID++; // Prepare for next allocation

	return NewID;
}

FStageMigrationAnalysis FStageMigrationAnalyzer::BuildAnalysis(
	AStage* Stage,
	EStageMigrationStatus Status,
	int32 NewStageID)
{
	FStageMigrationAnalysis Analysis;
	Analysis.Stage = Stage;
	Analysis.CurrentStageID = Stage ? Stage->SUID.StageID : 0;
	Analysis.NewStageID = NewStageID;
	Analysis.Status = Status;
	Analysis.StageName = Stage ? Stage->GetStageName() : TEXT("Unknown");

	switch (Status)
	{
	case EStageMigrationStatus::Valid:
		Analysis.IssueDescription = TEXT("Stage is correctly initialized");
		Analysis.SuggestedAction = TEXT("No action needed");
		break;

	case EStageMigrationStatus::Uninitialized:
		Analysis.IssueDescription = TEXT("StageID is uninitialized (0)");
		Analysis.SuggestedAction = FString::Printf(TEXT("Assign new ID: %d"), NewStageID);
		break;

	case EStageMigrationStatus::Conflict:
		Analysis.IssueDescription = FString::Printf(TEXT("StageID %d conflicts with another Stage"), Analysis.CurrentStageID);
		Analysis.SuggestedAction = FString::Printf(TEXT("Reassign to new ID: %d"), NewStageID);
		break;

	case EStageMigrationStatus::Corrupted:
		Analysis.IssueDescription = TEXT("Stage data is corrupted");
		Analysis.SuggestedAction = TEXT("Manual intervention required");
		break;
	}

	return Analysis;
}
