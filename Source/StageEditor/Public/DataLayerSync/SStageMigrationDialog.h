// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "DataLayerSync/StageMigrationTypes.h"

// Forward declarations
class SWindow;
class UWorld;
class UStageRegistryAsset;

/**
 * @brief Stage migration preview dialog.
 *
 * Displays migration analysis results and allows user to:
 * - Review proposed StageID changes
 * - See conflict resolution strategy
 * - Confirm or cancel migration
 * - View detailed statistics
 *
 * Usage:
 * ```cpp
 * FMigrationAnalysisResult Analysis = FStageMigrationAnalyzer::AnalyzeStages(World);
 * if (Analysis.HasIssues())
 * {
 *     SStageMigrationDialog::ShowDialog(World, Analysis, Registry);
 * }
 * ```
 *
 * UI Layout:
 * ```
 * ┌────────────────────────────────────────────────┐
 * │ Stage Migration Preview                        │
 * ├────────────────────────────────────────────────┤
 * │ Summary:                                       │
 * │   Total Stages: 10                             │
 * │   Uninitialized: 3                             │
 * │   Conflicts: 2                                 │
 * ├────────────────────────────────────────────────┤
 * │ Details:                                       │
 * │ ┌────────────────────────────────────────────┐ │
 * │ │ [Stage_A]  0 → 1  (Uninitialized)         │ │
 * │ │ [Stage_B]  5 → 6  (Conflict resolved)     │ │
 * │ │ [Stage_C]  Valid  (No change)             │ │
 * │ └────────────────────────────────────────────┘ │
 * ├────────────────────────────────────────────────┤
 * │            [Cancel]  [Execute Migration]       │
 * └────────────────────────────────────────────────┘
 * ```
 *
 * @see FStageMigrationAnalyzer - Generates analysis data
 * @see FStageMigrationExecutor - Executes migration on confirmation
 */
class STAGEEDITOR_API SStageMigrationDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SStageMigrationDialog) {}
	SLATE_END_ARGS()

	/**
	 * Construct the dialog widget.
	 *
	 * @param InArgs - Slate arguments
	 * @param InWorld - Target World
	 * @param InAnalysis - Migration analysis result
	 * @param InRegistry - StageRegistryAsset to register Stages into
	 */
	void Construct(
		const FArguments& InArgs,
		UWorld* InWorld,
		const FMigrationAnalysisResult& InAnalysis,
		UStageRegistryAsset* InRegistry);

	/**
	 * Show migration dialog as modal window.
	 *
	 * Blocks until user clicks Cancel or Execute Migration.
	 *
	 * @param World - Target World
	 * @param Analysis - Migration analysis result
	 * @param Registry - StageRegistryAsset
	 * @return true if user confirmed and migration executed successfully
	 */
	static bool ShowDialog(
		UWorld* World,
		const FMigrationAnalysisResult& Analysis,
		UStageRegistryAsset* Registry);

private:
	//----------------------------------------------------------------
	// UI Construction
	//----------------------------------------------------------------

	/**
	 * Build summary section (Total/Uninitialized/Conflicts).
	 */
	TSharedRef<SWidget> BuildSummarySection();

	/**
	 * Build details list (per-Stage analysis).
	 */
	TSharedRef<SWidget> BuildDetailsSection();

	/**
	 * Build button row (Cancel/Execute).
	 */
	TSharedRef<SWidget> BuildButtonRow();

	//----------------------------------------------------------------
	// Event Handlers
	//----------------------------------------------------------------

	/**
	 * Handle "Execute Migration" button click.
	 */
	FReply OnExecuteMigration();

	/**
	 * Handle "Cancel" button click.
	 */
	FReply OnCancel();

	//----------------------------------------------------------------
	// Helpers
	//----------------------------------------------------------------

	/**
	 * Get color for migration status.
	 *
	 * - Valid: Green
	 * - Uninitialized: Yellow
	 * - Conflict: Orange
	 * - Corrupted: Red
	 */
	FSlateColor GetStatusColor(EStageMigrationStatus Status) const;

	/**
	 * Get icon for migration status.
	 */
	const FSlateBrush* GetStatusIcon(EStageMigrationStatus Status) const;

private:
	//----------------------------------------------------------------
	// Data
	//----------------------------------------------------------------

	/** Target World */
	UWorld* World = nullptr;

	/** Migration analysis result */
	FMigrationAnalysisResult Analysis;

	/** StageRegistryAsset to register Stages into */
	UStageRegistryAsset* Registry = nullptr;

	/** Parent window (for closing) */
	TWeakPtr<SWindow> ParentWindow;

	/** Result flag (set to true if migration executed successfully) */
	bool bMigrationExecuted = false;
};
