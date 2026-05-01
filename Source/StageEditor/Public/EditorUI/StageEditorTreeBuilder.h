// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Forward declarations
class FStageEditorController;
struct FStageTreeItem;

/**
 * @brief Builds tree view data structure for StageEditorPanel.
 *
 * Responsibilities:
 * - Build hierarchical tree items from Stage data
 * - Provide child item retrieval for tree view
 *
 * Note: Row widget generation is handled by SStageTreeRow in StageEditorPanel.cpp,
 *       not by this TreeBuilder class.
 *
 * Tree Structure:
 * - Stage
 *   - Acts Folder
 *     - Act 0 (Default)
 *       - Entity 1 (State: X)
 *       - Entity 2 (State: Y)
 *     - Act 1
 *       - ...
 *   - Entities Folder
 *     - Entity 1
 *     - Entity 2
 */
class STAGEEDITOR_API FStageEditorTreeBuilder
{
public:
	explicit FStageEditorTreeBuilder(TWeakPtr<FStageEditorController> InController);
	~FStageEditorTreeBuilder();

	//----------------------------------------------------------------
	// Tree Building
	//----------------------------------------------------------------

	/**
	 * Build root tree items from all Stages in World.
	 * @param OutRootItems - Populated with root Stage items
	 * @param OutActorPathToTreeItem - Map of actor paths to tree items (for selection sync)
	 */
	void RebuildTreeItems(
		TArray<TSharedPtr<FStageTreeItem>>& OutRootItems,
		TMap<FString, TWeakPtr<FStageTreeItem>>& OutActorPathToTreeItem);

	//----------------------------------------------------------------
	// Tree View Callbacks
	//----------------------------------------------------------------

	/**
	 * Get children for tree item (required by STreeView).
	 * @param Item - Parent item
	 * @param OutChildren - Populated with child items
	 */
	void OnGetChildren(
		TSharedPtr<FStageTreeItem> Item,
		TArray<TSharedPtr<FStageTreeItem>>& OutChildren);

private:
	/** Weak reference to Controller */
	TWeakPtr<FStageEditorController> WeakController;

	//----------------------------------------------------------------
	// Tree Building Helpers
	//----------------------------------------------------------------

	/**
	 * Build tree items for a single Stage.
	 * @param Stage - Stage actor
	 * @param OutActorPathToTreeItem - Map to populate with actor paths
	 * @return Root Stage tree item
	 */
	TSharedPtr<FStageTreeItem> BuildStageItem(
		class AStage* Stage,
		TMap<FString, TWeakPtr<FStageTreeItem>>& OutActorPathToTreeItem);

	/**
	 * Build Acts Folder and all Act items for a Stage.
	 * @param Stage - Stage actor
	 * @param StageItem - Parent Stage tree item
	 * @param OutActorPathToTreeItem - Map to populate with actor paths
	 * @return Acts Folder tree item
	 */
	TSharedPtr<FStageTreeItem> BuildActsFolder(
		class AStage* Stage,
		TSharedPtr<FStageTreeItem> StageItem,
		TMap<FString, TWeakPtr<FStageTreeItem>>& OutActorPathToTreeItem);

	/**
	 * Build Entities Folder for a Stage.
	 * @param Stage - Stage actor
	 * @param StageItem - Parent Stage tree item
	 * @param OutActorPathToTreeItem - Map to populate with actor paths
	 * @return Entities Folder tree item
	 */
	TSharedPtr<FStageTreeItem> BuildEntitiesFolder(
		class AStage* Stage,
		TSharedPtr<FStageTreeItem> StageItem,
		TMap<FString, TWeakPtr<FStageTreeItem>>& OutActorPathToTreeItem);

	/**
	 * Get display name for Entity actor.
	 * @param EntityActor - Entity actor
	 * @param EntityID - Entity ID
	 * @return Display name
	 */
	FString GetEntityDisplayName(class AActor* EntityActor, int32 EntityID) const;
};
