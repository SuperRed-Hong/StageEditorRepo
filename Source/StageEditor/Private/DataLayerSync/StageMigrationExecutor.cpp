// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerSync/StageMigrationExecutor.h"
#include "Actors/Stage.h"
#include "Data/StageRegistryAsset.h"
#include "Engine/World.h"
#include "Subsystems/StageManagerSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogStageMigrationExecutor, Log, All);

//----------------------------------------------------------------
// Public API
//----------------------------------------------------------------

FMigrationExecutionResult FStageMigrationExecutor::ExecuteMigration(
	UWorld* World,
	const FMigrationAnalysisResult& Analysis,
	UStageRegistryAsset* Registry)
{
	if (!World)
	{
		return FMigrationExecutionResult::MakeError(TEXT("World is nullptr"));
	}

	if (!Registry)
	{
		return FMigrationExecutionResult::MakeError(TEXT("Registry is nullptr"));
	}

	if (!Analysis.HasIssues())
	{
		// No migration needed
		return FMigrationExecutionResult::MakeSuccess(0, 0, TEXT("No migration needed, all Stages are valid"));
	}

	UE_LOG(LogStageMigrationExecutor, Log, TEXT("Starting migration: %s"), *Analysis.GetSummary());

	// Open transaction for Undo support
	FScopedTransaction Transaction(NSLOCTEXT("StageEditor", "MigrateStages", "Migrate Stage IDs"));

	int32 MigratedCount = 0;
	int32 ReassignedCount = 0;

	// Process each Stage analysis
	for (const FStageMigrationAnalysis& StageAnalysis : Analysis.StageAnalyses)
	{
		if (StageAnalysis.Status == EStageMigrationStatus::Valid)
		{
			// Already valid, register directly (if not already in Registry)
			if (!Registry->IsStageIDRegistered(StageAnalysis.CurrentStageID))
			{
				if (RegisterStageInRegistry(StageAnalysis.Stage, Registry))
				{
					MigratedCount++;
					UE_LOG(LogStageMigrationExecutor, Log,
						TEXT("Registered valid Stage '%s' (ID: %d)"),
						*StageAnalysis.StageName, StageAnalysis.CurrentStageID);
				}
			}
			continue;
		}

		// Stage needs migration
		AStage* Stage = StageAnalysis.Stage;
		if (!IsValid(Stage))
		{
			UE_LOG(LogStageMigrationExecutor, Error,
				TEXT("Stage '%s' is invalid, skipping"), *StageAnalysis.StageName);
			continue;
		}

		// Step 1: Apply new StageID (if changed)
		if (StageAnalysis.WillChangeID())
		{
			if (ApplyNewStageID(Stage, StageAnalysis.NewStageID))
			{
				ReassignedCount++;
				UE_LOG(LogStageMigrationExecutor, Log,
					TEXT("Reassigned Stage '%s': %d → %d"),
					*StageAnalysis.StageName,
					StageAnalysis.CurrentStageID,
					StageAnalysis.NewStageID);
			}
			else
			{
				UE_LOG(LogStageMigrationExecutor, Error,
					TEXT("Failed to reassign Stage '%s'"), *StageAnalysis.StageName);
				continue;
			}
		}

		// Step 2: Register in Registry
		if (RegisterStageInRegistry(Stage, Registry))
		{
			MigratedCount++;
			UE_LOG(LogStageMigrationExecutor, Log,
				TEXT("Registered Stage '%s' (ID: %d)"),
				*StageAnalysis.StageName, Stage->SUID.StageID);
		}
		else
		{
			UE_LOG(LogStageMigrationExecutor, Error,
				TEXT("Failed to register Stage '%s' in Registry"), *StageAnalysis.StageName);
		}
	}

	// Mark Registry as dirty
	Registry->MarkPackageDirty();

	// Generate migration report
	FString Report = GenerateMigrationReport(Analysis, MigratedCount, ReassignedCount);

	UE_LOG(LogStageMigrationExecutor, Log, TEXT("Migration completed: %d migrated, %d reassigned"),
		MigratedCount, ReassignedCount);

	return FMigrationExecutionResult::MakeSuccess(MigratedCount, ReassignedCount, Report);
}

//----------------------------------------------------------------
// Private Helpers
//----------------------------------------------------------------

bool FStageMigrationExecutor::ApplyNewStageID(AStage* Stage, int32 NewStageID)
{
	if (!IsValid(Stage))
	{
		return false;
	}

	// Modify for transaction support
	Stage->Modify();

	// Update StageID
	Stage->SUID.StageID = NewStageID;

	// Mark package dirty
	Stage->MarkPackageDirty();

	return true;
}

bool FStageMigrationExecutor::RegisterStageInRegistry(AStage* Stage, UStageRegistryAsset* Registry)
{
	if (!Stage || !Registry)
	{
		return false;
	}

	// Check if already registered
	if (Registry->IsStageIDRegistered(Stage->SUID.StageID))
	{
		UE_LOG(LogStageMigrationExecutor, Verbose,
			TEXT("Stage '%s' (ID: %d) already registered in Registry"),
			*Stage->GetStageName(), Stage->SUID.StageID);
		return true;
	}

	// Modify Registry for transaction support
	Registry->Modify();

	// Create registry entry manually (don't use AllocateAndRegister, ID already assigned)
	FStageRegistryEntry Entry;
	Entry.StageID = Stage->SUID.StageID;
	Entry.StageName = Stage->GetStageName();
	Entry.RegistrationTime = FDateTime::Now();

	// Get OwnerLevel
	if (UWorld* World = Stage->GetWorld())
	{
		Entry.OwnerLevel = TSoftObjectPtr<UWorld>(World);
	}

	// Add to Registry
	Registry->RegisteredStages.Add(Entry.StageID, Entry);

	// Update NextStageID if necessary
	if (Entry.StageID >= Registry->NextStageID)
	{
		Registry->NextStageID = Entry.StageID + 1;
	}

	// Mark Registry dirty
	Registry->MarkPackageDirty();

	return true;
}

FString FStageMigrationExecutor::GenerateMigrationReport(
	const FMigrationAnalysisResult& Analysis,
	int32 MigratedCount,
	int32 ReassignedCount)
{
	FString Report;
	Report += TEXT("=== Stage Migration Report ===\n\n");
	Report += TEXT("Migration completed successfully!\n\n");
	Report += FString::Printf(TEXT("Total Stages: %d\n"), Analysis.GetTotalStageCount());
	Report += FString::Printf(TEXT("Migrated: %d\n"), MigratedCount);
	Report += FString::Printf(TEXT("Reassigned IDs: %d\n\n"), ReassignedCount);

	if (ReassignedCount > 0)
	{
		Report += TEXT("Details:\n");
		for (const FStageMigrationAnalysis& StageAnalysis : Analysis.StageAnalyses)
		{
			if (StageAnalysis.WillChangeID())
			{
				Report += FString::Printf(TEXT("  - [%s] %d → %d (%s)\n"),
					*StageAnalysis.StageName,
					StageAnalysis.CurrentStageID,
					StageAnalysis.NewStageID,
					*UEnum::GetValueAsString(StageAnalysis.Status));
			}
		}
	}

	Report += TEXT("\n=============================\n");
	return Report;
}
