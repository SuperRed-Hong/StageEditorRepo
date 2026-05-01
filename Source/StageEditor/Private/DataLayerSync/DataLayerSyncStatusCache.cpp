// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerSync/DataLayerSyncStatusCache.h"
#include "DataLayerSync/DataLayerSyncStatus.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "DataLayer/DataLayerAction.h"
#include "Engine/Engine.h"
#include "Editor.h"
#include "Actors/Stage.h"
#include "Core/StageCoreTypes.h"
#include "Subsystems/StageManagerSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogDataLayerSyncCache, Log, All);

//----------------------------------------------------------------
// Singleton
//----------------------------------------------------------------

FDataLayerSyncStatusCache& FDataLayerSyncStatusCache::Get()
{
	static FDataLayerSyncStatusCache Instance;
	return Instance;
}

//----------------------------------------------------------------
// 生命周期
//----------------------------------------------------------------

void FDataLayerSyncStatusCache::Initialize()
{
	if (bIsInitialized)
	{
		return;
	}

	UE_LOG(LogDataLayerSyncCache, Log, TEXT("Initializing DataLayerSyncStatusCache (event-driven mode)"));

	// 绑定事件 - 纯事件驱动，不再使用后台 Ticker
	BindEvents();

	bIsInitialized = true;
}

void FDataLayerSyncStatusCache::Shutdown()
{
	if (!bIsInitialized)
	{
		return;
	}

	UE_LOG(LogDataLayerSyncCache, Log, TEXT("Shutting down DataLayerSyncStatusCache"));

	// 解绑事件
	UnbindEvents();

	// 清空缓存
	Cache.Empty();

	bIsInitialized = false;
}

//----------------------------------------------------------------
// 缓存读取
//----------------------------------------------------------------

FDataLayerSyncStatusInfo FDataLayerSyncStatusCache::GetCachedStatus(const UDataLayerAsset* Asset)
{
	if (!Asset)
	{
		FDataLayerSyncStatusInfo Info;
		Info.Status = EDataLayerSyncStatus::NotImported;
		return Info;
	}

	TWeakObjectPtr<const UDataLayerAsset> WeakAsset(Asset);

	// 查找缓存
	if (FCachedSyncStatus* CachedStatus = Cache.Find(WeakAsset))
	{
		// 缓存有效则直接返回
		if (CachedStatus->bIsValid)
		{
			return CachedStatus->Info;
		}

		// 缓存已失效，立即计算新值并更新缓存
		FDataLayerSyncStatusInfo NewInfo = ComputeStatus(Asset);
		CachedStatus->Info = NewInfo;
		CachedStatus->bIsValid = true;

		return NewInfo;
	}

	// 无缓存，立即计算并缓存
	FDataLayerSyncStatusInfo NewInfo = ComputeStatus(Asset);
	Cache.Add(WeakAsset, FCachedSyncStatus(NewInfo));

	return NewInfo;
}

bool FDataLayerSyncStatusCache::HasValidCache(const UDataLayerAsset* Asset) const
{
	if (!Asset)
	{
		return false;
	}

	TWeakObjectPtr<const UDataLayerAsset> WeakAsset(Asset);
	const FCachedSyncStatus* CachedStatus = Cache.Find(WeakAsset);

	// 不再检查过期时间，只检查缓存是否存在且有效
	return CachedStatus && CachedStatus->bIsValid;
}

//----------------------------------------------------------------
// 缓存管理
//----------------------------------------------------------------

void FDataLayerSyncStatusCache::InvalidateCache(const UDataLayerAsset* Asset)
{
	if (!Asset)
	{
		return;
	}

	TWeakObjectPtr<const UDataLayerAsset> WeakAsset(Asset);

	if (FCachedSyncStatus* CachedStatus = Cache.Find(WeakAsset))
	{
		CachedStatus->bIsValid = false;
	}
}

void FDataLayerSyncStatusCache::InvalidateAll()
{
	UE_LOG(LogDataLayerSyncCache, Log, TEXT("Invalidating all cache entries (%d)"), Cache.Num());

	for (auto& Pair : Cache)
	{
		Pair.Value.bIsValid = false;
	}
}

FDataLayerSyncStatusInfo FDataLayerSyncStatusCache::ForceRefresh(const UDataLayerAsset* Asset)
{
	if (!Asset)
	{
		FDataLayerSyncStatusInfo Info;
		Info.Status = EDataLayerSyncStatus::NotImported;
		return Info;
	}

	// 立即计算
	FDataLayerSyncStatusInfo Info = ComputeStatus(Asset);

	// 更新缓存
	TWeakObjectPtr<const UDataLayerAsset> WeakAsset(Asset);
	Cache.Add(WeakAsset, FCachedSyncStatus(Info));

	UE_LOG(LogDataLayerSyncCache, Verbose, TEXT("Force refreshed: %s -> Status=%d"),
		*Asset->GetName(), (int32)Info.Status);

	return Info;
}

//----------------------------------------------------------------
// 内部方法
//----------------------------------------------------------------

FDataLayerSyncStatusInfo FDataLayerSyncStatusCache::ComputeStatus(const UDataLayerAsset* Asset)
{
	// 调用现有的检测逻辑
	return FDataLayerSyncStatusDetector::DetectStatus(Asset);
}

//----------------------------------------------------------------
// 事件绑定
//----------------------------------------------------------------

void FDataLayerSyncStatusCache::BindEvents()
{
	// 1. DataLayer 变化事件
	if (GEditor)
	{
		if (UDataLayerEditorSubsystem* DLSubsystem = GEditor->GetEditorSubsystem<UDataLayerEditorSubsystem>())
		{
			DataLayerChangedHandle = DLSubsystem->OnDataLayerChanged().AddRaw(
				this, &FDataLayerSyncStatusCache::OnDataLayerChanged);
		}
	}

	// 2. Actor 添加/删除事件
	if (GEngine)
	{
		ActorAddedHandle = GEngine->OnLevelActorAdded().AddRaw(
			this, &FDataLayerSyncStatusCache::OnActorAddedToWorld);

		ActorDeletedHandle = GEngine->OnLevelActorDeleted().AddRaw(
			this, &FDataLayerSyncStatusCache::OnActorRemovedFromWorld);
	}

	// 3. Stage 注册/注销事件 - 监听 PostLoad 等方式注册的 Stage
	if (GEditor)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World)
		{
			if (UStageManagerSubsystem* Subsystem = World->GetSubsystem<UStageManagerSubsystem>())
			{
				StageRegisteredHandle = Subsystem->OnStageRegistered.AddRaw(
					this, &FDataLayerSyncStatusCache::OnStageRegistered);
				StageUnregisteredHandle = Subsystem->OnStageUnregistered.AddRaw(
					this, &FDataLayerSyncStatusCache::OnStageUnregistered);
			}
		}
	}

	UE_LOG(LogDataLayerSyncCache, Log, TEXT("Events bound"));
}

void FDataLayerSyncStatusCache::UnbindEvents()
{
	// 1. DataLayer 变化事件
	if (GEditor)
	{
		if (UDataLayerEditorSubsystem* DLSubsystem = GEditor->GetEditorSubsystem<UDataLayerEditorSubsystem>())
		{
			DLSubsystem->OnDataLayerChanged().Remove(DataLayerChangedHandle);
		}
	}

	// 2. Actor 事件
	if (GEngine)
	{
		GEngine->OnLevelActorAdded().Remove(ActorAddedHandle);
		GEngine->OnLevelActorDeleted().Remove(ActorDeletedHandle);
	}

	// 3. Stage 注册/注销事件
	if (GEditor)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World)
		{
			if (UStageManagerSubsystem* Subsystem = World->GetSubsystem<UStageManagerSubsystem>())
			{
				if (StageRegisteredHandle.IsValid())
				{
					Subsystem->OnStageRegistered.Remove(StageRegisteredHandle);
				}
				if (StageUnregisteredHandle.IsValid())
				{
					Subsystem->OnStageUnregistered.Remove(StageUnregisteredHandle);
				}
			}
		}
	}

	DataLayerChangedHandle.Reset();
	ActorAddedHandle.Reset();
	ActorDeletedHandle.Reset();
	StageRegisteredHandle.Reset();
	StageUnregisteredHandle.Reset();

	UE_LOG(LogDataLayerSyncCache, Log, TEXT("Events unbound"));
}

//----------------------------------------------------------------
// 事件回调
//----------------------------------------------------------------

void FDataLayerSyncStatusCache::OnDataLayerChanged(const EDataLayerAction Action, const TWeakObjectPtr<const UDataLayerInstance>& ChangedInstance, const FName& ChangedProperty)
{
	const UDataLayerInstance* Instance = ChangedInstance.Get();
	if (!Instance)
	{
		// 无法确定具体变化，失效所有
		InvalidateAll();
		return;
	}

	// 失效该 DataLayer 的缓存
	if (const UDataLayerAsset* Asset = Instance->GetAsset())
	{
		InvalidateCache(Asset);
	}

	// 也失效父 DataLayer
	if (const UDataLayerInstance* Parent = Instance->GetParent())
	{
		if (const UDataLayerAsset* ParentAsset = Parent->GetAsset())
		{
			InvalidateCache(ParentAsset);
		}
	}
}

void FDataLayerSyncStatusCache::OnActorAddedToWorld(AActor* Actor)
{
	if (!Actor || !Actor->HasDataLayers())
	{
		return;
	}

	// 只失效相关的 DataLayer 缓存
	for (const UDataLayerInstance* Instance : Actor->GetDataLayerInstances())
	{
		if (!Instance) continue;

		if (const UDataLayerAsset* Asset = Instance->GetAsset())
		{
			InvalidateCache(Asset);
		}

		// 也失效父 DataLayer（Stage 级别）
		if (const UDataLayerInstance* Parent = Instance->GetParent())
		{
			if (const UDataLayerAsset* ParentAsset = Parent->GetAsset())
			{
				InvalidateCache(ParentAsset);
			}
		}
	}

	UE_LOG(LogDataLayerSyncCache, Verbose, TEXT("OnActorAddedToWorld: %s"), *Actor->GetName());
}

void FDataLayerSyncStatusCache::OnActorRemovedFromWorld(AActor* Actor)
{
	// 与 OnActorAddedToWorld 逻辑相同
	OnActorAddedToWorld(Actor);

	UE_LOG(LogDataLayerSyncCache, Verbose, TEXT("OnActorRemovedFromWorld: %s"),
		Actor ? *Actor->GetName() : TEXT("null"));
}

void FDataLayerSyncStatusCache::OnStageRegistered(AStage* Stage)
{
	if (!Stage)
	{
		return;
	}

	// 失效 Stage 关联的 DataLayer 缓存
	if (Stage->StageDataLayerAsset)
	{
		InvalidateCache(Stage->StageDataLayerAsset);
		UE_LOG(LogDataLayerSyncCache, Log, TEXT("OnStageRegistered: Invalidated cache for Stage '%s' DataLayer '%s'"),
			*Stage->GetActorLabel(), *Stage->StageDataLayerAsset->GetName());
	}

	// 也失效所有 Act 的 DataLayer 缓存
	for (const FAct& Act : Stage->Acts)
	{
		if (Act.AssociatedDataLayer)
		{
			InvalidateCache(Act.AssociatedDataLayer);
		}
	}
}

void FDataLayerSyncStatusCache::OnStageUnregistered(AStage* Stage, int32 StageID)
{
	// Stage 可能已经被销毁，所以可能是 nullptr
	if (Stage)
	{
		// 失效 Stage 关联的 DataLayer 缓存
		if (Stage->StageDataLayerAsset)
		{
			InvalidateCache(Stage->StageDataLayerAsset);
			UE_LOG(LogDataLayerSyncCache, Log, TEXT("OnStageUnregistered: Invalidated cache for Stage '%s' (ID:%d) DataLayer '%s'"),
				*Stage->GetActorLabel(), StageID, *Stage->StageDataLayerAsset->GetName());
		}

		// 也失效所有 Act 的 DataLayer 缓存
		for (const FAct& Act : Stage->Acts)
		{
			if (Act.AssociatedDataLayer)
			{
				InvalidateCache(Act.AssociatedDataLayer);
			}
		}
	}
	else
	{
		// Stage 已销毁，无法获取具体的 DataLayer，失效所有缓存
		UE_LOG(LogDataLayerSyncCache, Log, TEXT("OnStageUnregistered: Stage (ID:%d) already destroyed, invalidating all cache"), StageID);
		InvalidateAll();
	}
}
