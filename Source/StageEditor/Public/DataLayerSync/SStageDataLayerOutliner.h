// Copyright Stage Editor Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneOutlinerFwd.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class FStageDataLayerMode;
class SSceneOutliner;
class UDataLayerInstance;
class UWorld;
enum class EDataLayerAction : uint8;

/**
 * Stage DataLayer Outliner - Custom SceneOutliner-based DataLayer browser for StageEditor.
 *
 * This widget provides a DataLayer tree view based on the SceneOutliner framework with:
 * - Toolbar with Sync All and Import Selected buttons
 * - Custom columns for sync status, SUID, and actions
 * - Event subscription for sync status tracking
 *
 * Key workflow:
 * 1. User renames DataLayers to follow DL_Stage_* or DL_Act_<stage>_* naming
 * 2. User selects a DataLayer and clicks "Import Selected"
 * 3. Stage/Act structures are created based on DataLayer hierarchy
 * 4. Changes are tracked and "Sync All" button shows pending updates
 *
 * Architecture:
 * - Uses FStageDataLayerMode for outliner behavior
 * - Uses FStageDataLayerHierarchy for data structure
 * - Uses FStageDataLayerTreeItem for individual nodes
 *
 * @see FStageDataLayerMode - Outliner mode implementation
 * @see FStageDataLayerHierarchy - Data hierarchy
 * @see FStageDataLayerTreeItem - Tree node type
 */
class STAGEEDITOR_API SStageDataLayerOutliner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SStageDataLayerOutliner) {}
	SLATE_END_ARGS()

	/** Construct this widget */
	void Construct(const FArguments& InArgs);

	/** Destructor - cleanup event subscriptions */
	virtual ~SStageDataLayerOutliner();

	//----------------------------------------------------------------
	// Public API
	//----------------------------------------------------------------

	/** Refresh status display (native browser refreshes itself via events) */
	void RefreshTree();

	/** Sync browser selection to a specific DataLayer */
	void SyncToDataLayer(const UDataLayerInstance* DataLayer);

	/** Get currently selected DataLayers (cached from events) */
	TArray<UDataLayerInstance*> GetSelectedDataLayers() const;

	/** Called when selection changes in the outliner (called by FStageDataLayerMode) */
	void OnSelectionChanged(TSet<TWeakObjectPtr<const UDataLayerInstance>>& InSelectedDataLayersSet);

private:
	//----------------------------------------------------------------
	// Initialization
	//----------------------------------------------------------------

	/** Initialize the DataLayer Outliner (reserved for future custom outliner) */
	void InitializeOutliner();

	/** Subscribe to DataLayer change events */
	void SubscribeToEvents();

	/** Unsubscribe from DataLayer change events */
	void UnsubscribeFromEvents();

	/** Handle map/world change events */
	void OnMapChanged(uint32 MapChangeFlags);

	//----------------------------------------------------------------
	// Event Handlers
	//----------------------------------------------------------------

	/** Called when any DataLayer changes (add/modify/delete/rename) */
	void OnDataLayerChanged(
		const EDataLayerAction Action,
		const TWeakObjectPtr<const UDataLayerInstance>& ChangedDataLayer,
		const FName& ChangedProperty);

	/** Called when Actor's DataLayer membership changes */
	void OnActorDataLayersChanged(const TWeakObjectPtr<AActor>& ChangedActor);

	//----------------------------------------------------------------
	// Toolbar Actions
	//----------------------------------------------------------------

	/** Refresh button callback - only refreshes cache and UI, no sync */
	FReply OnRefreshClicked();

	/** Sync All button callback */
	FReply OnSyncAllClicked();

	/** Check if Sync All button should be enabled */
	bool CanSyncAll() const;

	/** Import Selected button callback */
	FReply OnImportSelectedClicked();

	/** Check if Import Selected button should be enabled */
	bool CanImportSelected() const;

	/** Get all selected DataLayerAssets that can be imported (Stage DataLayers only) */
	TArray<class UDataLayerAsset*> GetSelectedImportableDataLayerAssets() const;

	//----------------------------------------------------------------
	// UI Construction
	//----------------------------------------------------------------

	/** Rebuild the entire UI (called on init and world change) */
	void RebuildUI();

	/** Build the toolbar with action buttons */
	TSharedRef<SWidget> BuildToolbar();

	//----------------------------------------------------------------
	// Members
	//----------------------------------------------------------------

	/** The SceneOutliner widget */
	TSharedPtr<SSceneOutliner> SceneOutliner;

	/** The mode controlling the SceneOutliner behavior */
	FStageDataLayerMode* DataLayerMode = nullptr;

	/** The world being displayed */
	TWeakObjectPtr<UWorld> RepresentingWorld;

	/** Currently selected DataLayers (cached from selection events) */
	TSet<TWeakObjectPtr<const UDataLayerInstance>> SelectedDataLayersSet;

	/** Delegate handles for event subscriptions */
	FDelegateHandle OnDataLayerChangedHandle;
	FDelegateHandle OnActorDataLayersChangedHandle;
	FDelegateHandle OnMapChangedHandle;
};
