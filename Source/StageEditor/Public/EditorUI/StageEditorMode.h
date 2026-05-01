// Copyright Stage Editor Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISceneOutlinerMode.h"
#include "ISceneOutlinerTreeItem.h"
#include "SSceneOutliner.h"
#include "SceneOutlinerFwd.h"
#include "UObject/WeakObjectPtr.h"

class AActor;
class AStage;
class FMenuBuilder;
class FStageEditorController;
class FUICommandList;
class ISceneOutlinerHierarchy;
class SStageEditorOutliner;
class SWidget;
class UWorld;
struct FKeyEvent;
struct FPointerEvent;

/**
 * Parameters for constructing a FStageEditorMode.
 */
struct FStageEditorModeParams
{
	FStageEditorModeParams() {}

	FStageEditorModeParams(
		SSceneOutliner* InSceneOutliner,
		SStageEditorOutliner* InStageOutliner,
		const TWeakObjectPtr<UWorld>& InSpecifiedWorldToDisplay = nullptr,
		TWeakPtr<FStageEditorController> InController = nullptr);

	/** The world to display (if specified, won't auto-switch on world changes) */
	TWeakObjectPtr<UWorld> SpecifiedWorldToDisplay = nullptr;

	/** The StageEditor outliner widget that owns this mode */
	SStageEditorOutliner* StageOutliner = nullptr;

	/** The SceneOutliner widget */
	SSceneOutliner* SceneOutliner = nullptr;

	/** The controller for handling operations */
	TWeakPtr<FStageEditorController> Controller = nullptr;
};

/**
 * Mode class that defines the behavior of the StageEditor's SceneOutliner.
 *
 * This mode controls:
 * - What data appears in the tree (via Hierarchy)
 * - How items are sorted and filtered
 * - User interactions (selection, double-click, context menus)
 * - Keyboard shortcuts
 *
 * Key responsibilities:
 * - Create and manage the FStageEditorHierarchy
 * - Handle selection synchronization with Viewport
 * - Provide context menu for Stage/Act/Entity operations
 * - Support keyboard navigation and shortcuts
 * - Handle drag & drop operations
 *
 * @see ISceneOutlinerMode - Base interface from SceneOutliner module
 * @see FStageDataLayerMode - Reference implementation
 */
class STAGEEDITOR_API FStageEditorMode : public ISceneOutlinerMode
{
public:
	/** Sort priority enum for tree item types */
	enum class EItemSortOrder : int32
	{
		Stage = 0,
		ActsFolder = 1,
		EntitiesFolder = 2,
		Act = 3,
		Entity = 4,
		OrphanedFolder = 100,
		OrphanedEntity = 101,
	};

	/** Constructor */
	FStageEditorMode(const FStageEditorModeParams& Params);

	/** Destructor */
	virtual ~FStageEditorMode();

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
	virtual bool ShowFilterOptions() const override { return true; }

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
	// ISceneOutlinerMode Interface - Drag & Drop
	//----------------------------------------------------------------

	/** Whether the mode supports drag and drop */
	virtual bool CanSupportDragAndDrop() const override { return true; }

	/** Parse a drag drop operation */
	virtual bool ParseDragDrop(FSceneOutlinerDragDropPayload& OutPayload, const FDragDropOperation& Operation) const override;

	/** Validate a drop operation */
	virtual FSceneOutlinerDragValidationInfo ValidateDrop(const ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload) const override;

	/** Handle a drop operation */
	virtual void OnDrop(ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload, const FSceneOutlinerDragValidationInfo& ValidationInfo) const override;

	//----------------------------------------------------------------
	// Public API
	//----------------------------------------------------------------

	/** Get the outliner widget that owns this mode */
	SStageEditorOutliner* GetStageOutliner() const { return StageOutliner; }

	/** Get selected Stages from the outliner */
	TArray<AStage*> GetSelectedStages() const;

	/** Get selected Entity actors from the outliner */
	TArray<AActor*> GetSelectedEntities() const;

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

	/** Focus actor in viewport */
	void FocusActorInViewport(AActor* Actor);

	/** Select actor in viewport */
	void SelectActorInViewport(AActor* Actor, bool bAddToSelection = false);

	//----------------------------------------------------------------
	// Context Menu Builders
	//----------------------------------------------------------------

	void BuildStageContextMenu(FMenuBuilder& MenuBuilder, AStage* Stage);
	void BuildActContextMenu(FMenuBuilder& MenuBuilder, AStage* Stage, int32 ActIndex);
	void BuildEntityContextMenu(FMenuBuilder& MenuBuilder, AStage* Stage, AActor* Entity, int32 EntityID);

	//----------------------------------------------------------------
	// Drag & Drop Helpers
	//----------------------------------------------------------------

	/** Find the target Stage from a tree item (traverses up parent chain) */
	AStage* FindStageForItem(const ISceneOutlinerTreeItem& Item) const;

	/** Find the target Act ID from a tree item */
	int32 FindActIDForItem(const ISceneOutlinerTreeItem& Item) const;

	//----------------------------------------------------------------
	// Members
	//----------------------------------------------------------------

	/** The world we're representing */
	TWeakObjectPtr<UWorld> RepresentingWorld;

	/** The world that was manually specified (if any) */
	const TWeakObjectPtr<UWorld> SpecifiedWorldToDisplay;

	/** The outliner widget that owns this mode */
	SStageEditorOutliner* StageOutliner;

	/** The controller for handling operations */
	TWeakPtr<FStageEditorController> WeakController;

	/** Number of Stages that passed filters */
	uint32 FilteredStageCount = 0;

	/** Number of Entities that passed filters */
	uint32 FilteredEntityCount = 0;

	/** Guard to prevent recursive selection updates */
	bool bUpdatingSelection = false;
};
