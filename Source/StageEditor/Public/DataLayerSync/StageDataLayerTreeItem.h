// Copyright Stage Editor Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISceneOutlinerTreeItem.h"
#include "SceneOutlinerFwd.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "UObject/ObjectKey.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class ISceneOutliner;
class SWidget;
class UDataLayerInstance;
template <typename ItemType> class STableRow;

/**
 * Tree item representing a single DataLayer in the Stage DataLayer Browser.
 *
 * This is the node type for our custom SceneOutliner-based DataLayer browser.
 * Each item wraps a UDataLayerInstance and provides display/interaction logic.
 *
 * Key responsibilities:
 * - Display DataLayer name with sync status indicators
 * - Provide selection/interaction capabilities
 * - Support visibility toggling (inherited from base)
 * - Enable context menu generation
 *
 * Based on UE's FDataLayerTreeItem pattern but customized for StageEditor needs.
 *
 * @see ISceneOutlinerTreeItem - Base interface from SceneOutliner module
 * @see FDataLayerTreeItem - Engine reference implementation (Private)
 */
struct STAGEEDITOR_API FStageDataLayerTreeItem : ISceneOutlinerTreeItem
{
public:
	/** Constructor */
	FStageDataLayerTreeItem(UDataLayerInstance* InDataLayerInstance);

	/** Get the underlying DataLayer instance */
	UDataLayerInstance* GetDataLayer() const { return DataLayerInstance.Get(); }

	//----------------------------------------------------------------
	// ISceneOutlinerTreeItem Interface
	//----------------------------------------------------------------

	/** Returns true if the underlying DataLayer is still valid */
	virtual bool IsValid() const override { return DataLayerInstance.IsValid(); }

	/** Get unique ID for this item (used for tree item mapping) */
	virtual FSceneOutlinerTreeItemID GetID() const override { return ID; }

	/** Get display string for this item (used for sorting) */
	virtual FString GetDisplayString() const override;

	/** Returns true if user can interact with this item */
	virtual bool CanInteract() const override;

	/** Generate the label widget for this row */
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;

	/** Returns true if this item supports visibility info */
	virtual bool HasVisibilityInfo() const override { return true; }

	/** Handle visibility change */
	virtual void OnVisibilityChanged(const bool bNewVisibility) override;

	/** Get current visibility state */
	virtual bool GetVisibility() const override;

	/** Returns true if visibility state should be shown */
	virtual bool ShouldShowVisibilityState() const override;

	//----------------------------------------------------------------
	// Filter Predicates
	//----------------------------------------------------------------

	/** Predicate for filtering this item */
	DECLARE_DELEGATE_RetVal_OneParam(bool, FFilterPredicate, const UDataLayerInstance*);

	/** Predicate for determining interactive state */
	DECLARE_DELEGATE_RetVal_OneParam(bool, FInteractivePredicate, const UDataLayerInstance*);

	/** Apply filter predicate to this item's DataLayer */
	bool Filter(FFilterPredicate Pred) const
	{
		return Pred.Execute(GetDataLayer());
	}

	/** Get interactive state for this item's DataLayer */
	bool GetInteractiveState(FInteractivePredicate Pred) const
	{
		return Pred.Execute(GetDataLayer());
	}

	//----------------------------------------------------------------
	// Highlight Support
	//----------------------------------------------------------------

	/** Check if this item should be highlighted (e.g., when containing selected actors) */
	bool ShouldBeHighlighted() const;

	/** Set whether to highlight when selected */
	void SetIsHighlightedIfSelected(bool bInIsHighlightedIfSelected) { bIsHighlightedIfSelected = bInIsHighlightedIfSelected; }

	//----------------------------------------------------------------
	// Type Information
	//----------------------------------------------------------------

	/** Static type identifier for this tree item type */
	static const FSceneOutlinerTreeItemType Type;

private:
	/** Weak pointer to the DataLayer instance */
	TWeakObjectPtr<UDataLayerInstance> DataLayerInstance;

	/** Object key used as unique ID (stable across frames) */
	const FObjectKey ID;

	/** Flag for highlight behavior */
	bool bIsHighlightedIfSelected;
};
