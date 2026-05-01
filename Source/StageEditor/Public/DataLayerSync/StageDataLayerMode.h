// Copyright Stage Editor Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISceneOutlinerMode.h"
#include "ISceneOutlinerTreeItem.h"
#include "SSceneOutliner.h"
#include "SceneOutlinerFwd.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class FMenuBuilder;
class FUICommandList;
class ISceneOutlinerHierarchy;
class SStageDataLayerOutliner;
class SWidget;
class UDataLayerEditorSubsystem;
class UDataLayerInstance;
class UWorld;
struct FKeyEvent;
struct FPointerEvent;

/**
 * Parameters for constructing a FStageDataLayerMode.
 */
struct FStageDataLayerModeParams
{
	FStageDataLayerModeParams() {}

	FStageDataLayerModeParams(
		SSceneOutliner* InSceneOutliner,
		SStageDataLayerOutliner* InDataLayerOutliner,
		const TWeakObjectPtr<UWorld>& InSpecifiedWorldToDisplay = nullptr);

	/** The world to display (if specified, won't auto-switch on world changes) */
	TWeakObjectPtr<UWorld> SpecifiedWorldToDisplay = nullptr;

	/** The outliner widget that owns this mode */
	SStageDataLayerOutliner* DataLayerOutliner = nullptr;

	/** The SceneOutliner widget */
	SSceneOutliner* SceneOutliner = nullptr;
};

/** Delegate called when a DataLayer is selected in the browser */
DECLARE_DELEGATE_OneParam(FOnStageDataLayerSelected, UDataLayerInstance*);

/**
 * Mode class that defines the behavior of the Stage DataLayer Browser's SceneOutliner.
 *
 * This mode controls:
 * - What data appears in the tree (via Hierarchy)
 * - How items are sorted and filtered
 * - User interactions (selection, double-click, context menus)
 * - Keyboard shortcuts
 *
 * Key responsibilities:
 * - Create and manage the FStageDataLayerHierarchy
 * - Handle selection synchronization with StageEditor
 * - Provide context menu for DataLayer operations
 * - Support keyboard navigation and shortcuts
 *
 * This is a simplified version of FDataLayerMode focused on StageEditor's needs:
 * - No Actor display (DataLayers only)
 * - No drag & drop between DataLayers (handled elsewhere)
 * - Custom context menu with StageEditor actions
 *
 * @see ISceneOutlinerMode - Base interface from SceneOutliner module
 * @see FDataLayerMode - Engine reference implementation (Private)
 */
class STAGEEDITOR_API FStageDataLayerMode : public ISceneOutlinerMode
{
public:
	/** Sort priority enum for tree item types */
	enum class EItemSortOrder : int32
	{
		DataLayer = 0,
	};

	/** Constructor */
	FStageDataLayerMode(const FStageDataLayerModeParams& Params);

	/** Destructor */
	virtual ~FStageDataLayerMode();

	//----------------------------------------------------------------
	// ISceneOutlinerMode Interface - Core
	//----------------------------------------------------------------

	/** Rebuild mode data and refresh the tree */
	virtual void Rebuild() override;

	/** Get sort priority for an item type */
	virtual int32 GetTypeSortPriority(const ISceneOutlinerTreeItem& Item) const override;

	//----------------------------------------------------------------
	// ISceneOutlinerMode Interface - Capabilities
	//----------------------------------------------------------------

	/** Get selection mode (multi-select supported) */
	virtual ESelectionMode::Type GetSelectionMode() const override { return ESelectionMode::Multi; }

	/** Whether keyboard focus is supported */
	virtual bool SupportsKeyboardFocus() const override { return true; }

	/** Whether the mode is interactive */
	virtual bool IsInteractive() const override { return true; }

	/** Whether items can be renamed */
	virtual bool CanRename() const override { return true; }

	/** Whether a specific item can be renamed */
	virtual bool CanRenameItem(const ISceneOutlinerTreeItem& Item) const override;

	/** Whether to show status bar */
	virtual bool ShowStatusBar() const override { return true; }

	/** Whether to show view button */
	virtual bool ShowViewButton() const override { return false; }

	/** Whether to show filter options */
	virtual bool ShowFilterOptions() const override { return false; }

	//----------------------------------------------------------------
	// ISceneOutlinerMode Interface - Status
	//----------------------------------------------------------------

	/** Get status bar text */
	virtual FText GetStatusText() const override;

	/** Get status text color */
	virtual FSlateColor GetStatusTextColor() const override { return FSlateColor::UseForeground(); }

	//----------------------------------------------------------------
	// ISceneOutlinerMode Interface - Events
	//----------------------------------------------------------------

	/** Called when an item is added to the tree */
	virtual void OnItemAdded(FSceneOutlinerTreeItemPtr Item) override;

	/** Called when an item is removed from the tree */
	virtual void OnItemRemoved(FSceneOutlinerTreeItemPtr Item) override;

	/** Called when item passes filters but isn't yet in tree */
	virtual void OnItemPassesFilters(const ISceneOutlinerTreeItem& Item) override;

	/** Called when item selection changes */
	virtual void OnItemSelectionChanged(
		FSceneOutlinerTreeItemPtr TreeItem,
		ESelectInfo::Type SelectionType,
		const FSceneOutlinerItemSelection& Selection) override;

	/** Called when an item is double-clicked */
	virtual void OnItemDoubleClick(FSceneOutlinerTreeItemPtr Item) override;

	/** Called when a key is pressed */
	virtual FReply OnKeyDown(const FKeyEvent& InKeyEvent) override;

	/** Synchronize mode-specific selection with tree view */
	virtual void SynchronizeSelection() override;

	//----------------------------------------------------------------
	// ISceneOutlinerMode Interface - Context Menu
	//----------------------------------------------------------------

	/** Create context menu for current selection */
	virtual TSharedPtr<SWidget> CreateContextMenu() override;

	//----------------------------------------------------------------
	// Public API
	//----------------------------------------------------------------

	/** Get the outliner widget that owns this mode */
	SStageDataLayerOutliner* GetDataLayerOutliner() const { return DataLayerOutliner; }

	/** Get selected DataLayers from the outliner */
	TArray<UDataLayerInstance*> GetSelectedDataLayers() const;

protected:
	/** Create the hierarchy for this mode */
	virtual TUniquePtr<ISceneOutlinerHierarchy> CreateHierarchy() override;

private:
	//----------------------------------------------------------------
	// Helpers
	//----------------------------------------------------------------

	/** Register context menu */
	void RegisterContextMenu();

	/** Choose the world to represent */
	void ChooseRepresentingWorld();

	/** Handle level selection changes */
	void OnLevelSelectionChanged(UObject* Obj);

	/** Get the owning world */
	UWorld* GetOwningWorld() const;

	/** Get DataLayer from tree item */
	UDataLayerInstance* GetDataLayerFromTreeItem(const ISceneOutlinerTreeItem& TreeItem) const;

	/** Cache selected items for restoration */
	void CacheSelectedItems(const FSceneOutlinerItemSelection& Selection);

	/** Refresh selection state */
	void RefreshSelection();

	/** Find selected DataLayers in Content Browser */
	void FindInContentBrowser();

	//----------------------------------------------------------------
	// Members
	//----------------------------------------------------------------

	/** The world we're representing */
	TWeakObjectPtr<UWorld> RepresentingWorld;

	/** The world that was manually specified (if any) */
	const TWeakObjectPtr<UWorld> SpecifiedWorldToDisplay;

	/** The outliner widget that owns this mode */
	SStageDataLayerOutliner* DataLayerOutliner;

	/** Reference to DataLayerEditorSubsystem */
	UDataLayerEditorSubsystem* DataLayerEditorSubsystem;

	/** Number of DataLayers that passed filters */
	uint32 FilteredDataLayerCount = 0;

	/** Set of DataLayers that passed initial filters */
	TSet<TWeakObjectPtr<UDataLayerInstance>> ApplicableDataLayers;

	/** Cached selected DataLayers for restoration */
	TSet<TWeakObjectPtr<const UDataLayerInstance>> SelectedDataLayersSet;

	/** Whether highlighting is enabled */
	bool bHighlightSelectedDataLayers = false;
};
