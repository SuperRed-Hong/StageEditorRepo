// Copyright Stage Editor Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class AStage;
class AActor;
class FStageEditorController;
class FStageEditorMode;
class SSceneOutliner;
class UWorld;

/**
 * StageEditor Outliner widget that wraps SSceneOutliner.
 * Displays Stage-Act-Entity hierarchy using SceneOutliner framework.
 *
 * This widget:
 * - Creates and manages the SceneOutliner with custom Mode and Hierarchy
 * - Provides high-level API for StageEditorPanel
 * - Handles refresh and selection synchronization
 *
 * Tree Structure:
 * - Stage
 *   - Acts (Folder)
 *     - Act 0 (Default)
 *       - Entity 1 (State: X)
 *     - Act 1
 *   - Entities (Folder)
 *     - Entity 1
 *     - Entity 2
 */
class STAGEEDITOR_API SStageEditorOutliner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SStageEditorOutliner) {}
		/** The controller for Stage operations */
		SLATE_ARGUMENT(TSharedPtr<FStageEditorController>, Controller)
		/** The world to display (optional, defaults to editor world) */
		SLATE_ARGUMENT(UWorld*, World)
	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	/** Destructor */
	virtual ~SStageEditorOutliner();

	//----------------------------------------------------------------
	// Public API
	//----------------------------------------------------------------

	/** Refresh the tree view */
	void Refresh();

	/** Get selected Stages */
	TArray<AStage*> GetSelectedStages() const;

	/** Get selected Entity actors */
	TArray<AActor*> GetSelectedEntities() const;

	/** Set selection to a specific Stage */
	void SetSelection(AStage* Stage);

	/** Set selection to a specific Entity */
	void SetSelection(AActor* Entity);

	/** Expand all items */
	void ExpandAll();

	/** Collapse all items */
	void CollapseAll();

	/** Get the underlying SceneOutliner */
	TSharedPtr<SSceneOutliner> GetSceneOutliner() const { return SceneOutliner; }

	/** Get the Mode */
	FStageEditorMode* GetMode() const { return ModePtr; }

private:
	/** Create the SceneOutliner with our custom mode */
	TSharedRef<SWidget> CreateOutliner();

	/** Controller reference */
	TWeakPtr<FStageEditorController> WeakController;

	/** World to display */
	TWeakObjectPtr<UWorld> World;

	/** The SceneOutliner widget */
	TSharedPtr<SSceneOutliner> SceneOutliner;

	/**
	 * Raw pointer to our custom mode.
	 * Note: SceneOutliner takes ownership of the Mode, so we only store a raw pointer for access.
	 * Do NOT delete this pointer - SceneOutliner manages its lifetime.
	 */
	FStageEditorMode* ModePtr = nullptr;
};
