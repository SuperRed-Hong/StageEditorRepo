// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataLayerSync/DataLayerSyncStatus.h"
#include "DataLayerSynchronizer.generated.h"

class UDataLayerAsset;
class UDataLayerInstance;
class AStage;
class AActor;
class UWorld;

/**
 * 单个 DataLayer 同步结果
 */
USTRUCT(BlueprintType)
struct STAGEEDITOR_API FDataLayerSyncResult
{
	GENERATED_BODY()

	/** 是否成功 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sync")
	bool bSuccess = false;

	/** 错误消息（如果失败） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sync")
	FText ErrorMessage;

	/** 新增的 Act 数量 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sync")
	int32 AddedActCount = 0;

	/** 移除的 Act 数量 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sync")
	int32 RemovedActCount = 0;

	/** 新增的 Entity 数量 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sync")
	int32 AddedEntityCount = 0;

	/** 移除的 Entity 数量 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sync")
	int32 RemovedEntityCount = 0;

	/** 是否有任何变化 */
	bool HasChanges() const
	{
		return AddedActCount > 0 || RemovedActCount > 0 ||
			   AddedEntityCount > 0 || RemovedEntityCount > 0;
	}
};

/**
 * 批量同步结果
 */
USTRUCT(BlueprintType)
struct STAGEEDITOR_API FDataLayerBatchSyncResult
{
	GENERATED_BODY()

	/** 同步的 DataLayer 数量 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sync")
	int32 SyncedCount = 0;

	/** 失败的 DataLayer 数量 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sync")
	int32 FailedCount = 0;

	/** 跳过的 DataLayer 数量（已同步或未导入） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sync")
	int32 SkippedCount = 0;

	/** 总 Act 变化 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sync")
	int32 TotalActChanges = 0;

	/** 总 Entity 变化 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sync")
	int32 TotalEntityChanges = 0;
};

/**
 * DataLayer 同步器
 *
 * 负责将 DataLayer 的变化同步到 Stage-Act-Entity 架构。
 *
 * 同步场景:
 * 1. Stage 级别: 新增/删除子 DataLayer → 创建/删除 Act
 * 2. Act 级别: 新增/删除 Actor → 注册/注销 Entity
 */
class STAGEEDITOR_API FDataLayerSynchronizer
{
public:
	/**
	 * 同步单个 DataLayer
	 *
	 * @param DataLayerAsset 要同步的 DataLayerAsset
	 * @param World 目标 World（可选，默认使用编辑器 World）
	 * @return 同步结果
	 */
	static FDataLayerSyncResult SyncDataLayer(UDataLayerAsset* DataLayerAsset, UWorld* World = nullptr);

	/**
	 * 同步所有 OutOfSync 的 DataLayer
	 *
	 * @param World 目标 World（可选，默认使用编辑器 World）
	 * @return 批量同步结果
	 */
	static FDataLayerBatchSyncResult SyncAllOutOfSync(UWorld* World = nullptr);

	/**
	 * 检查 DataLayer 是否可以同步
	 *
	 * @param DataLayerAsset 要检查的 DataLayerAsset
	 * @param OutErrorMessage 错误消息输出
	 * @return 是否可以同步
	 */
	static bool CanSync(UDataLayerAsset* DataLayerAsset, FText& OutErrorMessage);

private:
	/**
	 * 同步 Stage 级别变化（处理子 DataLayer 变化）
	 */
	static FDataLayerSyncResult SyncStageLevelChanges(
		AStage* Stage,
		const FDataLayerSyncStatusInfo& StatusInfo,
		UWorld* World);

	/**
	 * 同步 Act 级别变化（处理 Actor 成员变化）
	 */
	static FDataLayerSyncResult SyncActLevelChanges(
		AStage* Stage,
		int32 ActID,
		UDataLayerAsset* ActDataLayer,
		const FDataLayerSyncStatusInfo& StatusInfo,
		UWorld* World);

	/**
	 * 为新的子 DataLayer 创建 Act
	 */
	static bool CreateActFromDataLayer(
		AStage* Stage,
		UDataLayerAsset* ActDataLayerAsset,
		const FString& ActName,
		UWorld* World);

	/**
	 * 删除对应已移除 DataLayer 的 Act
	 */
	static bool RemoveActForDataLayer(
		AStage* Stage,
		UDataLayerAsset* RemovedDataLayerAsset);

	/**
	 * 获取 DataLayer 中的 Actor 列表
	 */
	static TArray<AActor*> GetActorsInDataLayer(UDataLayerAsset* Asset, UWorld* World);
};
