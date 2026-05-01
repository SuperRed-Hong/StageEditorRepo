// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataLayerSync/StageMigrationTypes.h"

// Forward declarations
class AStage;
class UWorld;
class UStageRegistryAsset;

/**
 * @brief Stage migration executor.
 *
 * Executes migration based on FStageMigrationAnalyzer results:
 * - Creates StageRegistryAsset if missing
 * - Assigns new StageIDs to uninitialized Stages
 * - Resolves StageID conflicts by reassigning IDs
 * - Registers all Stages in Registry
 * - Generates migration report
 *
 * Usage:
 * ```cpp
 * FMigrationAnalysisResult Analysis = FStageMigrationAnalyzer::AnalyzeStages(World);
 * if (Analysis.HasIssues())
 * {
 *     FMigrationExecutionResult Result = FStageMigrationExecutor::ExecuteMigration(
 *         World, Analysis, Registry);
 *
 *     if (Result.IsSuccess())
 *     {
 *         UE_LOG(LogTemp, Log, TEXT("Migration succeeded: %s"), *Result.MigrationReport);
 *     }
 * }
 * ```
 *
 * Transaction Support:
 * - All Stage modifications wrapped in FScopedTransaction
 * - Supports Undo/Redo
 * - Rollback on failure
 *
 * @see FStageMigrationAnalyzer - Analyzes Stages before migration
 * @see Phase13_P0-3_MigrationPlan.md - Complete migration design
 */
class STAGEEDITOR_API FStageMigrationExecutor
{
public:
	/**
	 * Execute Stage migration based on analysis result.
	 *
	 * Workflow:
	 * 1. Open FScopedTransaction for Undo support
	 * 2. For each Stage needing migration:
	 *    - Modify() the Stage
	 *    - Update SUID.StageID to new value
	 *    - Register in RegistryAsset
	 * 3. Save RegistryAsset
	 * 4. Generate migration report
	 *
	 * @param World - Target World (Level)
	 * @param Analysis - Migration analysis result from FStageMigrationAnalyzer
	 * @param Registry - StageRegistryAsset to register Stages into
	 * @return Execution result with success/failure and report
	 */
	static FMigrationExecutionResult ExecuteMigration(
		UWorld* World,
		const FMigrationAnalysisResult& Analysis,
		UStageRegistryAsset* Registry);

private:
	/**
	 * Apply new StageID to a Stage.
	 *
	 * - Calls Stage->Modify() for transaction support
	 * - Updates SUID.StageID
	 * - Logs the change
	 *
	 * @param Stage - Stage to update
	 * @param NewStageID - New StageID
	 * @return true if successful
	 */
	static bool ApplyNewStageID(AStage* Stage, int32 NewStageID);

	/**
	 * Register a Stage in RegistryAsset.
	 *
	 * Uses Registry->AllocateAndRegister() if Stage is uninitialized,
	 * or manually creates FStageRegistryEntry for already-assigned IDs.
	 *
	 * @param Stage - Stage to register
	 * @param Registry - RegistryAsset
	 * @return true if successful
	 */
	static bool RegisterStageInRegistry(AStage* Stage, UStageRegistryAsset* Registry);

	/**
	 * Generate migration report summary.
	 *
	 * Format:
	 * ```
	 * Migration completed successfully:
	 * - Total Stages: 10
	 * - Migrated: 5
	 * - Reassigned IDs: 3
	 *
	 * Details:
	 * - [Stage_A] 0 → 1 (Uninitialized)
	 * - [Stage_B] 5 → 6 (Conflict resolved)
	 * ...
	 * ```
	 *
	 * @param Analysis - Original analysis result
	 * @param MigratedCount - Number of Stages migrated
	 * @param ReassignedCount - Number of IDs reassigned
	 * @return Report string
	 */
	static FString GenerateMigrationReport(
		const FMigrationAnalysisResult& Analysis,
		int32 MigratedCount,
		int32 ReassignedCount);
};
