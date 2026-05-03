// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Data/StageRegistryTypes.h"
#include "DataLayerSync/StageMigrationTypes.h"
#include "ISourceControlProvider.h"
#include "StageEditorSubsystem.generated.h"

// Forward declarations
class UStageRegistryAsset;
class AStage;
class UWorld;
class ISourceControlChangelist;

/**
 * @brief Editor subsystem for managing Stage Registry assets.
 *
 * Purpose:
 * - Create and load StageRegistryAsset for each Level
 * - Provide Registry management API (wrapping UStageRegistryAsset CRUD)
 * - Handle Source Control integration (for Multi-user mode)
 * - Lifecycle management (cache loaded Registries)
 *
 * Architecture (Phase 13 - Simplified Version):
 * - This is a **lightweight wrapper** over UStageRegistryAsset
 * - Stage registration still happens via UStageManagerSubsystem (RuntimeSubsystem)
 * - This subsystem focuses on RegistryAsset lifecycle only
 * - Full dual-Subsystem architecture can be implemented later as optimization
 *
 * Key Design Decisions:
 * 1. **UEditorSubsystem** - Only exists in Editor, zero runtime overhead
 * 2. **Registry-per-Level** - Each Level has one StageRegistryAsset
 * 3. **Lazy Loading** - Registries loaded on-demand via GetOrLoadRegistryAsset()
 * 4. **Cache Management** - Loaded Registries cached in TMap for performance
 *
 * Usage Flow:
 * 1. StageEditorPanel calls GetOrLoadRegistryAsset() on Level load
 * 2. If Registry exists → load and cache
 * 3. If Registry missing → show warning, user creates via CreateRegistryAsset()
 * 4. UStageManagerSubsystem::RegisterStage() calls Registry->AllocateAndRegister()
 *
 * @see UStageRegistryAsset - Persistent registry data asset
 * @see UStageManagerSubsystem - Runtime Stage management
 * @see Phase13_StageRegistry_Discussion.md - Full design documentation
 */
UCLASS()
class STAGEEDITOR_API UStageEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	//----------------------------------------------------------------
	// Lifecycle
	//----------------------------------------------------------------

	/** Initialize the subsystem */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Deinitialize the subsystem */
	virtual void Deinitialize() override;

public:
	//----------------------------------------------------------------
	// Registry Asset Management
	//----------------------------------------------------------------

	/**
	 * Get or load the StageRegistryAsset for a given Level.
	 *
	 * Workflow:
	 * 1. Check cache (LoadedRegistries)
	 * 2. If not cached, search for .uasset in Level folder
	 * 3. If found, load and cache
	 * 4. If not found, return nullptr (user must create via CreateRegistryAsset)
	 *
	 * @param Level - Target Level (World)
	 * @return RegistryAsset if found/loaded, nullptr if not exists
	 */
	UStageRegistryAsset* GetOrLoadRegistryAsset(UWorld* Level);

	/**
	 * Create a new StageRegistryAsset for a Level.
	 *
	 * Workflow:
	 * 1. Check if Registry already exists (prevent duplicates)
	 * 2. Create new UStageRegistryAsset in Level folder
	 * 3. Initialize with Level and Collaboration Mode
	 * 4. Save asset to disk
	 * 5. Add to cache
	 * 6. (Multi-user mode) Check Out file via Source Control
	 *
	 * @param Level - Target Level
	 * @param Mode - Collaboration mode (Solo/Multi)
	 * @return Created RegistryAsset, or nullptr on failure
	 */
	UStageRegistryAsset* CreateRegistryAsset(UWorld* Level, ECollaborationMode Mode);

	/**
	 * Check if a Registry exists for a Level.
	 *
	 * @param Level - Target Level
	 * @return true if Registry exists (in cache or on disk)
	 */
	bool DoesRegistryExist(UWorld* Level) const;

	/**
	 * Clear the Registry cache for a specific Level.
	 * Useful when Registry is deleted externally.
	 *
	 * @param Level - Target Level
	 */
	void ClearRegistryCache(UWorld* Level);

	/**
	 * Get the default Registry asset path for a Level.
	 * Format: "/Game/Levels/<LevelName>_StageRegistry"
	 *
	 * @param Level - Target Level
	 * @return Asset path string
	 */
	FString GetRegistryAssetPath(UWorld* Level) const;

	/**
	 * Manually associate a Level with a specific Registry asset.
	 * Used when auto-detection fails (e.g., after plugin export to new project).
	 * The association is stored in editor config and persists across sessions.
	 *
	 * @param Level - Target Level
	 * @param RegistryPath - Full asset path to the Registry
	 */
	void SetManualRegistryAssociation(UWorld* Level, const FString& RegistryPath);

	/**
	 * Clear manual Registry association for a Level.
	 *
	 * @param Level - Target Level
	 */
	void ClearManualRegistryAssociation(UWorld* Level);

	/**
	 * Get manual Registry association for a Level (if any).
	 *
	 * @param Level - Target Level
	 * @return Registry path if manually set, empty string otherwise
	 */
	FString GetManualRegistryAssociation(UWorld* Level) const;

public:
	//----------------------------------------------------------------
	// Source Control Integration
	//----------------------------------------------------------------

	/**
	 * Check if Source Control is enabled and available.
	 *
	 * @return true if SC is enabled and provider is available
	 */
	bool IsSourceControlEnabled() const;

	/**
	 * Check Out a RegistryAsset file via Source Control.
	 * Only used in Multi-user mode.
	 *
	 * @param Registry - RegistryAsset to check out
	 * @return true if check out succeeded or SC not enabled, false on failure
	 */
	bool CheckOutRegistryFile(UStageRegistryAsset* Registry);

	//----------------------------------------------------------------
	// Phase 13.5: Multi-User Registry Sync
	//----------------------------------------------------------------

	/**
	 * Get the lock status information for a Registry file.
	 * WARNING: This performs a SYNCHRONOUS server query. Use GetCachedRegistryLockInfo() for UI.
	 *
	 * @param Registry - RegistryAsset to check
	 * @return FRegistryLockInfo with lock state and locker information
	 */
	FRegistryLockInfo GetRegistryLockInfo(UStageRegistryAsset* Registry);

	/**
	 * Get CACHED lock status information (Phase 18.1 Performance Optimization).
	 * Returns cached result if queried within LockInfoQueryThrottleInterval (5 seconds).
	 * Use this for UI display to avoid blocking the main thread.
	 *
	 * @param Registry - RegistryAsset to check
	 * @param bForceRefresh - If true, bypass cache and query server (default: false)
	 * @return FRegistryLockInfo with lock state and locker information
	 */
	FRegistryLockInfo GetCachedRegistryLockInfo(UStageRegistryAsset* Registry, bool bForceRefresh = false);

	/**
	 * Check Out Registry to a dedicated Changelist for review.
	 * Creates "Auto-Registry-Updates" changelist if not exists.
	 *
	 * @param Registry - RegistryAsset to check out
	 * @param OutErrorMessage - Error message if failed
	 * @return true if check out succeeded
	 */
	bool CheckOutToChangelist(UStageRegistryAsset* Registry, FString& OutErrorMessage);

	/**
	 * Save Registry to disk immediately.
	 *
	 * @param Registry - RegistryAsset to save
	 * @return true if save succeeded
	 */
	bool SaveRegistryToDisk(UStageRegistryAsset* Registry);

	/**
	 * Open the Source Control Changelist panel for user review.
	 */
	void OpenChangelistPanel();

	/**
	 * Get or create the dedicated Changelist for Registry updates.
	 *
	 * @return Changelist state pointer, or nullptr if failed
	 */
	FSourceControlChangelistStatePtr GetOrCreateRegistryChangelist();

	/** Name of the dedicated Changelist for Registry updates */
	static const FString RegistryChangelistName;

	/**
	 * Append Stage change info to the Changelist description.
	 * Records what Stages were added or removed.
	 *
	 * @param StageID - The SUID of the changed Stage
	 * @param StageName - Display name of the Stage
	 * @param bAdded - true if Stage was added/registered, false if removed
	 */
	void AppendStageChangeToChangelist(int32 StageID, const FString& StageName, bool bAdded);

	/**
	 * Append Registry creation summary to the Changelist description.
	 *
	 * @param MapName - Name of the map/level
	 * @param StageCount - Number of Stages registered
	 * @param Mode - Collaboration mode (Solo/Multi)
	 */
	void AppendRegistryCreationToChangelist(const FString& MapName, int32 StageCount, ECollaborationMode Mode);

	//----------------------------------------------------------------
	// Phase 18: Source Control Status Auto-Refresh
	//----------------------------------------------------------------

	/**
	 * Delegate broadcast when Source Control state changes.
	 * Used by StageEditorPanel to refresh status text caches (Lock Status, Sync Status).
	 *
	 * Why needed:
	 * - When user submits Registry via View Changelist, SC state changes but Panel doesn't know
	 * - Subsystem already listens to SC events (for MetadataCache refresh)
	 * - This delegate bridges SC events to Panel's UI refresh logic
	 *
	 * Connection flow:
	 * 1. ISourceControlProvider broadcasts FSourceControlStateChanged event
	 * 2. Subsystem::RegisterSourceControlHooks() catches event
	 * 3. Subsystem broadcasts OnSourceControlStateChanged delegate
	 * 4. Panel (subscribed in InitializePanel) calls StateManager->RefreshAllStatusTexts()
	 *
	 * @see RegisterSourceControlHooks() - SC event subscription
	 * @see SStageEditorPanel::InitializePanel() - Panel subscription
	 * @see FStageEditorStateManager::RefreshAllStatusTexts() - Status text refresh
	 */
	DECLARE_MULTICAST_DELEGATE(FOnSourceControlStateChanged);
	FOnSourceControlStateChanged OnSourceControlStateChanged;

public:
	//----------------------------------------------------------------
	// Stage Registration (从 RuntimeSubsystem 迁移)
	//----------------------------------------------------------------

	/**
	 * Register a Stage: allocate StageID, update RegistryAsset, add to runtime cache.
	 * This is the primary entry point for Stage registration in Editor.
	 *
	 * Workflow:
	 * 1. Get or load RegistryAsset for the Stage's Level
	 * 2. (Multi-user mode) Check Out Registry file
	 * 3. Call Registry->AllocateAndRegister(Stage) to allocate StageID
	 * 4. Update Stage->SUID.StageID
	 * 5. Add Stage to RuntimeSubsystem cache via AddStageToRuntimeCache()
	 * 6. Mark Registry dirty and append to Changelist
	 *
	 * @param Stage - The Stage actor to register
	 * @return Allocated StageID, or -1 on failure
	 */
	int32 RegisterStage(AStage* Stage);

	/**
	 * Unregister a Stage: remove from RegistryAsset and runtime cache.
	 *
	 * Workflow:
	 * 1. Get RegistryAsset for the Stage's Level
	 * 2. (Multi-user mode) Check Out Registry file
	 * 3. Call Registry->Unregister(StageID)
	 * 4. Remove from RuntimeSubsystem cache via RemoveStageFromRuntimeCache()
	 * 5. Mark Registry dirty and append to Changelist
	 *
	 * @param Stage - The Stage actor to unregister
	 * @return true if successful
	 */
	bool UnregisterStage(AStage* Stage);

	/**
	 * Find Stage by DataLayer using Registry (optimized for SyncStatus detection).
	 *
	 * Two-step lookup strategy:
	 * 1. Fast path: Check Registry for Stage-level DataLayers (low-frequency data)
	 * 2. Fallback: Query all loaded Stages for Act-level DataLayers (high-frequency data, not in Registry)
	 *
	 * Why Act DataLayers are NOT in Registry:
	 * - Acts are frequently created/deleted by users
	 * - Storing them in Registry would cause high Source Control conflict rate in multi-user mode
	 * - Registry should only contain low-frequency, stable data
	 *
	 * @param DataLayerAsset - DataLayer to search for (Stage-level or Act-level)
	 * @param World - Optional World context (defaults to editor world)
	 * @return Stage actor if found and loaded, nullptr otherwise
	 */
	AStage* FindStageByDataLayerInRegistry(class UDataLayerAsset* DataLayerAsset, UWorld* World = nullptr) const;

public:
	//----------------------------------------------------------------
	// Phase 13.8 Phase 3: MetadataCache Sync Mechanisms
	//----------------------------------------------------------------

	/**
	 * Manually refresh MetadataCache from Registry (Console command).
	 * Useful for debugging or when automatic sync fails.
	 *
	 * Usage: Type "RefreshStageMetadataCache" in UE Editor console
	 *
	 * @see RefreshMetadataCacheFromRegistry
	 */
	UFUNCTION(Exec, Category = "StageEditor")
	void RefreshStageMetadataCache();

public:
	//----------------------------------------------------------------
	// Debugging & Validation
	//----------------------------------------------------------------

	/**
	 * Validate all loaded Registries.
	 * Useful for debugging and integrity checks.
	 *
	 * @param OutErrors - Array to fill with error messages
	 * @return true if all Registries are valid
	 */
	bool ValidateAllRegistries(TArray<FString>& OutErrors) const;

	/**
	 * Dump all loaded Registries to log.
	 */
	void DumpAllRegistriesToLog() const;

private:
	//----------------------------------------------------------------
	// Internal Helpers
	//----------------------------------------------------------------

	/**
	 * Find existing Registry asset on disk.
	 *
	 * @param Level - Target Level
	 * @return Asset path if found, empty string otherwise
	 */
	FString FindExistingRegistryAsset(UWorld* Level) const;

	/**
	 * Load a Registry asset from path.
	 *
	 * @param AssetPath - Full asset path
	 * @return Loaded RegistryAsset, or nullptr on failure
	 */
	UStageRegistryAsset* LoadRegistryAsset(const FString& AssetPath);

	/**
	 * Save a Registry asset to disk.
	 *
	 * @param Registry - RegistryAsset to save
	 * @param AssetPath - Target asset path
	 * @return true if save succeeded
	 */
	bool SaveRegistryAsset(UStageRegistryAsset* Registry, const FString& AssetPath);

	/**
	 * Handle Stage loaded event from RuntimeSubsystem.
	 * Called when a Stage is loaded via WP streaming or PostLoad.
	 *
	 * @param Stage - The Stage that was loaded
	 */
	void HandleStageLoaded(AStage* Stage);

	/**
	 * Handle Stage unloaded event from RuntimeSubsystem.
	 * Called when a Stage is unloaded via WP streaming.
	 *
	 * @param Stage - The Stage that was unloaded
	 */
	void HandleStageUnloaded(AStage* Stage);

	/**
	 * Refresh metadata cache from Registry (Phase 13.8).
	 *
	 * Loads all Stage metadata from StageRegistryAsset into RuntimeSubsystem's MetadataCache.
	 * This eliminates temporal coupling between query logic and PostLoad events.
	 *
	 * @param World - The World whose Registry to load from
	 */
	void RefreshMetadataCacheFromRegistry(UWorld* World);

	//----------------------------------------------------------------
	// Phase 13.8 Phase 3: File Watcher & Source Control Hooks
	//----------------------------------------------------------------

	/**
	 * Register Registry file watcher (Strategy 1: File monitoring).
	 * Monitors the Registry file for external modifications.
	 */
	void RegisterRegistryFileWatcher();

	/**
	 * Unregister Registry file watcher.
	 */
	void UnregisterRegistryFileWatcher();

	/**
	 * Callback when Registry file is modified externally.
	 *
	 * @param FileChanges - List of file changes detected by DirectoryWatcher
	 */
	void OnRegistryFileChanged(const TArray<struct FFileChangeData>& FileChanges);

	/**
	 * Register Source Control hooks (Strategy 2: SC event monitoring).
	 * Listens for Sync/Update operations that might modify the Registry.
	 */
	void RegisterSourceControlHooks();

	/**
	 * Unregister Source Control hooks.
	 */
	void UnregisterSourceControlHooks();


private:
	//----------------------------------------------------------------
	// Cache
	//----------------------------------------------------------------

	/**
	 * Cache of loaded RegistryAssets.
	 * Key: Level soft object path (FSoftObjectPath)
	 * Value: Loaded RegistryAsset
	 */
	UPROPERTY(Transient)
	TMap<FSoftObjectPath, UStageRegistryAsset*> LoadedRegistries;

	/**
	 * Manual Registry associations (for plugin export scenarios).
	 * Key: Level package path (e.g., "/Game/Levels/MyLevel")
	 * Value: Registry asset path (e.g., "/Game/Levels/MyLevel_StageData/MyLevel_StageRegistry")
	 *
	 * Stored in EditorPerProjectUserSettings.ini for cross-session persistence.
	 */
	TMap<FString, FString> ManualRegistryAssociations;

	/** Whether ManualRegistryAssociations has been loaded from config */
	bool bManualAssociationsLoaded = false;

	/** Load manual associations from config file */
	void LoadManualAssociationsFromConfig();

	/** Save manual associations to config file */
	void SaveManualAssociationsToConfig();

	//----------------------------------------------------------------
	// Phase 13.8 Phase 3: Sync State
	//----------------------------------------------------------------

	/** File watcher delegate handle (Strategy 1) */
	FDelegateHandle RegistryFileWatcherHandle;

	/** Cached Registry file path for file monitoring */
	FString CachedRegistryFilePath;

	/** Source Control state changed delegate handle (Strategy 2) */
	FDelegateHandle SourceControlStateChangedHandle;

	/** Throttle: Last time RefreshMetadataCacheFromRegistry was called */
	double LastMetadataCacheRefreshTime = 0.0;

	/** Throttle: Minimum interval between refreshes (in seconds) */
	static constexpr double MetadataCacheRefreshThrottleInterval = 1.0;

	//----------------------------------------------------------------
	// Phase 18.1: Source Control Query Optimization
	//----------------------------------------------------------------

	/** Cached LockInfo to avoid repeated server queries */
	FRegistryLockInfo CachedLockInfo;

	/** Last time LockInfo was queried from server */
	double LastLockInfoQueryTime = 0.0;

	/** Minimum interval between LockInfo server queries (in seconds) */
	static constexpr double LockInfoQueryThrottleInterval = 5.0;

	/** Cached Changelist reference (avoid re-querying all changelists) */
	FSourceControlChangelistPtr CachedRegistryChangelist;

	/** Whether CachedRegistryChangelist has been validated */
	bool bChangelistCacheValid = false;
};
