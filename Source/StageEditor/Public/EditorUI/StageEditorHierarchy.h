// Copyright Stage Editor Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISceneOutlinerHierarchy.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AStage;
class AActor;
class FStageEditorMode;
class UWorld;
struct FSceneOutlinerTreeItemID;

/**
 * Hierarchy class that defines the tree structure for the StageEditor SceneOutliner.
 *
 * Tree Structure:
 * - Stage
 *   - Acts (Folder)
 *     - Act 0 (Default)
 *       - Entity 1 (State: X)
 *       - Entity 2 (State: Y)
 *     - Act 1
 *       - ...
 *   - Entities (Folder)
 *     - Entity 1
 *     - Entity 2
 *
 * This class is responsible for:
 * - Enumerating all Stages and creating tree items for them
 * - Creating child items (ActsFolder, EntitiesFolder, Acts, Entities)
 * - Establishing parent-child relationships
 * - Listening to Stage change events and updating the tree
 *
 * @see ISceneOutlinerHierarchy - Base interface from SceneOutliner module
 * @see FStageDataLayerHierarchy - Reference implementation for DataLayerBrowser
 */
class STAGEEDITOR_API FStageEditorHierarchy : public ISceneOutlinerHierarchy
{
public:
	virtual ~FStageEditorHierarchy();

	/**
	 * Factory method to create a new hierarchy instance.
	 * @param Mode The mode that owns this hierarchy
	 * @param World The world to display Stages from
	 * @return Unique pointer to the new hierarchy
	 */
	static TUniquePtr<FStageEditorHierarchy> Create(FStageEditorMode* Mode, const TWeakObjectPtr<UWorld>& World);

	//----------------------------------------------------------------
	// ISceneOutlinerHierarchy Interface
	//----------------------------------------------------------------

	/**
	 * Create all root-level tree items (Stages).
	 * Enumerates all AStage actors in the world.
	 */
	virtual void CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const override;

	/**
	 * Create children for a given tree item.
	 * - Stage -> ActsFolder + EntitiesFolder
	 * - ActsFolder -> All Acts
	 * - Act -> Entities with state overrides
	 * - EntitiesFolder -> All registered Entities (flat)
	 */
	virtual void CreateChildren(const FSceneOutlinerTreeItemPtr& Item, TArray<FSceneOutlinerTreeItemPtr>& OutChildren) const override;

	/**
	 * Find or create a parent item for a given tree item.
	 * Used for reconstructing tree from flat item list.
	 */
	virtual FSceneOutlinerTreeItemPtr FindOrCreateParentItem(
		const ISceneOutlinerTreeItem& Item,
		const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items,
		bool bCreate = false) override;

private:
	/** Private constructor - use Create() factory method */
	FStageEditorHierarchy(FStageEditorMode* Mode, const TWeakObjectPtr<UWorld>& World);
	FStageEditorHierarchy(const FStageEditorHierarchy&) = delete;
	FStageEditorHierarchy& operator=(const FStageEditorHierarchy&) = delete;

	//----------------------------------------------------------------
	// Helpers
	//----------------------------------------------------------------

	/** Get the owning world for this hierarchy */
	UWorld* GetOwningWorld() const;

	/** Find Stage by StageID */
	AStage* FindStageByID(int32 StageID) const;

	/** Broadcast a full refresh event */
	void FullRefreshEvent();

	//----------------------------------------------------------------
	// Event Handlers
	//----------------------------------------------------------------

	/** Called when a Stage is registered */
	void OnStageRegistered(AStage* Stage);

	/** Called when a Stage is unregistered */
	void OnStageUnregistered(AStage* Stage, int32 StageID);

	/** Called when Stage data changes (Acts, Entities) */
	void OnStageDataChanged(AStage* Stage);

	/** Called when the level actor list changes */
	void OnLevelActorListChanged();

	//----------------------------------------------------------------
	// Members
	//----------------------------------------------------------------

	/** The world we're representing */
	TWeakObjectPtr<UWorld> RepresentingWorld;

	/** Delegate handles for cleanup */
	FDelegateHandle StageRegisteredHandle;
	FDelegateHandle StageUnregisteredHandle;
	FDelegateHandle StageDataChangedHandle;
	FDelegateHandle LevelActorListChangedHandle;
};
