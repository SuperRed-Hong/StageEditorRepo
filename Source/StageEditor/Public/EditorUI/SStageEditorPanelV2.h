// Copyright Stage Editor Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FStageEditorController;
class SStageEditorOutliner;
class AStage;
class UStageRegistryAsset;
class IStructureDetailsView;

// Forward declare for FAssetCreationSettings
struct FAssetCreationSettings;

/**
 * @brief Stage Editor Panel V2 - SceneOutliner-based implementation
 * @details This is the new implementation using SceneOutliner framework.
 *          Old STreeView-based implementation is preserved in SStageEditorPanel.h/.cpp
 *
 * @note Migration Note (Phase 17.2):
 *       - Replaces STreeView with SSceneOutliner
 *       - Uses custom TreeItems (FStageEditorStageTreeItem, etc.)
 *       - Uses custom columns (Name, ID, State)
 *       - Integrates with existing Controller/Model layer
 *
 * @note Feature Parity with V1:
 *       - World Partition detection and warning
 *       - Registry warning and creation
 *       - Toolbar: Register Entities, Create BPs, Clean Orphaned
 *       - Settings window for asset creation paths
 *       - Viewport selection sync
 */
class SStageEditorPanelV2 : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SStageEditorPanelV2) {}
	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs, TSharedPtr<FStageEditorController> InController);

	virtual ~SStageEditorPanelV2();

	//----------------------------------------------------------------
	// Core API
	//----------------------------------------------------------------

	/** Refreshes the UI from the Controller's data */
	void RefreshUI();

	/** Gets the currently selected Stages */
	TArray<AStage*> GetSelectedStages() const;

	/** Gets the currently selected Entities */
	TArray<AActor*> GetSelectedEntities() const;

	/** Sets selection to a specific Stage */
	void SetSelection(AStage* Stage);

	/** Sets selection to a specific Entity */
	void SetSelection(AActor* Entity);

	/** Expands all items in the outliner */
	void ExpandAll();

	/** Collapses all items in the outliner */
	void CollapseAll();

private:
	//----------------------------------------------------------------
	// UI Construction
	//----------------------------------------------------------------

	/** Creates the main toolbar with action buttons */
	TSharedRef<SWidget> CreateMainToolbar();

	/** Creates the quick create toolbar (BP creation buttons) */
	TSharedRef<SWidget> CreateQuickCreateToolbar();

	/** Creates the Registry warning banner */
	TSharedRef<SWidget> CreateRegistryWarningBanner();

	/** Creates the Registry info banner */
	TSharedRef<SWidget> CreateRegistryInfoBanner();

	/** Creates the StageEditorOutliner widget */
	TSharedRef<SWidget> CreateOutliner();

	/** Rebuilds the entire UI (used when World Partition state changes) */
	void RebuildUI();

	//----------------------------------------------------------------
	// Toolbar Button Handlers
	//----------------------------------------------------------------

	/** Handler for "Refresh" button */
	FReply OnRefreshClicked();

	/** Handler for "Expand All" button */
	FReply OnExpandAllClicked();

	/** Handler for "Collapse All" button */
	FReply OnCollapseAllClicked();

	/** Handler for "Register Selected Entities" button */
	FReply OnRegisterSelectedEntitiesClicked();

	/** Handler for "Clean Orphaned Entities" button */
	FReply OnCleanOrphanedEntitiesClicked();

	/** Handler for "Create Stage BP" button */
	FReply OnCreateStageBPClicked();

	/** Handler for "Create Entity Actor BP" button */
	FReply OnCreateEntityActorBPClicked();

	/** Handler for "Create Entity Component BP" button */
	FReply OnCreateEntityComponentBPClicked();

	/** Handler for "Open Settings" button */
	FReply OnOpenSettingsClicked();

	/** Closes the settings window */
	void CloseSettingsWindow();

	//----------------------------------------------------------------
	// Registry Helpers
	//----------------------------------------------------------------

	/** Handler for "Create Registry" button */
	FReply OnCreateRegistryClicked();

	/** Handler for "Select Existing Registry" button */
	FReply OnSelectExistingRegistryClicked();

	/** Handler for "Convert to World Partition" button */
	FReply OnConvertToWorldPartitionClicked();

	/** Handler for "Refresh WP Status" button */
	FReply OnRefreshWorldPartitionStatusClicked();

	/** Checks if the current level is a World Partition level */
	bool IsWorldPartitionLevel() const;

	/** Checks if the current level has a StageRegistryAsset */
	bool HasRegistryAsset() const;

	/** Gets the cached Registry pointer (updates cache if stale) */
	UStageRegistryAsset* GetCachedRegistry() const;

	/** Returns visibility for the Registry warning banner */
	EVisibility GetRegistryWarningVisibility() const;

	/** Returns the current Registry asset path for display in UI */
	FText GetRegistryPathText() const;

	//----------------------------------------------------------------
	// Multi-User Status (Phase 4)
	//----------------------------------------------------------------

	/** Creates the Lock Status Bar widget */
	TSharedRef<SWidget> CreateLockStatusBar();

	/** Creates the Sync Warning Banner widget */
	TSharedRef<SWidget> CreateSyncWarningBanner();

	/** Returns visibility for the Lock Status Bar */
	EVisibility GetLockStatusBarVisibility() const;

	/** Returns the lock status text */
	FText GetLockStatusText() const;

	/** Returns visibility for the Sync Warning Banner */
	EVisibility GetSyncWarningVisibility() const;

	/** Returns the sync status text */
	FText GetSyncStatusText() const;

	/** Returns dynamic button text for Sync/Reconcile */
	FText GetSyncButtonText() const;

	/** Returns dynamic tooltip for Sync/Reconcile button */
	FText GetSyncButtonTooltip() const;

	/** Handler for "View Changelist" button */
	FReply OnViewChangelistClicked();

	/** Handler for "Refresh Lock Status" button */
	FReply OnRefreshLockStatusClicked();

	/** Handler for "Sync Registry" button */
	FReply OnSyncRegistryClicked();

	/** Refresh all cached status texts */
	void RefreshStatusCaches();

	//----------------------------------------------------------------
	// Recycle/Repair IDs (Phase 7)
	//----------------------------------------------------------------

	/** Handler for "Recycle IDs" button */
	FReply OnRecycleIDsClicked();

	/** Get visibility for Recycle IDs button */
	EVisibility GetRecycleIDsButtonVisibility() const;

	/** Get text for Recycle IDs button */
	FText GetRecycleIDsButtonText() const;

	/** Get tooltip for Recycle IDs button */
	FText GetRecycleIDsButtonTooltip() const;

	/** Handler for "Repair Duplicate IDs" button */
	FReply OnRepairDuplicateIDsClicked();

	/** Get visibility for Repair Duplicate IDs button */
	EVisibility GetRepairDuplicateIDsButtonVisibility() const;

	/** Get text for Repair Duplicate IDs button */
	FText GetRepairDuplicateIDsButtonText() const;

	/** Get tooltip for Repair Duplicate IDs button */
	FText GetRepairDuplicateIDsButtonTooltip() const;

	//----------------------------------------------------------------
	// Viewport Selection Sync
	//----------------------------------------------------------------

	/** Registers delegates to listen for viewport selection changes */
	void RegisterViewportSelectionListener();

	/** Removes viewport selection delegates */
	void UnregisterViewportSelectionListener();

	/** Handles selection events originating from the viewport/world outliner */
	void HandleViewportSelectionChanged(UObject* SelectedObject);

	//----------------------------------------------------------------
	// Event Handlers
	//----------------------------------------------------------------

	/** Called when a map is opened/changed */
	void OnMapOpened(const FString& Filename, bool bAsTemplate);

	/** Called when the Controller model changes */
	void OnModelChanged();

	/** Handles changes to asset creation settings */
	void OnAssetCreationSettingsChanged(const FPropertyChangedEvent& PropertyChangedEvent);

	/** Checks World Partition status and rebuilds UI if changed */
	void CheckAndRefreshWorldPartitionStatus();

	//----------------------------------------------------------------
	// Private State
	//----------------------------------------------------------------

	/** Reference to the Controller (MVC bridge) */
	TSharedPtr<FStageEditorController> Controller;

	/** The SceneOutliner-based tree view */
	TSharedPtr<SStageEditorOutliner> StageOutliner;

	/** Settings for asset creation, exposed to the details view */
	TSharedPtr<FStructOnScope> CreationSettings;

	/** Details view for asset creation settings */
	TSharedPtr<IStructureDetailsView> SettingsDetailsView;

	/** Weak reference to the settings popup window (to prevent multiple windows) */
	TWeakPtr<SWindow> SettingsWindow;

	/** Delegate handle for selection change events */
	FDelegateHandle ViewportSelectionDelegateHandle;

	/** Delegate handle for map changed events */
	FDelegateHandle MapChangedHandle;

	/** Cached World Partition state to detect changes */
	bool bCachedIsWorldPartition = false;

	/** Cached Registry pointer for optimized access */
	mutable TWeakObjectPtr<UStageRegistryAsset> CachedRegistry;
	mutable FString CachedRegistryLevelPath;

	/** Selection object we bound to (cached for removal) */
	TWeakObjectPtr<class USelection> ActorSelectionPtr;

	/** Guards to prevent recursive selection updates */
	bool bUpdatingTreeSelectionFromViewport = false;
	bool bUpdatingViewportSelectionFromPanel = false;

	//----------------------------------------------------------------
	// Multi-User Status Cache (Phase 4)
	//----------------------------------------------------------------

	/** Cached lock status bar visibility */
	mutable EVisibility CachedLockStatusBarVisibility = EVisibility::Collapsed;

	/** Cached lock status text */
	mutable FText CachedLockStatusText;

	/** Cached sync warning visibility */
	mutable EVisibility CachedSyncWarningVisibility = EVisibility::Collapsed;

	/** Cached sync status text */
	mutable FText CachedSyncStatusText;

	/** Cached deleted IDs count for Recycle button */
	mutable int32 CachedDeletedIDsCount = 0;

	/** Cached duplicate IDs count for Repair button */
	mutable int32 CachedDuplicateIDsCount = 0;
};
