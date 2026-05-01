// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataLayerSyncStatus.generated.h"

class UDataLayerAsset;
class UDataLayerInstance;

/**
 * DataLayer 同步状态枚举
 */
UENUM(BlueprintType)
enum class EDataLayerSyncStatus : uint8
{
	/** 未导入到 StageEditor（蓝色图标） */
	NotImported UMETA(DisplayName = "Not Imported"),

	/** 已同步，无变化（绿色图标） */
	Synced UMETA(DisplayName = "Synced"),

	/** 已导入但有变化，需要同步（橙色图标） */
	OutOfSync UMETA(DisplayName = "Out of Sync")
};

/**
 * 同步状态详细信息
 *
 * 包含状态检测的详细结果，用于生成提示信息和决定同步操作。
 */
USTRUCT(BlueprintType)
struct STAGEEDITOR_API FDataLayerSyncStatusInfo
{
	GENERATED_BODY()

	/** 同步状态 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SyncStatus")
	EDataLayerSyncStatus Status = EDataLayerSyncStatus::NotImported;

	//----------------------------------------------------------------
	// 子 DataLayer 变化（仅 Stage 级别）
	//----------------------------------------------------------------

	/** 新增的子 DataLayer 名称列表（场景中存在但未注册到 Stage） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SyncStatus|Children")
	TArray<FString> NewChildDataLayers;

	/** 被删除的子 DataLayer 名称列表（已注册但场景中不存在） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SyncStatus|Children")
	TArray<FString> RemovedChildDataLayers;

	/** 未导入的子 DataLayer 名称列表（子 Act DataLayer 处于 NotImported 状态） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SyncStatus|Children")
	TArray<FString> NotImportedChildDataLayers;

	//----------------------------------------------------------------
	// Actor 变化
	//----------------------------------------------------------------

	/** 新增的 Actor 数量 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SyncStatus|Actors")
	int32 AddedActorCount = 0;

	/** 移除的 Actor 数量 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SyncStatus|Actors")
	int32 RemovedActorCount = 0;

	//----------------------------------------------------------------
	// 辅助函数
	//----------------------------------------------------------------

	/** 是否有任何变化 */
	bool HasChanges() const
	{
		return NewChildDataLayers.Num() > 0 ||
			   RemovedChildDataLayers.Num() > 0 ||
			   NotImportedChildDataLayers.Num() > 0 ||
			   AddedActorCount > 0 ||
			   RemovedActorCount > 0;
	}

	/** 是否有子 DataLayer 变化 */
	bool HasChildChanges() const
	{
		return NewChildDataLayers.Num() > 0 ||
			   RemovedChildDataLayers.Num() > 0 ||
			   NotImportedChildDataLayers.Num() > 0;
	}

	/** 是否有 Actor 变化 */
	bool HasActorChanges() const
	{
		return AddedActorCount > 0 || RemovedActorCount > 0;
	}

	/** 获取变化摘要（用于日志） */
	FString GetChangeSummary() const
	{
		if (!HasChanges())
		{
			return TEXT("No changes");
		}

		TArray<FString> Parts;
		if (NewChildDataLayers.Num() > 0)
		{
			Parts.Add(FString::Printf(TEXT("+%d new children"), NewChildDataLayers.Num()));
		}
		if (RemovedChildDataLayers.Num() > 0)
		{
			Parts.Add(FString::Printf(TEXT("-%d removed children"), RemovedChildDataLayers.Num()));
		}
		if (NotImportedChildDataLayers.Num() > 0)
		{
			Parts.Add(FString::Printf(TEXT("%d unimported children"), NotImportedChildDataLayers.Num()));
		}
		if (AddedActorCount > 0)
		{
			Parts.Add(FString::Printf(TEXT("+%d actors"), AddedActorCount));
		}
		if (RemovedActorCount > 0)
		{
			Parts.Add(FString::Printf(TEXT("-%d actors"), RemovedActorCount));
		}

		return FString::Join(Parts, TEXT(", "));
	}
};

/**
 * DataLayer 同步状态检测器
 *
 * 提供状态检测和提示生成功能。
 */
class STAGEEDITOR_API FDataLayerSyncStatusDetector
{
public:
	/**
	 * 检测 DataLayer 的同步状态
	 *
	 * @param Asset 目标 DataLayerAsset
	 * @return 同步状态详细信息
	 */
	static FDataLayerSyncStatusInfo DetectStatus(const UDataLayerAsset* Asset);

	/**
	 * 生成同步状态的提示文本
	 *
	 * @param StatusInfo 同步状态信息
	 * @return 用于 Tooltip 显示的本地化文本
	 */
	static FText GenerateTooltip(const FDataLayerSyncStatusInfo& StatusInfo);

	/**
	 * 获取状态对应的颜色
	 *
	 * @param Status 同步状态
	 * @return 用于 UI 显示的颜色
	 */
	static FLinearColor GetStatusColor(EDataLayerSyncStatus Status);

	/**
	 * 获取状态对应的图标名称
	 *
	 * @param Status 同步状态
	 * @return Slate Brush 名称
	 */
	static FName GetStatusIconName(EDataLayerSyncStatus Status);

private:
	/**
	 * 获取 DataLayer 当前的子 DataLayer 名称列表
	 */
	static TArray<FString> GetCurrentChildDataLayerNames(const UDataLayerAsset* Asset);

	/**
	 * 获取 DataLayer 当前包含的 Actor 列表
	 */
	static TArray<TSoftObjectPtr<AActor>> GetCurrentActors(const UDataLayerAsset* Asset);
};
