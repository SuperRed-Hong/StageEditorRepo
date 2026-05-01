// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataLayerSync/StageMigrationTypes.h"

// Forward declarations
class AStage;
class UWorld;

/**
 * @brief Stage migration analyzer.
 *
 * Scans a Level for existing Stages and analyzes migration requirements:
 * - Detects uninitialized Stages (StageID = 0)
 * - Detects StageID conflicts (duplicate IDs)
 * - Proposes new StageIDs for problematic Stages
 *
 * Usage:
 * ```cpp
 * FMigrationAnalysisResult Result = FStageMigrationAnalyzer::AnalyzeStages(World);
 * if (Result.HasIssues())
 * {
 *     // Show migration dialog to user
 *     ShowMigrationDialog(Result);
 * }
 * ```
 *
 * Conflict Resolution Strategy:
 * - First Stage with conflicting ID keeps its ID
 * - Subsequent conflicting Stages get reassigned to new IDs
 * - Uninitialized Stages (ID=0) always get new IDs
 *
 * @see FStageMigrationExecutor - Executes migration based on analysis
 * @see Phase13_P0-3_MigrationPlan.md - Complete migration design
 */
class STAGEEDITOR_API FStageMigrationAnalyzer
{
public:
	/**
	 * Analyze all Stages in a Level.
	 *
	 * Workflow:
	 * 1. Find all Stage Actors in World
	 * 2. Detect StageID conflicts (duplicate IDs)
	 * 3. Calculate next available StageID
	 * 4. Categorize each Stage (Valid/Uninitialized/Conflict)
	 * 5. Propose new StageIDs for problematic Stages
	 *
	 * @param World - Target World (Level)
	 * @return Analysis result with issue detection and proposed solutions
	 */
	static FMigrationAnalysisResult AnalyzeStages(UWorld* World);

private:
	/**
	 * Find all Stage Actors in a World.
	 *
	 * Uses TActorIterator to scan the World for AStage instances.
	 *
	 * @param World - Target World
	 * @return Array of Stage Actors
	 */
	static TArray<AStage*> FindAllStages(UWorld* World);

	/**
	 * Detect StageID conflicts (duplicate IDs).
	 *
	 * Returns a map where:
	 * - Key: StageID
	 * - Value: Array of Stages with that ID (conflict if Num() > 1)
	 *
	 * @param Stages - Array of Stages to check
	 * @return Conflict map (StageID → Stages)
	 */
	static TMap<int32, TArray<AStage*>> DetectStageIDConflicts(const TArray<AStage*>& Stages);

	/**
	 * Allocate a new StageID that doesn't conflict.
	 *
	 * Ensures the returned ID is not already used by any Stage.
	 *
	 * @param Stages - Array of all Stages (to check for conflicts)
	 * @param NextID - Input/Output: Next available ID (will be incremented)
	 * @return New unique StageID
	 */
	static int32 AllocateNewStageID(const TArray<AStage*>& Stages, int32& NextID);

	/**
	 * Build analysis result for a single Stage.
	 *
	 * Categorizes the Stage and fills in IssueDescription/SuggestedAction.
	 *
	 * @param Stage - Stage to analyze
	 * @param Status - Migration status (Valid/Uninitialized/Conflict/Corrupted)
	 * @param NewStageID - Proposed new StageID (if changing)
	 * @return FStageMigrationAnalysis with full details
	 */
	static FStageMigrationAnalysis BuildAnalysis(
		AStage* Stage,
		EStageMigrationStatus Status,
		int32 NewStageID);
};
