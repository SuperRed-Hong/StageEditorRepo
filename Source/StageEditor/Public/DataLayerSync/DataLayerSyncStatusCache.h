// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataLayerSync/DataLayerSyncStatus.h"
#include "DataLayer/DataLayerAction.h"

class UDataLayerAsset;
class UDataLayerInstance;
class AActor;

/**
 * 缓存的同步状态条目
 */
struct FCachedSyncStatus
{
	/** 同步状态信息 */
	FDataLayerSyncStatusInfo Info;

	/** 缓存创建时间 */
	double CacheTime = 0;

	/** 缓存是否有效 */
	bool bIsValid = false;

	FCachedSyncStatus() = default;

	FCachedSyncStatus(const FDataLayerSyncStatusInfo& InInfo)
		: Info(InInfo)
		, CacheTime(FPlatformTime::Seconds())
		, bIsValid(true)
	{
	}
};

/**
 * DataLayer 同步状态缓存管理器 (单例)
 *
 * 设计原则:
 * - 读取优先：UI 读取直接返回缓存，无需计算
 * - 事件驱动：监听变化事件，精准失效
 * - 按需计算：缓存失效时在 GetCachedStatus 中立即重新计算
 *
 * 刷新触发方式:
 * - DataLayer 变化事件（重命名、删除、层级变化）
 * - Actor 添加/删除事件
 * - StageEditorController 操作（创建/删除 Act、同步等）
 * - 用户点击 Refresh 按钮（手动刷新）
 *
 * 性能提升:
 * - UI 刷新: 50 × O(W) → 50 × O(1) (缓存有效时)
 * - 事件触发时重新计算，确保状态准确
 */
class STAGEEDITOR_API FDataLayerSyncStatusCache
{
public:
	/** 获取单例实例 */
	static FDataLayerSyncStatusCache& Get();

	//----------------------------------------------------------------
	// 缓存读取 API - O(1)
	//----------------------------------------------------------------

	/**
	 * 获取缓存的状态（始终立即返回，不阻塞）
	 *
	 * @param Asset 目标 DataLayerAsset
	 * @return 缓存的状态信息；如无缓存返回 NotImported
	 */
	FDataLayerSyncStatusInfo GetCachedStatus(const UDataLayerAsset* Asset);

	/**
	 * 检查缓存是否存在且有效
	 *
	 * @param Asset 目标 DataLayerAsset
	 * @return 缓存是否有效
	 */
	bool HasValidCache(const UDataLayerAsset* Asset) const;

	//----------------------------------------------------------------
	// 缓存管理 API
	//----------------------------------------------------------------

	/**
	 * 使单个条目失效
	 *
	 * @param Asset 目标 DataLayerAsset
	 */
	void InvalidateCache(const UDataLayerAsset* Asset);

	/**
	 * 使所有缓存失效
	 */
	void InvalidateAll();

	/**
	 * 强制同步刷新（阻塞，用于按钮点击等需要立即结果的场景）
	 *
	 * @param Asset 目标 DataLayerAsset
	 * @return 最新的状态信息
	 */
	FDataLayerSyncStatusInfo ForceRefresh(const UDataLayerAsset* Asset);

	//----------------------------------------------------------------
	// 生命周期
	//----------------------------------------------------------------

	/** 初始化缓存系统（在 Module 启动时调用） */
	void Initialize();

	/** 关闭缓存系统（在 Module 关闭时调用） */
	void Shutdown();

	/** 检查是否已初始化 */
	bool IsInitialized() const { return bIsInitialized; }

private:
	FDataLayerSyncStatusCache() = default;
	~FDataLayerSyncStatusCache() = default;

	// 禁止拷贝
	FDataLayerSyncStatusCache(const FDataLayerSyncStatusCache&) = delete;
	FDataLayerSyncStatusCache& operator=(const FDataLayerSyncStatusCache&) = delete;

	//----------------------------------------------------------------
	// 内部状态
	//----------------------------------------------------------------

	/** 缓存存储 */
	TMap<TWeakObjectPtr<const UDataLayerAsset>, FCachedSyncStatus> Cache;

	/** 是否已初始化 */
	bool bIsInitialized = false;

	//----------------------------------------------------------------
	// 事件句柄
	//----------------------------------------------------------------

	FDelegateHandle DataLayerChangedHandle;
	FDelegateHandle ActorAddedHandle;
	FDelegateHandle ActorDeletedHandle;

	//----------------------------------------------------------------
	// 内部方法
	//----------------------------------------------------------------

	/** 计算单个 Asset 的状态（内部使用） */
	FDataLayerSyncStatusInfo ComputeStatus(const UDataLayerAsset* Asset);

	/** 绑定事件监听 */
	void BindEvents();

	/** 解绑事件监听 */
	void UnbindEvents();

	//----------------------------------------------------------------
	// 事件回调
	//----------------------------------------------------------------

	void OnDataLayerChanged(const EDataLayerAction Action, const TWeakObjectPtr<const UDataLayerInstance>& ChangedInstance, const FName& ChangedProperty);
	void OnActorAddedToWorld(AActor* Actor);
	void OnActorRemovedFromWorld(AActor* Actor);

	/** Stage 注册回调 - 当 Stage 通过 PostLoad 等方式注册时触发缓存失效 */
	void OnStageRegistered(class AStage* Stage);

	/** Stage 注销回调 - 当 Stage 卸载时触发缓存失效 */
	void OnStageUnregistered(class AStage* Stage, int32 StageID);

	/** Stage 注册/注销委托句柄 */
	FDelegateHandle StageRegisteredHandle;
	FDelegateHandle StageUnregisteredHandle;
};
