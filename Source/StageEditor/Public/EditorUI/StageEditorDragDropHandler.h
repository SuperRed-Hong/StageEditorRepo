// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Input/DragAndDrop.h"

// Forward declarations
class FStageEditorController;
struct FStageTreeItem;
class SStageEditorPanel;
class FDragDropEvent;
class AActor;

/**
 * @brief Handles drag-and-drop operations for StageEditorPanel tree view.
 *
 * Responsibilities:
 * - Actor drag from World Outliner → Stage registration
 * - Entity drag between Acts (internal tree drag)
 * - Drag visual feedback (highlight target rows)
 * - Drop validation (prevent invalid operations)
 *
 * Supported Drag Operations:
 * 1. World Outliner Actor → Stage: Register actors as Entities
 * 2. Registered Entity → Act: Add Entity to Act with State 0
 */
class STAGEEDITOR_API FStageEditorDragDropHandler
{
public:
	explicit FStageEditorDragDropHandler(
		TWeakPtr<FStageEditorController> InController,
		TWeakPtr<SStageEditorPanel> InPanel);
	~FStageEditorDragDropHandler();

	//----------------------------------------------------------------
	// Drag Events
	//----------------------------------------------------------------

	/**
	 * Handle drag detected on tree row.
	 * @param MyGeometry - Widget geometry
	 * @param MouseEvent - Mouse event
	 * @param Item - Tree item being dragged
	 * @return Drag operation
	 */
	FReply OnRowDragDetected(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent,
		TSharedPtr<FStageTreeItem> Item);

	/**
	 * Handle drag entering tree row.
	 * @param DragDropEvent - Drag drop event
	 * @param TargetItem - Tree item being entered
	 */
	void OnRowDragEnter(
		const FDragDropEvent& DragDropEvent,
		TSharedPtr<FStageTreeItem> TargetItem);

	/**
	 * Handle drag leaving tree row.
	 * @param DragDropEvent - Drag drop event
	 * @param TargetItem - Tree item being left
	 */
	void OnRowDragLeave(
		const FDragDropEvent& DragDropEvent,
		TSharedPtr<FStageTreeItem> TargetItem);

	/**
	 * Handle drop on tree row.
	 * @param DragDropEvent - Drag drop event
	 * @param TargetItem - Tree item receiving drop
	 * @return Reply indicating if drop was handled
	 */
	FReply OnRowDrop(
		const FDragDropEvent& DragDropEvent,
		TSharedPtr<FStageTreeItem> TargetItem);

	//----------------------------------------------------------------
	// Drag State
	//----------------------------------------------------------------

	/**
	 * Get current drag target item (for visual feedback).
	 * @return Item currently being dragged over
	 */
	TSharedPtr<FStageTreeItem> GetDraggedOverItem() const { return DraggedOverItem; }

	/**
	 * Clear drag state.
	 */
	void ClearDragState();

	/**
	 * Check if item should show drag highlight.
	 * @param Item - Item to check
	 * @return true if item is currently drag target
	 */
	bool IsDragTarget(TSharedPtr<FStageTreeItem> Item) const;

	/**
	 * Check if item is descendant of another item.
	 * @param Item - Item to check
	 * @param Ancestor - Potential ancestor
	 * @return true if Item is descendant of Ancestor
	 */
	bool IsItemOrDescendantOf(
		TSharedPtr<FStageTreeItem> Item,
		TSharedPtr<FStageTreeItem> Ancestor) const;

private:
	/** Weak reference to Controller */
	TWeakPtr<FStageEditorController> WeakController;

	/** Weak reference to Panel (for UI refresh) */
	TWeakPtr<SStageEditorPanel> WeakPanel;

	/** Currently dragged-over item (for visual feedback) */
	TSharedPtr<FStageTreeItem> DraggedOverItem;

	//----------------------------------------------------------------
	// Drop Validation
	//----------------------------------------------------------------

	/**
	 * Validate if Actor drag from World Outliner can be dropped.
	 * @param TargetItem - Target tree item
	 * @return true if drop is valid
	 */
	bool CanDropActorsFromOutliner(TSharedPtr<FStageTreeItem> TargetItem) const;

	/**
	 * Validate if Entity drag within tree can be dropped.
	 * @param SourceItem - Source Entity item
	 * @param TargetItem - Target tree item
	 * @return true if drop is valid
	 */
	bool CanDropEntityToAct(
		TSharedPtr<FStageTreeItem> SourceItem,
		TSharedPtr<FStageTreeItem> TargetItem) const;

	//----------------------------------------------------------------
	// Drop Handlers
	//----------------------------------------------------------------

	/**
	 * Handle Actor drop from World Outliner.
	 * @param DragDropEvent - Drag drop event
	 * @param TargetItem - Target tree item
	 * @return Reply indicating if drop was handled
	 */
	FReply HandleActorDrop(
		const FDragDropEvent& DragDropEvent,
		TSharedPtr<FStageTreeItem> TargetItem);

	/**
	 * Handle Entity drop within tree (internal drag).
	 * @param DragDropEvent - Drag drop event
	 * @param TargetItem - Target tree item
	 * @return Reply indicating if drop was handled
	 */
	FReply HandleEntityDrop(
		const FDragDropEvent& DragDropEvent,
		TSharedPtr<FStageTreeItem> TargetItem);

	/**
	 * Find parent Stage for a tree item.
	 * @param Item - Tree item
	 * @return Parent Stage actor (nullptr if not found)
	 */
	class AStage* FindStageForItem(TSharedPtr<FStageTreeItem> Item) const;

	/**
	 * Find parent Act ID for a tree item.
	 * @param Item - Tree item
	 * @return Parent Act ID (-1 if not found)
	 */
	int32 FindActIDForItem(TSharedPtr<FStageTreeItem> Item) const;

	/**
	 * Refresh UI after drop operation.
	 */
	void RefreshUI();
};
