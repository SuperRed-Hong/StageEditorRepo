// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "StageRegistryTypes.generated.h"

//----------------------------------------------------------------
// Collaboration Mode Enum
//----------------------------------------------------------------

/**
 * @brief Collaboration mode for StageRegistry management.
 *
 * This enum determines how Stage registration behaves in team environments:
 * - Solo: Single-user mode, no Source Control requirements
 * - Multi: Multi-user mode, requires Source Control for data safety
 */
UENUM(BlueprintType)
enum class ECollaborationMode : uint8
{
	/**
	 * Solo mode - Single developer workflow.
	 * - No Source Control checks required
	 * - Suitable for personal projects or prototyping
	 * - Warning: Not safe for multi-user collaboration
	 */
	Solo UMETA(DisplayName = "Solo (Single Developer)"),

	/**
	 * Multi mode - Team collaboration workflow.
	 * - Requires Source Control (Git/Perforce) enabled
	 * - Automatic Check Out before modifications
	 * - Prevents StageID conflicts in team environments
	 * - Warning: Once created, cannot switch to Solo mode
	 */
	Multi UMETA(DisplayName = "Multi (Team Collaboration)")
};

//----------------------------------------------------------------
// Stage Registry Entry
//----------------------------------------------------------------

/**
 * @brief Persistent registry entry for a single Stage.
 *
 * Key Design:
 * - Uses TSoftObjectPtr<UWorld> for Level reference (supports World Partition and Level move/rename)
 * - LevelInstanceID optional (only populated if Stage is in a Level Instance)
 * - StageID is globally unique (managed by UStageRegistryAsset)
 *
 * @see UStageRegistryAsset::RegisteredStages
 */
USTRUCT(BlueprintType)
struct STAGEEDITORRUNTIME_API FStageRegistryEntry
{
	GENERATED_BODY()

	/** Globally unique Stage identifier. Allocated by AllocateNextStageID(). */
	UPROPERTY(VisibleAnywhere, Category = "Stage Registry")
	int32 StageID = 0;

	/** Soft reference to the Level containing this Stage. Supports Level move/rename. */
	UPROPERTY(VisibleAnywhere, Category = "Stage Registry")
	TSoftObjectPtr<UWorld> OwnerLevel;

	/** Optional: Level Instance ID if this Stage is inside a Level Instance. */
	UPROPERTY(VisibleAnywhere, Category = "Stage Registry")
	FGuid LevelInstanceID;

	/** Display name of the Stage (cached for fast lookup, synced from AStage::StageName). */
	UPROPERTY(VisibleAnywhere, Category = "Stage Registry")
	FString StageName;

	/**
	 * Associated Stage-level DataLayer (DL_Stage_*).
	 * This is a low-frequency data (set once on Stage creation), safe for Registry storage.
	 * Used for optimized SyncStatus detection.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Stage Registry")
	TSoftObjectPtr<class UDataLayerAsset> StageDataLayerAsset;

	/** Timestamp when this Stage was registered (for debugging and audit). */
	UPROPERTY(VisibleAnywhere, Category = "Stage Registry")
	FDateTime RegistrationTime;

	/** Default constructor */
	FStageRegistryEntry()
		: StageID(0)
		, RegistrationTime(FDateTime::Now())
	{
	}

	/** Constructor with parameters */
	FStageRegistryEntry(int32 InStageID, const TSoftObjectPtr<UWorld>& InLevel, const FString& InStageName)
		: StageID(InStageID)
		, OwnerLevel(InLevel)
		, StageName(InStageName)
		, RegistrationTime(FDateTime::Now())
	{
	}

	/** Equality operator (based on StageID only) */
	bool operator==(const FStageRegistryEntry& Other) const
	{
		return StageID == Other.StageID;
	}

	/** Hash function for TMap/TSet support */
	friend uint32 GetTypeHash(const FStageRegistryEntry& Entry)
	{
		return GetTypeHash(Entry.StageID);
	}

	/** Check if this entry is valid (has a valid StageID and Level reference) */
	bool IsValid() const
	{
		return StageID > 0 && !OwnerLevel.IsNull();
	}
};

//----------------------------------------------------------------
// Stage Runtime ID (for Runtime Subsystem)
//----------------------------------------------------------------

/**
 * @brief Lightweight runtime identifier for Stage lookup.
 *
 * Used by UStageManagerSubsystem for runtime queries and caching.
 * Unlike FStageRegistryEntry, this does NOT contain persistent data.
 *
 * @see UStageManagerSubsystem::RuntimeStageMap
 */
USTRUCT(BlueprintType)
struct STAGEEDITORRUNTIME_API FStageRuntimeID
{
	GENERATED_BODY()

	/** Stage ID (matches FStageRegistryEntry::StageID) */
	UPROPERTY(BlueprintReadOnly, Category = "Stage Runtime")
	int32 StageID = 0;

	/** Optional: Level Instance ID if Stage is in a Level Instance */
	UPROPERTY(BlueprintReadOnly, Category = "Stage Runtime")
	FGuid LevelInstanceID;

	/** Default constructor */
	FStageRuntimeID() : StageID(0) {}

	/** Constructor with StageID */
	explicit FStageRuntimeID(int32 InStageID) : StageID(InStageID) {}

	/** Constructor with StageID and LevelInstanceID */
	FStageRuntimeID(int32 InStageID, const FGuid& InLevelInstanceID)
		: StageID(InStageID)
		, LevelInstanceID(InLevelInstanceID)
	{
	}

	/** Equality operator */
	bool operator==(const FStageRuntimeID& Other) const
	{
		return StageID == Other.StageID && LevelInstanceID == Other.LevelInstanceID;
	}

	/** Inequality operator */
	bool operator!=(const FStageRuntimeID& Other) const
	{
		return !(*this == Other);
	}

	/** Hash function for TMap support */
	friend uint32 GetTypeHash(const FStageRuntimeID& ID)
	{
		return HashCombine(GetTypeHash(ID.StageID), GetTypeHash(ID.LevelInstanceID));
	}

	/** Check if this ID is valid */
	bool IsValid() const
	{
		return StageID > 0;
	}

	/** Convert to string for logging */
	FString ToString() const
	{
		if (LevelInstanceID.IsValid())
		{
			return FString::Printf(TEXT("StageID=%d, LevelInstanceID=%s"),
				StageID, *LevelInstanceID.ToString());
		}
		return FString::Printf(TEXT("StageID=%d"), StageID);
	}
};
