#pragma once

#pragma region Imports
#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/SHeaderRow.h"
#include "EditorLogic/StageEditorController.h"
#include "Actors/Stage.h"
#include "DataModels/StageTreeItem.h"
#include "StageEditorPanel.generated.h"
#pragma endregion Imports

#pragma region Column Names
namespace StageTreeColumns
{
	static const FName Watch("Watch");   // Eye icon for Debug HUD watch toggle (Stage rows only)
	static const FName ID("ID");
	static const FName Name("Name");
	static const FName Actions("Actions");
}
#pragma endregion Column Names

#pragma region Types

/**
 * Enum for configuration source selection
 */
UENUM()
enum class EStageEditorConfigSource : uint8
{
	/** Personal settings stored in Saved/Config (user-specific, not version controlled) */
	Personal,
	/** Project settings stored in Config/ (team-shared, read-only for most users) */
	Project
};

/**
 * Settings for asset creation paths
 */
USTRUCT()
struct FAssetCreationSettings
{
	GENERATED_BODY()

	/** If true, use custom path for Stage Blueprints. Otherwise, use default plugin path. */
	UPROPERTY(EditAnywhere, Category = "Asset Creation")
	bool bIsCustomStageAssetFolderPath = false;

	/** Custom folder path for Stage Blueprint creation */
	UPROPERTY(EditAnywhere, Category = "Asset Creation", meta = (EditCondition = "bIsCustomStageAssetFolderPath", ContentDir, RelativeToGameContentDir))
	FDirectoryPath StageAssetFolderPath;

	/** If true, use custom path for Entity Actor Blueprints. Otherwise, use default plugin path. */
	UPROPERTY(EditAnywhere, Category = "Asset Creation")
	bool bIsCustomEntityActorAssetPath = false;

	/** Custom folder path for Entity Actor Blueprint creation */
	UPROPERTY(EditAnywhere, Category = "Asset Creation", meta = (EditCondition = "bIsCustomEntityActorAssetPath", ContentDir, RelativeToGameContentDir))
	FDirectoryPath EntityActorAssetFolderPath;

	/** If true, use custom path for Entity Component Blueprints. Otherwise, use default plugin path. */
	UPROPERTY(EditAnywhere, Category = "Asset Creation")
	bool bIsCustomEntityComponentAssetPath = false;

	/** Custom folder path for Entity Component Blueprint creation */
	UPROPERTY(EditAnywhere, Category = "Asset Creation", meta = (EditCondition = "bIsCustomEntityComponentAssetPath", ContentDir, RelativeToGameContentDir))
	FDirectoryPath EntityComponentAssetFolderPath;

	/** If true, use custom path for DataLayer Assets. Otherwise, use default plugin path. */
	UPROPERTY(EditAnywhere, Category = "Asset Creation")
	bool bIsCustomDataLayerAssetPath = false;

	/** Custom folder path for DataLayer Asset creation */
	UPROPERTY(EditAnywhere, Category = "Asset Creation", meta = (EditCondition = "bIsCustomDataLayerAssetPath", ContentDir, RelativeToGameContentDir))
	FDirectoryPath DataLayerAssetFolderPath;

	/** Default parent class for creating new Stage Blueprints */
	UPROPERTY(EditAnywhere, Category = "Asset Creation",
		meta = (DisplayName = "Default Stage Blueprint Parent Class"))
	TSoftClassPtr<AStage> DefaultStageBlueprintParentClass;

	/** Default parent class for creating new Entity Actor Blueprints */
	UPROPERTY(EditAnywhere, Category = "Asset Creation",
		meta = (DisplayName = "Default Entity Actor Blueprint Parent Class"))
	TSoftClassPtr<AStageEntity> DefaultEntityActorBlueprintParentClass;

	/** Default parent class for creating new Entity Component Blueprints */
	UPROPERTY(EditAnywhere, Category = "Asset Creation",
		meta = (DisplayName = "Default Entity Component Blueprint Parent Class"))
	TSoftClassPtr<UStageEntityComponent> DefaultEntityComponentBlueprintParentClass;

	FAssetCreationSettings()
	{
		// Default paths to plugin Content folders (virtual paths)
		StageAssetFolderPath.Path = TEXT("/StageEditor/StagesBP");
		EntityActorAssetFolderPath.Path = TEXT("/StageEditor/EntitiesBP");
		EntityComponentAssetFolderPath.Path = TEXT("/StageEditor/EntitiesBP");
		DataLayerAssetFolderPath.Path = TEXT("/StageEditor/DataLayers");

		// Default parent class for Stage Blueprint creation
		DefaultStageBlueprintParentClass = TSoftClassPtr<AStage>(
			FSoftObjectPath(TEXT("/StageEditor/StagesBP/StageBaseBP/BP_BaseStage.BP_BaseStage_C")));

		// Default parent class for Entity Actor Blueprint creation
		DefaultEntityActorBlueprintParentClass = TSoftClassPtr<AStageEntity>(
			FSoftObjectPath(TEXT("/StageEditor/EntitiesBP/EntityBaseBP/BP_BaseStageEntityActor.BP_BaseStageEntityActor_C")));

		// Default parent class for Entity Component Blueprint creation
		DefaultEntityComponentBlueprintParentClass = TSoftClassPtr<UStageEntityComponent>(
			FSoftObjectPath(TEXT("/StageEditor/EntitiesBP/EntityBaseBP/BPC_BaseStageEntityComponent.BPC_BaseStageEntityComponent_C")));
	}
};

/**
 * Custom drag-drop operation for Entities
 */
class FEntityDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FEntityDragDropOp, FDragDropOperation)

	TArray<TSharedPtr<FStageTreeItem>> EntityItems;
	FText DefaultHoverText;
	FText CurrentHoverText;

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Menu.Background"))
			.Content()
			[
				SNew(STextBlock)
				.Text(CurrentHoverText)
			];
	}
};
#pragma endregion Types

// Forward declaration for friend classes
class SStageTreeRow;
class FStageEditorActionHandlers;
class FStageEditorDragDropHandler;

/**
 * @brief Main UI Panel for the Stage Editor
 * @details Displays the Stage hierarchy (Acts, Entities) and provides controls for managing them.
 */
class SStageEditorPanel : public SCompoundWidget
{
	// Allow helper classes to access private members
	friend class SStageTreeRow;
	friend class FStageEditorActionHandlers;
	friend class FStageEditorDragDropHandler;

public:
	SLATE_BEGIN_ARGS(SStageEditorPanel) {}
	SLATE_END_ARGS()

#pragma region Construction
	/**
	 * @brief Constructs the widget
	 * @param InArgs Slate arguments
	 * @param InController The logic controller for the editor
	 */
	void Construct(const FArguments& InArgs, TSharedPtr<FStageEditorController> InController);
	
	virtual ~SStageEditorPanel();
#pragma endregion Construction

#pragma region Core API
	/**
	 * @brief Refreshes the UI from the Controller's data.
	 * @details Rebuilds the tree view items based on the current state of the Stage and its Acts/Entities.
	 */
	void RefreshUI();
#pragma endregion Core API

#pragma region Drag & Drop Support
	//----------------------------------------------------------------
	// Drag & Drop Support
	//----------------------------------------------------------------
	
	/**
	 * @brief Called when a drag operation enters the panel.
	 */
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	
	/**
	 * @brief Called when a drag operation leaves the panel.
	 */
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	
	/**
	 * @brief Called when a drag operation moves over the panel.
	 */
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	
	/**
	 * @brief Called when a payload is dropped onto the panel.
	 */
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

	/** Settings for asset creation, exposed to the details view. */
	TSharedPtr<FStructOnScope> CreationSettings;

	/** Cached copy of settings when opening the settings window (for Cancel functionality). */
	FAssetCreationSettings CachedCreationSettings;

	/** Current configuration source (Personal or Project). */
	EStageEditorConfigSource CurrentConfigSource = EStageEditorConfigSource::Personal;
#pragma endregion Drag & Drop Support

#pragma region Callbacks
	//----------------------------------------------------------------
	// Callbacks
	//----------------------------------------------------------------
	
	/** Generates a row widget for the tree view. */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FStageTreeItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	
	/** Gets the children of a tree item. */
	void OnGetChildren(TSharedPtr<FStageTreeItem> Item, TArray<TSharedPtr<FStageTreeItem>>& OutChildren);
	
	/** Handles selection changes in the tree view. */
	void OnSelectionChanged(TSharedPtr<FStageTreeItem> Item, ESelectInfo::Type SelectInfo);
	
	/** Handler for "Create Act" button click. */
	FReply OnCreateActClicked();
	
	/** Handler for "Register Selected Entities" button click. */
	FReply OnRegisterSelectedEntitiesClicked();
	
	/** Handler for "Create Stage BP" button click. */
	FReply OnCreateStageBPClicked();

	/** Handler for "Create Entity Actor BP" button click. */
	FReply OnCreateEntityActorBPClicked();

	/** Handler for "Create Entity Component BP" button click. */
	FReply OnCreateEntityComponentBPClicked();
	
	/** Handler for "Refresh" button click. */
	FReply OnRefreshClicked();

	/** Handler for "Clean Orphaned Entities" button click. */
	FReply OnCleanOrphanedEntitiesClicked();

	/** Handler for "Convert to World Partition" button click. */
	FReply OnConvertToWorldPartitionClicked();

	/** Handler for "Refresh Status" button click (World Partition check). */
	FReply OnRefreshWorldPartitionStatusClicked();

	/** Handles dropping an item onto a row. */
	FReply OnRowDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent, TSharedPtr<FStageTreeItem> TargetItem);

	/** Handles double-clicking a row. */
	void OnRowDoubleClicked(TSharedPtr<FStageTreeItem> Item);

	/** Opens the context menu for a row. */
	TSharedPtr<SWidget> OnContextMenuOpening();
#pragma endregion Callbacks

#pragma region Private Helpers
private:
	/** Checks if the current level is a World Partition level. */
	bool IsWorldPartitionLevel() const;

	/** Checks if the current level has a StageRegistryAsset. */
	bool HasRegistryAsset() const;

	/** Gets the cached Registry pointer (updates cache if stale). Optimized for per-frame calls. */
	class UStageRegistryAsset* GetCachedRegistry() const;

	/** Returns visibility for the Registry warning banner. */
	EVisibility GetRegistryWarningVisibility() const;

	/** Returns the current Registry asset path for display in UI. */
	FText GetRegistryPathText() const;

	/** Handler for "Create Registry" button click. */
	FReply OnCreateRegistryClicked();

	/** Handler for "Select Existing Registry" button click. */
	FReply OnSelectExistingRegistryClicked();

	//----------------------------------------------------------------
	// Phase 13.5: Multi-User Registry Sync UI
	//----------------------------------------------------------------

	/** Returns visibility for the Registry lock status bar (Multi-user mode only). */
	EVisibility GetLockStatusBarVisibility() const;

	/** Returns visibility for the Sync Registry warning banner. */
	EVisibility GetSyncWarningVisibility() const;

	/** Returns the lock status display text (deprecated - reads cached value). */
	FText GetLockStatusText() const;

	/** Returns the sync status display text (deprecated - reads cached value). */
	FText GetSyncStatusText() const;

	/**
	 * Phase 13.9: Returns dynamic button text based on pending reconciliation status.
	 * Shows "Reconcile N Stage(s)" if there are offline-created Stages, otherwise "Sync Registry".
	 */
	FText GetSyncButtonText() const;

	/**
	 * Phase 13.9: Returns dynamic tooltip for the sync/reconcile button.
	 */
	FText GetSyncButtonTooltip() const;

	/** Handler for "Refresh Status" button click (Source Control status). */
	FReply OnRefreshStatusClicked();

	/** Handler for "Sync Registry" button click. */
	FReply OnSyncRegistryClicked();

	/** Handler for "View Changelist" button click. */
	FReply OnViewChangelistClicked();

	/** Phase 18: Handler for "Refresh Lock Status" button click (Manual refresh). */
	FReply OnRefreshLockStatusClicked();

	//----------------------------------------------------------------
	// Phase 13.10: Recycle IDs Button
	//----------------------------------------------------------------

	/** Handler for "Recycle IDs" button click. Manually recycles deleted StageIDs. */
	FReply OnRecycleIDsClicked();

	/** Get visibility for Recycle IDs button. */
	EVisibility GetRecycleIDsButtonVisibility() const;

	/** Get the button text showing deleted ID count. */
	FText GetRecycleIDsButtonText() const;

	/** Get tooltip for Recycle IDs button. */
	FText GetRecycleIDsButtonTooltip() const;

	//----------------------------------------------------------------
	// Phase 13.10: Repair Duplicate IDs Button
	//----------------------------------------------------------------

	/** Handler for "Repair Duplicate IDs" button click. Detects and repairs duplicate StageIDs. */
	FReply OnRepairDuplicateIDsClicked();

	/** Get visibility for Repair Duplicate IDs button. */
	EVisibility GetRepairDuplicateIDsButtonVisibility() const;

	/** Get the button text. */
	FText GetRepairDuplicateIDsButtonText() const;

	/** Get tooltip for Repair Duplicate IDs button. */
	FText GetRepairDuplicateIDsButtonTooltip() const;

	/**
	 * @brief Handles drag enter events on tree view rows
	 * @details Delegates to DragDropHandler for visual highlighting
	 * @param DragDropEvent The drag and drop event
	 * @param TargetItem The tree item being hovered
	 */
	void OnRowDragEnter(const FDragDropEvent& DragDropEvent, TSharedPtr<FStageTreeItem> TargetItem);

	/**
	 * @brief Checks if an item is the drag target or one of its descendants
	 * @param Item The item to check
	 * @param DragTarget The current drag target (Stage item)
	 * @return true if Item is DragTarget or a descendant of DragTarget
	 */
	bool IsItemOrDescendantOf(TSharedPtr<FStageTreeItem> Item, TSharedPtr<FStageTreeItem> DragTarget);

	/**
	 * @brief Handles drag leave events on tree view rows
	 * @details Delegates to DragDropHandler
	 * @param DragDropEvent The drag and drop event
	 * @param TargetItem The tree item being left
	 */
	void OnRowDragLeave(const FDragDropEvent& DragDropEvent, TSharedPtr<FStageTreeItem> TargetItem);

	/** Registers delegates to listen for viewport selection changes. */
	void RegisterViewportSelectionListener();

	/** Removes viewport selection delegates. */
	void UnregisterViewportSelectionListener();

	/** Handles selection events originating from the viewport/world outliner. */
	void HandleViewportSelectionChanged(UObject* SelectedObject);

	/** Expands all parent items leading to the specified tree item. */
	void ExpandAncestors(TSharedPtr<FStageTreeItem> Item);

	/** Finds the stage ancestor for a given item. */
	TSharedPtr<FStageTreeItem> FindStageAncestor(TSharedPtr<FStageTreeItem> Item) const;

	/** Shows a Yes/No confirmation dialog and returns true if user clicked Yes. */
	bool ShowConfirmDialog(const FText& Title, const FText& Message) const;

	/** Applies an Entity state change for items nested under Acts. */
	void ApplyEntityStateChange(TSharedPtr<FStageTreeItem> EntityItem, TSharedPtr<FStageTreeItem> ParentActItem, int32 NewState);

	/** Selects an actor inside the viewport/editor. */
	void SelectActorInViewport(AActor* ActorToSelect);

	/** Shows dialog for linking existing DataLayer to Act. */
	void ShowLinkDataLayerDialog(int32 ActID);

	/** Handles changes to asset creation settings. */
	void OnAssetCreationSettingsChanged(const FPropertyChangedEvent& PropertyChangedEvent);

	/** Loads asset creation settings from config file. */
	void LoadAssetCreationSettingsFromConfig();

	/** Saves asset creation settings to config file. */
	void SaveAssetCreationSettingsToConfig();

	/** Opens the settings window when gear button is clicked. */
	FReply OnOpenSettingsClicked();

	/** Closes the settings window. */
	void CloseSettingsWindow();

	/** Saves settings and closes the settings window. */
	FReply OnSaveSettingsClicked();

	/** Cancels changes and closes the settings window. */
	FReply OnCancelSettingsClicked();

	// ---- Config Source Management (Phase 19.3) ----

	/** Gets the file path for personal config (Saved/Config/). */
	FString GetPersonalConfigFilePath() const;

	/** Gets the file path for project config (Config/StageEditor/). */
	FString GetProjectConfigFilePath() const;

	/** Loads settings from JSON file. Returns true if successful. */
	bool LoadSettingsFromJson(const FString& FilePath);

	/** Saves settings to JSON file. Returns true if successful. */
	bool SaveSettingsToJson(const FString& FilePath);

	/** Called when config source radio button changes. */
	void OnConfigSourceChanged(EStageEditorConfigSource NewSource);

	/** Copies project settings to personal settings. */
	FReply OnCopyToPersonalClicked();

	/** Imports settings from a JSON file chosen by user. */
	FReply OnImportSettingsClicked();

	/** Exports current settings to a JSON file chosen by user. */
	FReply OnExportSettingsClicked();

	/** Resets settings to default values. */
	FReply OnResetToDefaultsClicked();

	/** Returns true if current config source allows editing. */
	bool IsConfigEditable() const;
#pragma endregion Private Helpers

#pragma region Private State
private:
	/** Reference to the Controller (MVC bridge) */
	TSharedPtr<FStageEditorController> Controller;

	/** Helper: State management and caching */
	TSharedPtr<class FStageEditorStateManager> StateManager;

	/** Helper: Tree building and row generation */
	TSharedPtr<class FStageEditorTreeBuilder> TreeBuilder;

	/** Helper: Button action handlers */
	TSharedPtr<class FStageEditorActionHandlers> ActionHandlers;

	/** Helper: Drag and drop operations */
	TSharedPtr<class FStageEditorDragDropHandler> DragDropHandler;

	/** The tree view widget displaying the Stage hierarchy */
	TSharedPtr<STreeView<TSharedPtr<FStageTreeItem>>> StageTreeView;

	/** Header row for the tree view (enables column resizing) */
	TSharedPtr<SHeaderRow> HeaderRow;

	/** Root items for the tree view (typically Stage items) */
	TArray<TSharedPtr<FStageTreeItem>> RootTreeItems;

	/** Details view for asset creation settings */
	TSharedPtr<class IStructureDetailsView> SettingsDetailsView;

	/** Weak reference to the settings popup window (to prevent multiple windows) */
	TWeakPtr<SWindow> SettingsWindow;

	/** Map of actor path to the corresponding tree item for quick selection sync. */
	TMap<FString, TWeakPtr<FStageTreeItem>> ActorPathToTreeItem;

	/** Delegate handle for selection change events. */
	FDelegateHandle ViewportSelectionDelegateHandle;

	/** Delegate handle for map changed events. */
	FDelegateHandle MapChangedHandle;

	/** Delegate handle for post-save world events. */
	FDelegateHandle PostSaveWorldHandle;

	/** Delegate handle for stage data changed events. */
	FDelegateHandle StageDataChangedHandle;

	/** Delegate handle for Source Control state changed events (Phase 18). */
	FDelegateHandle SourceControlStateChangedHandle;

	/** Delegate handle for PIE end events (Phase 21). */
	FDelegateHandle EndPIEHandle;

	/** Cached World Partition state to detect changes. */
	bool bCachedIsWorldPartition = false;

	// Cached state moved to StateManager helper

	/** Selection object we bound to (cached for removal). */
	TWeakObjectPtr<class USelection> ActorSelectionPtr;

	/** Called when a map is opened/changed. */
	void OnMapOpened(const FString& Filename, bool bAsTemplate);

	/** Called after the world is saved. */
	void OnPostSaveWorld(UWorld* World, FObjectPostSaveContext ObjectSaveContext);

	/** Called when Stage data changes (after Import/Sync operations). */
	void OnStageDataChanged(class AStage* Stage);

	/** Phase 18: Called when Source Control state changes (after Submit/Sync operations). */
	void OnSourceControlStateChanged();

	/** Phase 21: Called when PIE ends. Refreshes tree to clear stale weak pointers. */
	void OnEndPIE(bool bIsSimulating);

	/** Checks World Partition status and rebuilds UI if changed. */
	void CheckAndRefreshWorldPartitionStatus();

	/** Rebuilds the entire UI (used when World Partition state changes). */
	void RebuildUI();

	/** Guards to prevent recursive selection updates. */
	bool bUpdatingTreeSelectionFromViewport = false;
	bool bUpdatingViewportSelectionFromPanel = false;
#pragma endregion Private State
};
