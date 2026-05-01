// Copyright Stage Editor Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISceneOutlinerHierarchy.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class FStageDataLayerMode;
class UDataLayerInstance;
class UWorld;
struct FSceneOutlinerTreeItemID;
enum class EDataLayerAction : uint8;

/**
 * Hierarchy class that defines the tree structure for the Stage DataLayer Browser.
 *
 * This class is responsible for:
 * - Enumerating all DataLayers and creating tree items for them
 * - Establishing parent-child relationships between DataLayers
 * - Listening to DataLayer change events and updating the tree
 * - Filtering DataLayers based on visibility preferences
 *
 * The hierarchy presents a flat list of DataLayers (no Actor children) for simplicity,
 * focusing on DataLayer management rather than Actor membership viewing.
 *
 * Key design decisions:
 * - Only shows DataLayerInstance nodes (no Actor children)
 * - Supports parent DataLayer relationships for hierarchical display
 * - Event-driven refresh when DataLayers change
 * - Simple filtering: show all DataLayers regardless of type
 *
 * @see ISceneOutlinerHierarchy - Base interface from SceneOutliner module
 * @see FDataLayerHierarchy - Engine reference implementation (Private)
 */
class STAGEEDITOR_API FStageDataLayerHierarchy : public ISceneOutlinerHierarchy
{
public:
	virtual ~FStageDataLayerHierarchy();

	/**
	 * Factory method to create a new hierarchy instance.
	 * @param Mode The mode that owns this hierarchy
	 * @param World The world to display DataLayers from
	 * @return Unique pointer to the new hierarchy
	 */
	static TUniquePtr<FStageDataLayerHierarchy> Create(FStageDataLayerMode* Mode, const TWeakObjectPtr<UWorld>& World);

	//----------------------------------------------------------------
	// ISceneOutlinerHierarchy Interface
	//----------------------------------------------------------------

	/**
	 * Create all root-level tree items.
	 * This enumerates all DataLayers in the world and creates items for them.
	 */
	virtual void CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const override;

	/**
	 * Create children for a given tree item.
	 * Since we don't show Actor children, this returns nothing.
	 */
	virtual void CreateChildren(const FSceneOutlinerTreeItemPtr& Item, TArray<FSceneOutlinerTreeItemPtr>& OutChildren) const override;

	/**
	 * Find or create a parent item for a given tree item.
	 * Used to establish DataLayer parent hierarchy.
	 */
	virtual FSceneOutlinerTreeItemPtr FindOrCreateParentItem(
		const ISceneOutlinerTreeItem& Item,
		const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items,
		bool bCreate = false) override;

	//----------------------------------------------------------------
	// Configuration
	//----------------------------------------------------------------

	/** Set whether to highlight DataLayers containing selected actors */
	void SetHighlightSelectedDataLayers(bool bInHighlightSelectedDataLayers) { bHighlightSelectedDataLayers = bInHighlightSelectedDataLayers; }

	/** Get whether highlighting is enabled */
	bool GetHighlightSelectedDataLayers() const { return bHighlightSelectedDataLayers; }

private:
	/** Private constructor - use Create() factory method */
	FStageDataLayerHierarchy(FStageDataLayerMode* Mode, const TWeakObjectPtr<UWorld>& World);
	FStageDataLayerHierarchy(const FStageDataLayerHierarchy&) = delete;
	FStageDataLayerHierarchy& operator=(const FStageDataLayerHierarchy&) = delete;

	//----------------------------------------------------------------
	// Helpers
	//----------------------------------------------------------------

	/** Get the owning world for this hierarchy */
	UWorld* GetOwningWorld() const;

	/** Create a tree item for a DataLayer instance */
	FSceneOutlinerTreeItemPtr CreateDataLayerTreeItem(UDataLayerInstance* InDataLayer, bool bForce = false) const;

	/** Broadcast a full refresh event */
	void FullRefreshEvent();

	//----------------------------------------------------------------
	// Event Handlers
	//----------------------------------------------------------------

	/** Called when any DataLayer changes */
	void OnDataLayerChanged(
		const EDataLayerAction Action,
		const TWeakObjectPtr<const UDataLayerInstance>& ChangedDataLayer,
		const FName& ChangedProperty);

	/** Called when an Actor's DataLayer membership changes */
	void OnActorDataLayersChanged(const TWeakObjectPtr<AActor>& InActor);

	/** Called when the level actor list changes */
	void OnLevelActorListChanged();

	//----------------------------------------------------------------
	// Members
	//----------------------------------------------------------------

	/** The world we're representing */
	TWeakObjectPtr<UWorld> RepresentingWorld;

	/** Whether to highlight DataLayers containing selected actors */
	bool bHighlightSelectedDataLayers;
};
