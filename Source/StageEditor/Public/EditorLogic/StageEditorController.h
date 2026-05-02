#pragma once

#include "CoreMinimal.h"
#include "Core/StageCoreTypes.h"
#include "DataLayer/DataLayerAction.h"
#include "DataLayerSync/StageMigrationTypes.h"

class AStage;
class AActor;
class UDataLayerInstance;
class UStageRegistryAsset;

/**
 * @brief Logic controller for the Stage Editor.
 * Handles transactions and data modification for AStage.
 */
class STAGEEDITOR_API FStageEditorController : public TSharedFromThis<FStageEditorController>
{
public:
	FStageEditorController();
	~FStageEditorController();

	//----------------------------------------------------------------
	// Core API
	//----------------------------------------------------------------

	/** Sets the Stage currently being edited/active. */
	void SetActiveStage(AStage* NewStage);

	/** Returns the currently active Stage (for operations). */
	AStage* GetActiveStage() const;

	/** Returns all stages found in the level. */
	const TArray<TWeakObjectPtr<AStage>>& GetFoundStages() const { return FoundStages; }

	/** Creates a new Act in the Active Stage. Returns the new ActID, or -1 on failure. */
	int32 CreateNewAct();

	/** Registers a list of Actors as Entities to the Active Stage (or specific TargetStage). */
	bool RegisterEntities(const TArray<AActor*>& ActorsToRegister, AStage* TargetStage = nullptr);

	/** Quick create blueprint assets with custom default paths - Returns created Blueprint or nullptr if cancelled */
	UBlueprint* CreateStageBlueprint(const FString& DefaultPath = TEXT("/Game/Stages"), UClass* DefaultParentClass = nullptr, const FString& DefaultName = TEXT("BP_Stage_"));
	void CreateEntityActorBlueprint(const FString& DefaultPath, UClass* DefaultParentClass = nullptr);
	void CreateEntityComponentBlueprint(const FString& DefaultPath, UClass* DefaultParentClass = nullptr);

	/**
	 * @brief Initializes the controller, scanning the world for existing Stages.
	 */
	void Initialize();

	/**
	 * @brief Scans the current editor world for ALL AStage actors.
	 * Populates FoundStages.
	 */
	void FindStageInWorld();

	/**
	 * @brief Previews the specified Act by applying its Entity States.
	 * @param ActID The ID of the Act to preview.
	 */
	void PreviewAct(int32 ActID);

	//----------------------------------------------------------------
	// Entity Management
	//----------------------------------------------------------------

	/** Sets the state of a Entity within a specific Act. */
	bool SetEntityStateInAct(int32 EntityID, int32 ActID, int32 NewState);

	/** Removes a Entity from a specific Act (keeps it registered to Stage). */
	bool RemoveEntityFromAct(int32 EntityID, int32 ActID);

	/** Removes all Entities from a specific Act. */
	bool RemoveAllEntitiesFromAct(int32 ActID);

	/** Unregisters a Entity from the Stage entirely (removes from all Acts and EntityRegistry). */
	bool UnregisterAllEntities(int32 EntityID);

	/** Unregisters all Entities from the Stage. */
	bool UnregisterAllEntities();

	//----------------------------------------------------------------
	// Act Management
	//----------------------------------------------------------------

	/** Deletes an Act from the Stage (cannot delete Default Act). */
	bool DeleteAct(int32 ActID);

	//----------------------------------------------------------------
	// Data Layer Integration
	//----------------------------------------------------------------

	/** Sets the folder path where DataLayer assets will be created. */
	void SetDataLayerAssetFolderPath(const FString& Path) { DataLayerAssetFolderPath = Path; }

	/** Creates a DataLayerAsset and saves it to Content folder. */
	class UDataLayerAsset* CreateDataLayerAsset(const FString& AssetName, const FString& FolderPath = TEXT("/StageEditor/DataLayers"));

	/** Prompts user for a new DataLayer name when a conflict is detected. Returns empty string if cancelled. */
	FString PromptForNewDataLayerName(const FString& CurrentName);

	/** Gets or creates a DataLayerInstance for the given Asset. */
	class UDataLayerInstance* GetOrCreateInstanceForAsset(class UDataLayerAsset* Asset);

	/** Creates the root Data Layer for a Stage (Asset + Instance). */
	bool CreateDataLayerForStage(AStage* Stage);

	/** Creates a new Data Layer for an Act (Asset + Instance), as child of Stage's DataLayer. */
	bool CreateDataLayerForAct(int32 ActID);

	/** Deletes the Data Layer associated with an Act. */
	bool DeleteDataLayerForAct(int32 ActID);

	/** Assigns an existing Data Layer to the specified Act. */
	bool AssignDataLayerToAct(int32 ActID, class UDataLayerAsset* DataLayer);

	/** Syncs a Entity to the Act's Data Layer (adds the actor to the data layer). */
	bool SyncEntityToDataLayer(int32 EntityID, int32 ActID);

	/** Assigns a Entity to the Stage's root Data Layer. */
	bool AssignEntityToStageDataLayer(int32 EntityID);

	/** Removes a Entity from the Stage's root Data Layer. */
	bool RemoveEntityFromStageDataLayer(int32 EntityID);

	/** Assigns a Entity to an Act's Data Layer. */
	bool AssignEntityToActDataLayer(int32 EntityID, int32 ActID);

	/** Removes a Entity from an Act's Data Layer. */
	bool RemoveEntityFromActDataLayer(int32 EntityID, int32 ActID);

	/** Finds the DataLayerInstance for a Stage's root DataLayer. */
	class UDataLayerInstance* FindStageDataLayerInstance(AStage* Stage);

	//----------------------------------------------------------------
	// DataLayer Import Integration
	//----------------------------------------------------------------

	/**
	 * @brief RAII helper to suppress automatic DataLayer creation during import.
	 * When a Stage is spawned during import, OnLevelActorAdded should NOT auto-create
	 * DataLayers since the importer will assign existing DataLayers.
	 */
	struct STAGEEDITOR_API FScopedImportBypass
	{
		FScopedImportBypass() { ++FStageEditorController::ImportBypassCounter; }
		~FScopedImportBypass() { --FStageEditorController::ImportBypassCounter; }
	};

	/** Returns true if currently in import bypass mode (DataLayer auto-creation suppressed). */
	static bool IsImportBypassActive() { return ImportBypassCounter > 0; }

	/**
	 * @brief Imports a Stage from an existing DataLayer hierarchy.
	 * Creates a new Stage actor and assigns the existing DataLayer as its root.
	 * Child DataLayers are imported as Acts.
	 *
	 * @param StageDataLayerAsset The root Stage DataLayer asset (must follow DL_Stage_* naming)
	 * @param World Optional world, uses editor world if null
	 * @return The created Stage actor, or nullptr on failure
	 */
	AStage* ImportStageFromDataLayer(class UDataLayerAsset* StageDataLayerAsset, UWorld* World = nullptr);

	/**
	 * @brief Imports a Stage from an existing DataLayer hierarchy with DefaultAct selection.
	 *
	 * @param StageDataLayerAsset The root Stage DataLayer asset (must follow DL_Stage_* naming)
	 * @param SelectedDefaultActIndex Index of child DataLayer to use as DefaultAct (0-based among child DataLayers).
	 *        -1 = use empty DefaultAct (no associated DataLayer)
	 *        0+ = the selected child becomes DefaultAct (ID=1), others start from ID=2
	 * @param StageBlueprintClass Stage Blueprint class to use for instantiation (required)
	 * @param World Optional world, uses editor world if null
	 * @return The created Stage actor, or nullptr on failure
	 */
	AStage* ImportStageFromDataLayerWithDefaultAct(class UDataLayerAsset* StageDataLayerAsset, int32 SelectedDefaultActIndex, TSubclassOf<AStage> StageBlueprintClass, UWorld* World = nullptr);

	/**
	 * @brief Checks if a DataLayer can be imported as a Stage.
	 * @param DataLayerAsset The DataLayer asset to check
	 * @param OutReason If returns false, contains the reason why
	 * @return true if can import, false otherwise
	 */
	bool CanImportDataLayer(class UDataLayerAsset* DataLayerAsset, FText& OutReason);

	//----------------------------------------------------------------
	// DataLayer Rename (Unified Entry Point)
	//----------------------------------------------------------------

	/**
	 * @brief Renames a Stage's root DataLayer asset.
	 * Also updates AStage::StageDataLayerName to keep in sync.
	 * Broadcasts OnDataLayerRenamed delegate.
	 *
	 * @param Stage The Stage whose DataLayer to rename
	 * @param NewAssetName The new asset name (should follow DL_Stage_* convention)
	 * @return true on success
	 */
	bool RenameStageDataLayer(AStage* Stage, const FString& NewAssetName);

	/**
	 * @brief Renames an Act's DataLayer asset.
	 * Broadcasts OnDataLayerRenamed delegate.
	 *
	 * @param Stage The Stage containing the Act
	 * @param ActID The Act whose DataLayer to rename
	 * @param NewAssetName The new asset name (should follow DL_Act_* convention)
	 * @return true on success
	 */
	bool RenameActDataLayer(AStage* Stage, int32 ActID, const FString& NewAssetName);

	/**
	 * @brief Finds the Stage that owns a given DataLayer asset.
	 * @param DataLayerAsset The DataLayer to search for
	 * @return The owning Stage, or nullptr if not found or not imported
	 */
	AStage* FindStageByDataLayer(class UDataLayerAsset* DataLayerAsset);

	/**
	 * @brief Finds the ActID for a given DataLayer asset within a Stage.
	 * @param Stage The Stage to search in
	 * @param DataLayerAsset The DataLayer to find
	 * @return The ActID, or -1 if not found
	 */
	int32 FindActIDByDataLayer(AStage* Stage, class UDataLayerAsset* DataLayerAsset);

	//----------------------------------------------------------------
	// Orphaned Entity Management
	//----------------------------------------------------------------

	/**
	 * @brief Scans the scene for orphaned Entities and clears their state.
	 * Orphaned Entities are those with EntityID but no valid OwnerStage (Stage was deleted).
	 *
	 * @param World The world to scan, defaults to current editor world
	 * @return Number of Entities cleaned
	 */
	int32 CleanOrphanedEntities(UWorld* World = nullptr);

	/**
	 * @brief Checks if an Entity is already registered to a different Stage.
	 *
	 * @param Actor The Actor to check
	 * @param CurrentStage The Stage attempting to register this Entity
	 * @param OutOwnerStage If already registered, outputs the owning Stage
	 * @return true if registered to a different Stage
	 */
	bool IsEntityRegisteredToOtherStage(AActor* Actor, AStage* CurrentStage, AStage*& OutOwnerStage);

	//----------------------------------------------------------------
	// Stage Deletion
	//----------------------------------------------------------------

	/**
	 * @brief Deletes a Stage with user confirmation dialog.
	 * Shows dialog asking whether to delete associated DataLayers.
	 *
	 * @param Stage The Stage to delete
	 * @return true if deleted (user didn't cancel), false if cancelled
	 */
	bool DeleteStageWithConfirmation(AStage* Stage);

	/**
	 * @brief Deletes a Stage and optionally its associated DataLayers.
	 *
	 * @param Stage The Stage to delete
	 * @param bDeleteDataLayers If true, also deletes all associated DataLayers
	 * @return true on success
	 */
	bool DeleteStage(AStage* Stage, bool bDeleteDataLayers);

	//----------------------------------------------------------------
	// World Partition Support
	//----------------------------------------------------------------

	/** Checks if the current world has World Partition enabled. */
	bool IsWorldPartitionActive() const;

	/** Converts the current world to World Partition. */
	void ConvertToWorldPartition();

	//----------------------------------------------------------------
	// Phase 13.5: Multi-User Registry Sync
	//----------------------------------------------------------------

	/**
	 * @brief Calculates the sync status between Level Stages and Registry.
	 * WARNING: This may perform a SYNCHRONOUS server query in Multi-user mode.
	 * Use GetCachedSyncStatus() for UI display.
	 *
	 * @param World The world to scan
	 * @param Registry The Registry to compare against
	 * @return Sync status with pending assignment/removal lists
	 */
	FRegistrySyncStatus CalculateSyncStatus(UWorld* World, UStageRegistryAsset* Registry);

	/**
	 * @brief Get CACHED sync status (Phase 18.1 Performance Optimization).
	 * Returns cached result if queried within SyncStatusCacheInterval (5 seconds).
	 * Use this for UI display to avoid blocking the main thread.
	 *
	 * @param World The world to scan
	 * @param Registry The Registry to compare against
	 * @param bForceRefresh If true, bypass cache and recalculate (default: false)
	 * @return Sync status with pending assignment/removal lists
	 */
	FRegistrySyncStatus GetCachedSyncStatus(UWorld* World, UStageRegistryAsset* Registry, bool bForceRefresh = false);

	/**
	 * @brief Synchronizes pending Stages and Registry entries.
	 * - Assigns IDs to Stages with ID=0
	 * - Removes Registry entries for deleted Stages
	 * Requires Registry to be checked out (Multi-user mode).
	 *
	 * @return true if sync succeeded, false if Registry is locked
	 */
	bool SyncRegistry();

	/**
	 * Phase 13.9: Offline Editing Support
	 * @brief Reconciles Stages with temporary negative IDs to real positive IDs.
	 *
	 * When Stages are created offline (SC unavailable), they get temporary negative IDs.
	 * This method:
	 * 1. Finds all Stages with StageID < 0 (bPendingRegistration = true)
	 * 2. CheckOut Registry
	 * 3. Allocates real positive IDs via Registry
	 * 4. Remaps all references (Act.EntityStateOverrides, etc.)
	 * 5. Saves Registry and opens Changelist panel
	 *
	 * @return true if reconciliation succeeded, false if Registry is locked or no pending Stages
	 */
	bool ReconcilePendingStages();

	//----------------------------------------------------------------
	// Delegates
	//----------------------------------------------------------------

	/** Broadcasts when the Stage data changes (to update UI). */
	DECLARE_MULTICAST_DELEGATE(FOnStageModelChanged);
	FOnStageModelChanged OnModelChanged;

	/** Broadcasts when a DataLayer is renamed (Asset, NewName). */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDataLayerRenamed, class UDataLayerAsset*, const FString&);
	FOnDataLayerRenamed OnDataLayerRenamed;

private:
	UBlueprint* CreateBlueprintAsset(UClass* ParentClass, const FString& BaseName, const FString& DefaultPath);

	/**
	 * Phase 13.9: Allocate temporary negative ID for offline-created Stages.
	 * Returns -1, -2, -3, ... sequentially.
	 */
	int32 AllocateTemporaryID();

	/** List of all Stages found in the level. */
	TArray<TWeakObjectPtr<AStage>> FoundStages;

	/** The Stage currently selected or active for operations. */
	TWeakObjectPtr<AStage> ActiveStage;

	/** Path where DataLayer assets are created. */
	FString DataLayerAssetFolderPath = TEXT("/StageEditor/DataLayers");

	/** Helper to get the StageManagerSubsystem. */
	class UStageManagerSubsystem* GetSubsystem() const;

	/** Static counter for import bypass mode. When > 0, auto DataLayer creation is suppressed. */
	static int32 ImportBypassCounter;

	//----------------------------------------------------------------
	// Editor Delegates
	//----------------------------------------------------------------

	void BindEditorDelegates();
	void UnbindEditorDelegates();

	void OnLevelActorAdded(AActor* InActor);
	void OnLevelActorDeleted(AActor* InActor);

	/**
	 * @brief Handler for external DataLayer changes (from native UE DataLayerOutliner).
	 * Detects renames and syncs Stage data accordingly.
	 */
	void OnExternalDataLayerChanged(
		const EDataLayerAction Action,
		const TWeakObjectPtr<const UDataLayerInstance>& ChangedDataLayer,
		const FName& ChangedProperty);

	/** Handle for DataLayerChanged delegate subscription. */
	FDelegateHandle DataLayerChangedHandle;

	//----------------------------------------------------------------
	// Phase 18.1: SyncStatus Cache (Performance Optimization)
	//----------------------------------------------------------------

	/** Cached SyncStatus to avoid repeated server queries */
	FRegistrySyncStatus CachedSyncStatus;

	/** Last time SyncStatus was calculated */
	double LastSyncStatusCalcTime = 0.0;

	/** Minimum interval between SyncStatus server queries (in seconds) */
	static constexpr double SyncStatusCacheInterval = 5.0;
};
