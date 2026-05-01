// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Forward declarations
class FStageEditorController;
class UStageRegistryAsset;
struct FStageTreeItem;
template<typename ItemType> class STreeView;

/**
 * @brief Manages state and caching for StageEditorPanel.
 *
 * Responsibilities:
 * - Cached registry management
 * - Cached status texts (Lock Status, Sync Status) - PERFORMANCE FIX!
 * - Expansion state persistence
 * - Viewport selection synchronization
 *
 * PERFORMANCE NOTE:
 * This class contains the per-frame query fix. Status texts are cached here
 * and only updated on demand, preventing 60 queries/second to Source Control.
 */
class STAGEEDITOR_API FStageEditorStateManager
{
public:
	explicit FStageEditorStateManager(TWeakPtr<FStageEditorController> InController);
	~FStageEditorStateManager();

	//----------------------------------------------------------------
	// Registry Management
	//----------------------------------------------------------------

	/**
	 * Gets the cached Registry pointer (updates cache if stale).
	 * Optimized for per-frame calls (returns cached pointer).
	 */
	UStageRegistryAsset* GetCachedRegistry() const;

	/**
	 * Checks if the current level has a StageRegistryAsset.
	 */
	bool HasRegistryAsset() const;

	/**
	 * Invalidate all caches (called when map changes).
	 * Forces re-query of Registry and status texts on next access.
	 */
	void InvalidateAllCaches();

	/**
	 * Get the current Registry asset path (for UI display).
	 * Returns user-friendly path like "/Game/Maps/Level1_StageRegistry"
	 */
	FString GetRegistryAssetPath() const;

	//----------------------------------------------------------------
	// Status Text Caching (Performance Fix)
	//----------------------------------------------------------------

	/** Get cached lock status text (FAST: reads member variable). */
	FText GetCachedLockStatusText() const { return CachedLockStatusText; }

	/** Get cached sync status text (FAST: reads member variable). */
	FText GetCachedSyncStatusText() const { return CachedSyncStatusText; }

	/** Get cached sync warning visibility (FAST: reads member variable). */
	EVisibility GetCachedSyncWarningVisibility() const { return CachedSyncWarningVisibility; }

	/** Get cached lock status bar visibility (FAST: reads member variable). */
	EVisibility GetCachedLockStatusBarVisibility() const { return CachedLockStatusBarVisibility; }

	/**
	 * Refresh lock status text from Subsystem.
	 * EXPENSIVE: Queries Source Control. Call only when needed.
	 */
	void RefreshLockStatusText();

	/**
	 * Refresh sync status text from Controller.
	 * EXPENSIVE: Traverses all Stages. Call only when needed.
	 */
	void RefreshSyncStatusText();

	/**
	 * Refresh all cached status texts.
	 * Convenience method for initial load or explicit refresh.
	 */
	void RefreshAllStatusTexts();

	//----------------------------------------------------------------
	// Expansion State Management
	//----------------------------------------------------------------

	/**
	 * Save the current expansion state of tree items.
	 * @param RootItems - Root items of the tree
	 * @param OutExpansionState - Set to fill with expanded item hashes
	 * @param TreeView - Tree view widget to query expansion state
	 */
	void SaveExpansionState(
		const TArray<TSharedPtr<FStageTreeItem>>& RootItems,
		TSet<FString>& OutExpansionState,
		TSharedPtr<STreeView<TSharedPtr<FStageTreeItem>>> TreeView) const;

	/**
	 * Restore expansion state to tree items.
	 * @param RootItems - Root items of the tree
	 * @param InExpansionState - Set of expanded item hashes
	 * @param TreeView - Tree view widget to apply expansion
	 */
	void RestoreExpansionState(
		const TArray<TSharedPtr<FStageTreeItem>>& RootItems,
		const TSet<FString>& InExpansionState,
		TSharedPtr<STreeView<TSharedPtr<FStageTreeItem>>> TreeView);

	/**
	 * Generate unique hash for a tree item (for expansion tracking).
	 * @param Item - Tree item to hash
	 * @return Unique string identifier for the item
	 */
	FString GetItemHash(TSharedPtr<FStageTreeItem> Item) const;

	//----------------------------------------------------------------
	// Viewport Selection Sync
	//----------------------------------------------------------------

	/**
	 * Synchronize tree selection with viewport selection.
	 * @param TreeView - Tree view widget
	 * @param RootItems - Root items of the tree
	 */
	void SyncTreeSelectionWithViewport(
		TSharedPtr<STreeView<TSharedPtr<FStageTreeItem>>> TreeView,
		const TArray<TSharedPtr<FStageTreeItem>>& RootItems);

	/**
	 * Update actor-to-tree-item map for selection sync.
	 * @param RootItems - Root items of the tree
	 * @param ActorPathToTreeItem - Map to populate
	 */
	void UpdateActorPathMap(
		const TArray<TSharedPtr<FStageTreeItem>>& RootItems,
		TMap<FString, TWeakPtr<FStageTreeItem>>& ActorPathToTreeItem);

private:
	/** Weak reference to Controller */
	TWeakPtr<FStageEditorController> WeakController;

	/** Cached Registry pointer (avoids repeated lookups) */
	mutable TWeakObjectPtr<UStageRegistryAsset> CachedRegistry;

	/**
	 * Cached Lock Status text (PERFORMANCE: Updated on demand, not per frame).
	 * Before fix: Queried 60 times/second (6000ms lag).
	 * After fix: Updated only on explicit refresh or operations.
	 */
	FText CachedLockStatusText;

	/**
	 * Cached Sync Status text (PERFORMANCE: Updated on demand, not per frame).
	 * Before fix: Calculated 60 times/second with Stage traversal.
	 * After fix: Updated only on explicit refresh or operations.
	 */
	FText CachedSyncStatusText;

	/**
	 * Cached Sync Warning visibility (PERFORMANCE: Updated on demand, not per frame).
	 * Before fix: GetSyncWarningVisibility() called 60 times/second with CalculateSyncStatus().
	 * After fix: Updated only on explicit refresh or operations.
	 */
	EVisibility CachedSyncWarningVisibility;

	/**
	 * Cached Lock Status Bar visibility (PERFORMANCE: Updated on demand, not per frame).
	 * Determined by Registry mode (Multi vs Solo).
	 */
	EVisibility CachedLockStatusBarVisibility;

	/**
	 * Helper: Recursively find items matching hash.
	 * @param Items - Items to search
	 * @param Hash - Hash to match
	 * @param OutItems - Matching items
	 */
	void RecursivelyFindItemByHash(
		const TArray<TSharedPtr<FStageTreeItem>>& Items,
		const FString& Hash,
		TArray<TSharedPtr<FStageTreeItem>>& OutItems) const;

	/**
	 * Helper: Recursively update actor path map.
	 * @param Items - Items to process
	 * @param ActorPathToTreeItem - Map to update
	 */
	void RecursivelyUpdateActorPathMap(
		const TArray<TSharedPtr<FStageTreeItem>>& Items,
		TMap<FString, TWeakPtr<FStageTreeItem>>& ActorPathToTreeItem);
};
