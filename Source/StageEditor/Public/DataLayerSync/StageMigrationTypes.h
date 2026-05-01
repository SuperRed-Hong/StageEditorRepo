// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StageMigrationTypes.generated.h"

// Forward declarations
class AStage;

//================================================================
// Phase 13.5: Multi-User Registry Sync Types
//================================================================

/**
 * @brief Registry锁定状态枚举 (用于UI显示)
 * @see Phase13.5_MultiUser_RegistrySync_Design.md
 */
UENUM()
enum class ERegistryLockState : uint8
{
	/** Solo模式，不需要Source Control */
	NotApplicable,

	/** 未被任何人锁定 */
	Unlocked,

	/** 被当前用户锁定 (可编辑) */
	LockedByMe,

	/** 被其他用户锁定 (只读) */
	LockedByOther
};

/**
 * @brief Registry文件的Source Control锁定状态信息
 */
USTRUCT()
struct STAGEEDITOR_API FRegistryLockInfo
{
	GENERATED_BODY()

	/** 文件是否被CheckOut */
	UPROPERTY()
	bool bIsCheckedOut = false;

	/** 是否被当前用户CheckOut */
	UPROPERTY()
	bool bIsCheckedOutByMe = false;

	/** 是否被其他用户CheckOut */
	UPROPERTY()
	bool bIsCheckedOutByOther = false;

	/** 锁定者用户名 (如果被他人锁定) */
	UPROPERTY()
	FString OtherUserName;

	/** 锁定者Workspace (如果被他人锁定) */
	UPROPERTY()
	FString OtherWorkspace;

	/** 获取锁定状态枚举 */
	ERegistryLockState GetLockState() const
	{
		if (bIsCheckedOutByMe)
		{
			return ERegistryLockState::LockedByMe;
		}
		if (bIsCheckedOutByOther)
		{
			return ERegistryLockState::LockedByOther;
		}
		return ERegistryLockState::Unlocked;
	}

	/** 获取显示用的锁定状态文本 */
	FText GetDisplayText() const
	{
		switch (GetLockState())
		{
		case ERegistryLockState::LockedByMe:
			return FText::FromString(TEXT("Locked (being edited by you)"));
		case ERegistryLockState::LockedByOther:
			return FText::Format(
				NSLOCTEXT("StageEditor", "LockedByOther", "Locked by {0} (read-only)"),
				FText::FromString(OtherUserName));
		case ERegistryLockState::Unlocked:
		default:
			return FText::FromString(TEXT("Unlocked"));
		}
	}
};

/**
 * @brief Registry与Level的同步状态
 */
USTRUCT()
struct STAGEEDITOR_API FRegistrySyncStatus
{
	GENERATED_BODY()

	/** Level中有Actor但Registry中没有记录的Stages (ID=0) */
	UPROPERTY()
	TArray<TWeakObjectPtr<AStage>> PendingAssignment;

	/** Registry中有记录但Level中没有Actor的StageIDs */
	UPROPERTY()
	TArray<int32> PendingRemoval;

	/**
	 * Phase 13.9: Offline Editing Support
	 * Stages with temporary negative IDs (created offline) pending reconciliation
	 */
	UPROPERTY()
	TArray<TWeakObjectPtr<AStage>> PendingReconciliation;

	/** Registry文件是否需要从P4同步（本地版本过期） */
	UPROPERTY()
	bool bRegistryFileOutOfDate = false;

	/** Registry文件的P4状态描述（用于显示） */
	UPROPERTY()
	FString RegistryFileStatusMessage;

	/** 是否完全同步 */
	bool IsSynced() const
	{
		return PendingAssignment.Num() == 0
			&& PendingRemoval.Num() == 0
			&& PendingReconciliation.Num() == 0
			&& !bRegistryFileOutOfDate;
	}

	/** 获取待分配数量 */
	int32 GetPendingAssignmentCount() const { return PendingAssignment.Num(); }

	/** 获取待移除数量 */
	int32 GetPendingRemovalCount() const { return PendingRemoval.Num(); }

	/**
	 * Phase 13.9: Get number of Stages pending reconciliation
	 */
	int32 GetPendingReconciliationCount() const { return PendingReconciliation.Num(); }

	/** 获取显示用的同步状态文本 */
	FText GetDisplayText() const
	{
		if (IsSynced())
		{
			return FText::FromString(TEXT("Synced"));
		}

		FString Status;

		// Registry文件版本不同步 - 最优先显示
		if (bRegistryFileOutOfDate)
		{
			Status = TEXT("⚠ Registry file OUT OF DATE! ");
			if (!RegistryFileStatusMessage.IsEmpty())
			{
				Status += RegistryFileStatusMessage;
			}
			else
			{
				Status += TEXT("Need to sync from Source Control");
			}
			Status += TEXT("\n");
		}

		// Phase 13.9: Offline-created Stages - 优先级高于普通分配
		if (PendingReconciliation.Num() > 0)
		{
			Status += FString::Printf(TEXT("⚠️ %d Stage(s) with temporary IDs (offline-created)"), PendingReconciliation.Num());
			if (PendingAssignment.Num() > 0 || PendingRemoval.Num() > 0)
			{
				Status += TEXT("\n");
			}
		}

		// Stage数据不同步
		if (PendingAssignment.Num() > 0)
		{
			Status += FString::Printf(TEXT("%d Stage(s) pending ID assignment"), PendingAssignment.Num());
		}
		if (PendingRemoval.Num() > 0)
		{
			if (!Status.IsEmpty() && !Status.EndsWith(TEXT("\n"))) Status += TEXT(", ");
			Status += FString::Printf(TEXT("%d Stage(s) pending removal from Registry"), PendingRemoval.Num());
		}

		return FText::FromString(Status);
	}
};

/**
 * @brief Stage migration status classification.
 *
 * Used by FStageMigrationAnalyzer to categorize Stages during migration analysis.
 *
 * @see FStageMigrationAnalysis
 * @see FStageMigrationAnalyzer
 * @see Phase13_P0-3_MigrationPlan.md
 */
UENUM()
enum class EStageMigrationStatus : uint8
{
	/** Stage is correctly initialized, no action needed */
	Valid,

	/** Stage is uninitialized (StageID = 0), needs new ID allocation */
	Uninitialized,

	/** StageID conflict detected, needs reassignment */
	Conflict,

	/** Stage data is corrupted (rare, requires manual intervention) */
	Corrupted
};

/**
 * @brief Migration analysis result for a single Stage.
 *
 * Contains current state, proposed changes, and issue descriptions.
 */
USTRUCT()
struct STAGEEDITOR_API FStageMigrationAnalysis
{
	GENERATED_BODY()

	/** Stage Actor reference */
	UPROPERTY()
	TObjectPtr<AStage> Stage = nullptr;

	/** Current StageID */
	UPROPERTY()
	int32 CurrentStageID = 0;

	/** Proposed new StageID (after migration) */
	UPROPERTY()
	int32 NewStageID = 0;

	/** Migration status */
	UPROPERTY()
	EStageMigrationStatus Status = EStageMigrationStatus::Valid;

	/** Issue description (if any) */
	UPROPERTY()
	FString IssueDescription;

	/** Suggested action */
	UPROPERTY()
	FString SuggestedAction;

	/** Stage name (cached for display) */
	UPROPERTY()
	FString StageName;

	/** Default constructor */
	FStageMigrationAnalysis()
		: CurrentStageID(0)
		, NewStageID(0)
		, Status(EStageMigrationStatus::Valid)
	{
	}

	/** Check if Stage needs migration */
	bool NeedsMigration() const
	{
		return Status != EStageMigrationStatus::Valid;
	}

	/** Check if StageID will change */
	bool WillChangeID() const
	{
		return CurrentStageID != NewStageID;
	}

	/** Get display summary for UI */
	FString GetDisplaySummary() const
	{
		if (Status == EStageMigrationStatus::Valid)
		{
			return FString::Printf(TEXT("[%s] Valid (ID: %d)"), *StageName, CurrentStageID);
		}
		else if (WillChangeID())
		{
			return FString::Printf(TEXT("[%s] %s: %d → %d"),
				*StageName,
				*UEnum::GetValueAsString(Status),
				CurrentStageID,
				NewStageID);
		}
		else
		{
			return FString::Printf(TEXT("[%s] %s (ID: %d)"),
				*StageName,
				*UEnum::GetValueAsString(Status),
				CurrentStageID);
		}
	}
};

/**
 * @brief Overall migration analysis result for a Level.
 *
 * Contains analysis for all Stages and summary statistics.
 */
USTRUCT()
struct STAGEEDITOR_API FMigrationAnalysisResult
{
	GENERATED_BODY()

	/** Analysis results for all Stages */
	UPROPERTY()
	TArray<FStageMigrationAnalysis> StageAnalyses;

	/** Number of valid Stages */
	UPROPERTY()
	int32 ValidStageCount = 0;

	/** Number of uninitialized Stages */
	UPROPERTY()
	int32 UninitializedStageCount = 0;

	/** Number of conflicting Stages */
	UPROPERTY()
	int32 ConflictStageCount = 0;

	/** Number of corrupted Stages */
	UPROPERTY()
	int32 CorruptedStageCount = 0;

	/** Next available StageID (after migration) */
	UPROPERTY()
	int32 NextAvailableStageID = 1;

	/** Total number of Stages analyzed */
	int32 GetTotalStageCount() const
	{
		return StageAnalyses.Num();
	}

	/** Check if any issues were detected */
	bool HasIssues() const
	{
		return UninitializedStageCount > 0 || ConflictStageCount > 0 || CorruptedStageCount > 0;
	}

	/** Check if migration is needed */
	bool NeedsMigration() const
	{
		return HasIssues();
	}

	/** Get summary string for display */
	FString GetSummary() const
	{
		if (!HasIssues())
		{
			return FString::Printf(TEXT("All %d Stages are valid, no migration needed"), ValidStageCount);
		}

		return FString::Printf(
			TEXT("Total: %d, Valid: %d, Uninitialized: %d, Conflicts: %d, Corrupted: %d"),
			GetTotalStageCount(),
			ValidStageCount,
			UninitializedStageCount,
			ConflictStageCount,
			CorruptedStageCount);
	}

	/** Get detailed report for logging */
	FString GetDetailedReport() const
	{
		FString Report;
		Report += TEXT("=== Stage Migration Analysis Report ===\n");
		Report += FString::Printf(TEXT("Total Stages: %d\n"), GetTotalStageCount());
		Report += FString::Printf(TEXT("Valid: %d\n"), ValidStageCount);
		Report += FString::Printf(TEXT("Uninitialized: %d\n"), UninitializedStageCount);
		Report += FString::Printf(TEXT("Conflicts: %d\n"), ConflictStageCount);
		Report += FString::Printf(TEXT("Corrupted: %d\n"), CorruptedStageCount);
		Report += FString::Printf(TEXT("Next Available StageID: %d\n"), NextAvailableStageID);
		Report += TEXT("\nDetailed Analysis:\n");

		for (const FStageMigrationAnalysis& Analysis : StageAnalyses)
		{
			Report += FString::Printf(TEXT("  - %s\n"), *Analysis.GetDisplaySummary());
			if (Analysis.NeedsMigration())
			{
				Report += FString::Printf(TEXT("    Issue: %s\n"), *Analysis.IssueDescription);
				Report += FString::Printf(TEXT("    Action: %s\n"), *Analysis.SuggestedAction);
			}
		}

		Report += TEXT("======================================\n");
		return Report;
	}
};

/**
 * @brief Migration execution result.
 *
 * Returned by FStageMigrationExecutor after executing migration.
 */
USTRUCT()
struct STAGEEDITOR_API FMigrationExecutionResult
{
	GENERATED_BODY()

	/** Success flag */
	UPROPERTY()
	bool bSuccess = false;

	/** Error message (if failed) */
	UPROPERTY()
	FString ErrorMessage;

	/** Number of Stages migrated */
	UPROPERTY()
	int32 MigratedStageCount = 0;

	/** Number of StageIDs reassigned */
	UPROPERTY()
	int32 ReassignedIDCount = 0;

	/** Migration report (summary of changes) */
	UPROPERTY()
	FString MigrationReport;

	/** Check if migration succeeded */
	bool IsSuccess() const { return bSuccess; }

	/** Create success result */
	static FMigrationExecutionResult MakeSuccess(int32 MigratedCount, int32 ReassignedCount, const FString& Report)
	{
		FMigrationExecutionResult Result;
		Result.bSuccess = true;
		Result.MigratedStageCount = MigratedCount;
		Result.ReassignedIDCount = ReassignedCount;
		Result.MigrationReport = Report;
		return Result;
	}

	/** Create error result */
	static FMigrationExecutionResult MakeError(const FString& Error)
	{
		FMigrationExecutionResult Result;
		Result.bSuccess = false;
		Result.ErrorMessage = Error;
		return Result;
	}
};
