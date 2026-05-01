// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"

// Forward declarations
class FStageEditorController;
class UStageRegistryAsset;
struct FStageTreeItem;
class SStageEditorPanel;
class AActor;

/**
 * @brief Handles all action button callbacks for StageEditorPanel.
 *
 * Responsibilities:
 * - Stage operations (Create, Delete, Toggle MultiMode)
 * - Act operations (Create, Delete, Activate, Preview)
 * - Entity operations (Register, Unregister, Clean Orphaned)
 * - DataLayer operations (Import, Link, Sync)
 * - Source Control operations (CheckOut, Sync, View Changelist)
 *
 * All operations are wrapped in FScopedTransaction for Undo/Redo support.
 */
class STAGEEDITOR_API FStageEditorActionHandlers
{
public:
	explicit FStageEditorActionHandlers(
		TWeakPtr<FStageEditorController> InController,
		TWeakPtr<SStageEditorPanel> InPanel,
		TSharedPtr<FStructOnScope> InCreationSettings);
	~FStageEditorActionHandlers();

	//----------------------------------------------------------------
	// Button Actions (from StageEditorPanel toolbar)
	//----------------------------------------------------------------

	/** Handler for "Create Act" button */
	FReply OnCreateActClicked();

	/** Handler for "Register Selected Entities" button */
	FReply OnRegisterSelectedEntitiesClicked();

	/** Handler for "Create Stage BP" button */
	FReply OnCreateStageBPClicked();

	/** Handler for "Create Entity Actor BP" button */
	FReply OnCreateEntityActorBPClicked();

	/** Handler for "Create Entity Component BP" button */
	FReply OnCreateEntityComponentBPClicked();

	/** Handler for "Clean Orphaned Entities" button */
	FReply OnCleanOrphanedEntitiesClicked();

	/** Handler for "Create Registry" button */
	FReply OnCreateRegistryClicked();

	/** Handler for "Select Existing Registry" button */
	FReply OnSelectExistingRegistryClicked();

	/** Handler for "Sync Registry" button */
	FReply OnSyncRegistryClicked();

	/** Handler for "View Changelist" button */
	FReply OnViewChangelistClicked();

	/** Handler for "Convert to World Partition" button */
	FReply OnConvertToWorldPartitionClicked();

private:
	/** Weak reference to Controller */
	TWeakPtr<FStageEditorController> WeakController;

	/** Weak reference to Panel (for UI refresh) */
	TWeakPtr<SStageEditorPanel> WeakPanel;

	/** Asset creation settings (for Blueprint creation handlers) */
	TSharedPtr<FStructOnScope> CreationSettings;

	/** Get FAssetCreationSettings from CreationSettings */
	struct FAssetCreationSettings* GetSettings() const;

	/** Show confirmation dialog */
	bool ShowConfirmDialog(const FText& Title, const FText& Message) const;

	/** Refresh UI after operation */
	void RefreshUI();

	/** Check if level is World Partition (from Panel) */
	bool IsWorldPartitionLevel() const;
};
