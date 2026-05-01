// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataLayerSync/StageDataLayerNameParser.h"
#include "DataLayerImporter.generated.h"

class UDataLayerAsset;
class UDataLayerInstance;
class AStage;
class AActor;
class UWorld;

/**
 * 导入预览项 - 用于展示将要创建的内容
 * 注意：不使用递归数组（UHT 不支持），使用 Depth 表示层级
 */
USTRUCT(BlueprintType)
struct STAGEEDITOR_API FDataLayerImportPreviewItem
{
	GENERATED_BODY()

	/** 显示名称 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	FString DisplayName;

	/** SUID 显示（S:1, A:1.1 等） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	FString SUIDDisplay;

	/** 类型（Stage, Act, Entities） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	FString ItemType;

	/** 对应的 DataLayerAsset（如果是 Stage 或 Act） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	TObjectPtr<UDataLayerAsset> DataLayerAsset;

	/** 层级深度（用于缩进显示）：0=Stage, 1=Act, 2=Entities */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	int32 Depth = 0;

	/** Actor 数量（仅对 Entities 类型有效） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	int32 ActorCount = 0;
};

/**
 * 命名不规范的子 DataLayer 警告信息
 */
USTRUCT(BlueprintType)
struct STAGEEDITOR_API FDataLayerNamingWarning
{
	GENERATED_BODY()

	/** DataLayer 名称 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	FString DataLayerName;

	/** 对应的 DataLayerAsset */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	TObjectPtr<UDataLayerAsset> DataLayerAsset;

	/** 警告原因 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	FString WarningReason;
};

/**
 * 导入预览结果
 */
USTRUCT(BlueprintType)
struct STAGEEDITOR_API FDataLayerImportPreview
{
	GENERATED_BODY()

	/** 是否有效 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	bool bIsValid = false;

	/** 错误消息（如果无效） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	FText ErrorMessage;

	/** 源 DataLayer 名称 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	FString SourceDataLayerName;

	/** 预览项列表（扁平化，使用 Depth 表示层级） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	TArray<FDataLayerImportPreviewItem> Items;

	/** Stage 数量 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	int32 StageCount = 0;

	/** Act 数量 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	int32 ActCount = 0;

	/** Entity 数量 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	int32 EntityCount = 0;

	/** 命名不规范的子 DataLayer 警告列表 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	TArray<FDataLayerNamingWarning> NamingWarnings;

	/** 是否有命名警告 */
	bool HasNamingWarnings() const { return NamingWarnings.Num() > 0; }
};

/**
 * 导入执行参数
 */
USTRUCT(BlueprintType)
struct STAGEEDITOR_API FDataLayerImportParams
{
	GENERATED_BODY()

	/** 要导入的 Stage DataLayer */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import")
	TObjectPtr<UDataLayerAsset> StageDataLayerAsset;

	/**
	 * 选择作为 DefaultAct 的子 DataLayer 索引 (在 Preview.Items 中的索引)
	 * -1 表示使用空的 DefaultAct (无关联 DataLayer)
	 * 0 或更大表示选择的子 DataLayer 索引
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import")
	int32 SelectedDefaultActIndex = 0;

	/** Stage Blueprint class to use for instantiation (required) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Import")
	TSubclassOf<AStage> StageBlueprintClass;
};

/**
 * 导入执行结果
 */
USTRUCT(BlueprintType)
struct STAGEEDITOR_API FDataLayerImportResult
{
	GENERATED_BODY()

	/** 是否成功 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	bool bSuccess = false;

	/** 错误消息（如果失败） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	FText ErrorMessage;

	/** 创建的 Stage Actor */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	TObjectPtr<AStage> CreatedStage;

	/** 创建的 Act 数量 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	int32 CreatedActCount = 0;

	/** 注册的 Entity 数量 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Import")
	int32 RegisteredEntityCount = 0;
};

/**
 * DataLayer 导入器
 *
 * 负责将符合命名规范的 DataLayer 层级结构导入为 Stage-Act-Entity 架构。
 *
 * 使用流程：
 * 1. GeneratePreview() - 生成预览，展示将要创建的内容
 * 2. ExecuteImport() - 执行导入
 */
class STAGEEDITOR_API FDataLayerImporter
{
public:
	/**
	 * 生成导入预览
	 *
	 * @param StageDataLayerAsset 要导入的 Stage 级别 DataLayerAsset
	 * @param World 目标 World
	 * @return 预览结果
	 */
	static FDataLayerImportPreview GeneratePreview(UDataLayerAsset* StageDataLayerAsset, UWorld* World);

	/**
	 * 执行导入
	 *
	 * @param Params 导入参数（包含 StageDataLayerAsset 和 DefaultAct 选择）
	 * @param World 目标 World
	 * @return 导入结果
	 */
	static FDataLayerImportResult ExecuteImport(const FDataLayerImportParams& Params, UWorld* World);

	/**
	 * 执行导入（简化版本，使用默认 DefaultAct 选择）
	 *
	 * @param StageDataLayerAsset 要导入的 Stage 级别 DataLayerAsset
	 * @param World 目标 World
	 * @return 导入结果
	 */
	static FDataLayerImportResult ExecuteImport(UDataLayerAsset* StageDataLayerAsset, UWorld* World);

	/**
	 * 验证 DataLayer 是否可以被导入
	 *
	 * @param DataLayerAsset 要验证的 DataLayerAsset
	 * @param World 目标 World
	 * @param OutErrorMessage 错误消息输出
	 * @return 是否可以导入
	 */
	static bool CanImport(UDataLayerAsset* DataLayerAsset, UWorld* World, FText& OutErrorMessage);

private:
	/**
	 * 获取 DataLayer 的子 DataLayer 列表
	 */
	static TArray<UDataLayerInstance*> GetChildDataLayers(UDataLayerAsset* ParentAsset, UWorld* World);

	/**
	 * 获取 DataLayer 中的 Actor 列表
	 */
	static TArray<AActor*> GetActorsInDataLayer(UDataLayerAsset* Asset, UWorld* World);

	/**
	 * 创建 Stage Actor
	 *
	 * @param StageName Stage 名称
	 * @param StageBlueprintClass Blueprint 类（required）
	 * @param World 目标 World
	 * @return 创建的 Stage Actor
	 */
	static AStage* CreateStageActor(const FString& StageName, TSubclassOf<AStage> StageBlueprintClass, UWorld* World);

	/**
	 * 分配 SUID
	 */
	static void AssignSUID(AStage* Stage);
};
