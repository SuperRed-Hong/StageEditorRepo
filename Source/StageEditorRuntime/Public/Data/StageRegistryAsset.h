// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Data/StageRegistryTypes.h"
#include "StageRegistryAsset.generated.h"

// Forward declarations
class AStage;
class UWorld;

/**
 * @brief Persistent registry asset for Stage ID allocation and management.
 *
 * Purpose:
 * - **Persistent NextStageID** - Prevents StageID conflicts across editor sessions
 * - **Global Stage Registry** - Tracks all Stages in a Level (including unloaded ones in WP/LevelInstance)
 * - **Collaboration Support** - Solo vs Multi-user modes with Source Control integration
 *
 * Architecture:
 * - **Storage**: Saved as .uasset in Level folder (e.g., `Content/Levels/MyLevel_StageRegistry.uasset`)
 * - **Lifecycle**: Managed by UStageEditorSubsystem (Editor module)
 * - **Runtime Access**: Read-only via UStageManagerSubsystem (Runtime module)
 *
 * Key Design Decisions:
 * 1. **TSoftObjectPtr<UWorld>** - Supports Level move/rename without breaking references
 * 2. **Optional FLevelInstanceID** - Supports Level Instances (validated by P0-1)
 * 3. **ECollaborationMode** - Solo (no SC) vs Multi (requires SC)
 * 4. **No Mode Switching** - Once created, mode cannot be changed (simplifies design)
 *
 * Usage Flow:
 * 1. User creates Registry via StageEditorPanel → Choose Solo/Multi mode
 * 2. EditorSubsystem manages CRUD operations (RegisterStage, UnregisterStage)
 * 3. RuntimeSubsystem queries for Stage lookups (read-only)
 * 4. Registry saves NextStageID on every Stage registration (persistence)
 *
 * Transaction Safety (Phase 13 P0-2):
 * - All modifications wrapped in FScopedTransaction (Undo/Redo)
 * - PreCheck mechanism validates before modification
 * - Error propagation via FStageOperationStatus
 *
 * Migration Support (Phase 13 P0-3):
 * - Legacy Stages (StageID = 0 or conflicts) detected on first load
 * - Migration dialog shows analysis and auto-resolves conflicts
 * - First Stage keeps ID, subsequent conflicts get reassigned
 *
 * @see UStageEditorSubsystem - Editor-side management
 * @see UStageManagerSubsystem - Runtime-side queries
 * @see FStageRegistryEntry - Individual Stage entry
 * @see Phase13_StageRegistry_Discussion.md - Complete design documentation
 */
UCLASS(BlueprintType)
class STAGEEDITORRUNTIME_API UStageRegistryAsset : public UObject
{
	GENERATED_BODY()

public:
	//----------------------------------------------------------------
	// Persistent Data
	//----------------------------------------------------------------

	/**
	 * Next available StageID (auto-incremented on each RegisterStage call).
	 * CRITICAL: This must persist across editor sessions to prevent StageID conflicts.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Stage Registry")
	int32 NextStageID = 1;

	/**
	 * Pool of recycled StageIDs that are available for reuse.
	 * When allocating a new StageID, we first try to reuse IDs from this pool.
	 *
	 * IDs are added here when user clicks "Recycle IDs" button (detects ID gaps).
	 * IDs are removed (reused) when a new Stage is registered.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Stage Registry")
	TArray<int32> RecycledStageIDs;

	/**
	 * Registry of all Stages in this Level.
	 * Key: StageID
	 * Value: FStageRegistryEntry (contains OwnerLevel, LevelInstanceID, StageName, etc.)
	 */
	UPROPERTY(VisibleAnywhere, Category = "Stage Registry")
	TMap<int32, FStageRegistryEntry> RegisteredStages;

	/**
	 * Soft reference to the Level this Registry belongs to.
	 * Used for validation and integrity checks.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Stage Registry")
	TSoftObjectPtr<UWorld> OwnerLevel;

	/**
	 * Collaboration mode (Solo vs Multi-user).
	 * IMPORTANT: Cannot be changed after creation.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Stage Registry")
	ECollaborationMode CollaborationMode = ECollaborationMode::Solo;

	/**
	 * Creation timestamp (for debugging and audit).
	 */
	UPROPERTY(VisibleAnywhere, Category = "Stage Registry")
	FDateTime CreationTime;

public:
	//----------------------------------------------------------------
	// Lifecycle
	//----------------------------------------------------------------

	/** Constructor */
	UStageRegistryAsset();

#if WITH_EDITOR
	/**
	 * Called after this asset is duplicated in the editor.
	 * Resets all registry state to prevent ID conflicts and data corruption.
	 * StageRegistryAsset should NOT be manually duplicated — this is a safety net.
	 */
	virtual void PostEditImport() override;
#endif

	/** Initialize the Registry with Level and Collaboration Mode */
	void Initialize(const TSoftObjectPtr<UWorld>& InOwnerLevel, ECollaborationMode InMode);

	//----------------------------------------------------------------
	// Accessors
	//----------------------------------------------------------------

	/** Get the collaboration mode (Solo/Multi) */
	FORCEINLINE ECollaborationMode GetCollaborationMode() const { return CollaborationMode; }

	/** Get the next available StageID */
	FORCEINLINE int32 GetNextStageID() const { return NextStageID; }

	/** Get the number of registered Stages */
	FORCEINLINE int32 GetRegisteredStageCount() const { return RegisteredStages.Num(); }

	//----------------------------------------------------------------
	// Core API (Read-Only for Runtime, Write via EditorSubsystem)
	//----------------------------------------------------------------

	/**
	 * Allocate a new StageID and register a Stage.
	 *
	 * IMPORTANT:
	 * - This method is called ONLY by UStageEditorSubsystem::RegisterStage()
	 * - Do NOT call directly from user code
	 * - Increments NextStageID and marks package dirty
	 *
	 * @param Stage - Stage actor to register
	 * @return Allocated StageID (> 0 on success, 0 on failure)
	 */
	int32 AllocateAndRegister(AStage* Stage);

	/**
	 * Unregister a Stage by StageID.
	 *
	 * IMPORTANT:
	 * - This method is called ONLY by UStageEditorSubsystem::UnregisterStage()
	 * - Do NOT call directly from user code
	 * - Marks package dirty
	 *
	 * @param StageID - StageID to unregister
	 * @return true if Stage was found and unregistered, false otherwise
	 */
	bool Unregister(int32 StageID);

	/**
	 * Check if a StageID is already registered.
	 *
	 * @param StageID - StageID to check
	 * @return true if StageID exists in RegisteredStages
	 */
	bool IsStageIDRegistered(int32 StageID) const;

	/**
	 * Get a Stage entry by StageID (const version).
	 *
	 * @param StageID - StageID to query
	 * @return Pointer to FStageRegistryEntry if found, nullptr otherwise
	 */
	const FStageRegistryEntry* GetStageEntry(int32 StageID) const;

	/**
	 * Get a mutable Stage entry by StageID (for updating existing entries).
	 * IMPORTANT: Caller must call MarkPackageDirty() after modification.
	 *
	 * @param StageID - StageID to query
	 * @return Pointer to FStageRegistryEntry if found, nullptr otherwise
	 */
	FStageRegistryEntry* GetMutableStageEntry(int32 StageID);

	/**
	 * Get all registered Stage entries.
	 *
	 * @return Reference to RegisteredStages map
	 */
	const TMap<int32, FStageRegistryEntry>& GetAllStages() const { return RegisteredStages; }

	/**
	 * Get the total number of registered Stages.
	 *
	 * @return Count of RegisteredStages
	 */
	int32 GetStageCount() const { return RegisteredStages.Num(); }

	/**
	 * Find StageID by Stage-level DataLayer Asset (optimized lookup for SyncStatus detection).
	 *
	 * IMPORTANT: Only searches Stage-level DataLayers stored in Registry.
	 * Act-level DataLayers are NOT stored here (to avoid high-frequency Registry modifications
	 * and Source Control conflicts in multi-user mode).
	 *
	 * For Act-level DataLayers, use UStageEditorSubsystem::FindStageByDataLayerInRegistry()
	 * which falls back to runtime query of Stage->Acts.
	 *
	 * @param DataLayerAsset - Stage-level DataLayer to search for
	 * @return StageID if found (> 0), or 0 if not found
	 */
	int32 FindStageIDByDataLayer(const class UDataLayerAsset* DataLayerAsset) const;

	//----------------------------------------------------------------
	// ID Recycling (Manual - Detect ID Gaps)
	//----------------------------------------------------------------

	/**
	 * Get the number of recycled IDs available for reuse.
	 *
	 * @return Number of IDs in the recycle pool
	 */
	FORCEINLINE int32 GetRecycledIDCount() const { return RecycledStageIDs.Num(); }

	/**
	 * Get a copy of the recycled ID pool (for debugging/UI display).
	 *
	 * @return Array of recycled StageIDs
	 */
	FORCEINLINE const TArray<int32>& GetRecycledIDs() const { return RecycledStageIDs; }

	/**
	 * Detect ID gaps (unused IDs between 1 and NextStageID-1).
	 * These are IDs that are not in RegisteredStages and not already in RecycledStageIDs.
	 *
	 * @param OutGapIDs - Array to fill with detected gap IDs
	 * @return Number of gap IDs found
	 */
	int32 DetectIDGaps(TArray<int32>& OutGapIDs) const;

	/**
	 * Recycle detected ID gaps, making them available for reuse.
	 * Scans for IDs in range [1, NextStageID-1] that are not registered,
	 * and adds them to RecycledStageIDs.
	 *
	 * IMPORTANT:
	 * - This is a manual operation triggered by user clicking "Recycle IDs" button
	 * - In Multi mode, caller must CheckOut Registry first
	 * - Marks package dirty
	 *
	 * @return Number of IDs recycled (added to recycle pool)
	 */
	int32 RecycleIDGaps();

	//----------------------------------------------------------------
	// Duplicate ID Detection & Repair
	//----------------------------------------------------------------

	/**
	 * Detect duplicate StageIDs in the World (Stages with same ID).
	 * This can happen due to copy-paste, broken undo, or other edge cases.
	 *
	 * @param WorldContext - World to scan for Stage actors
	 * @param OutDuplicates - Map of StageID -> Array of Stage actors with that ID
	 * @return Number of duplicate groups found (0 = no duplicates)
	 */
	int32 DetectDuplicateStageIDs(UWorld* WorldContext, TMap<int32, TArray<AStage*>>& OutDuplicates) const;

	/**
	 * Repair duplicate StageIDs by assigning new unique IDs to duplicates.
	 * The first Stage found keeps its original ID, others get new IDs.
	 *
	 * IMPORTANT:
	 * - This modifies Stage actors (assigns new StageIDs to duplicates)
	 * - Should be wrapped in FScopedTransaction for Undo support
	 * - In Multi mode, caller must CheckOut Registry first
	 *
	 * @param WorldContext - World containing Stage actors
	 * @param OutRepairLog - Array of repair actions taken (for user feedback)
	 * @return Number of Stages repaired (assigned new IDs)
	 */
	int32 RepairDuplicateStageIDs(UWorld* WorldContext, TArray<FString>& OutRepairLog);

	//----------------------------------------------------------------
	// Validation & Integrity
	//----------------------------------------------------------------

	/**
	 * Validate Registry integrity.
	 * Checks for:
	 * - NextStageID >= max(RegisteredStages.Keys) + 1
	 * - No duplicate StageIDs
	 * - All Levels are valid soft references
	 *
	 * @param OutErrors - Array to fill with error messages (if any)
	 * @return true if Registry is valid, false otherwise
	 */
	bool ValidateIntegrity(TArray<FString>& OutErrors) const;

	/**
	 * Detect StageID conflicts.
	 * Returns list of Stages with duplicate StageIDs.
	 *
	 * @return Array of conflicting StageIDs
	 */
	TArray<int32> DetectConflicts() const;

	//----------------------------------------------------------------
	// Debugging
	//----------------------------------------------------------------

	/**
	 * Get a human-readable summary of the Registry.
	 * Format: "NextStageID=10, Registered=8, Mode=Solo"
	 *
	 * @return Summary string
	 */
	FString GetDebugSummary() const;

	/**
	 * Dump all registered Stages to log.
	 * Useful for debugging and auditing.
	 */
	void DumpToLog() const;

private:
	//----------------------------------------------------------------
	// Internal Helpers
	//----------------------------------------------------------------

	/**
	 * Create a FStageRegistryEntry from a Stage actor.
	 *
	 * @param Stage - Stage actor to create entry from
	 * @param StageID - Allocated StageID
	 * @return FStageRegistryEntry with populated fields
	 */
	FStageRegistryEntry CreateEntryFromStage(AStage* Stage, int32 StageID) const;
};
