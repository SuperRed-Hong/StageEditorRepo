// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StageDataLayerNameParser.generated.h"

/**
 * DataLayer 名称解析结果
 *
 * 用于解析符合 StageEditor 命名规范的 DataLayer：
 * - Stage: DL_Stage_<StageName>
 * - Act: DL_Act_<StageName>_<ActName>
 */
USTRUCT(BlueprintType)
struct STAGEEDITOR_API FDataLayerNameParseResult
{
	GENERATED_BODY()

	/** 是否符合命名规范 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DataLayerSync")
	bool bIsValid = false;

	/** 是否为 Stage 级别（否则为 Act 级别） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DataLayerSync")
	bool bIsStageLayer = false;

	/** Stage 名称（Stage 和 Act 级别都有效） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DataLayerSync")
	FString StageName;

	/** Act 名称（仅 Act 级别有效） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DataLayerSync")
	FString ActName;

	/** 获取完整的 DataLayer 名称（用于验证或重建） */
	FString ToDataLayerName() const
	{
		if (!bIsValid)
		{
			return FString();
		}

		if (bIsStageLayer)
		{
			return FString::Printf(TEXT("DL_Stage_%s"), *StageName);
		}
		else
		{
			return FString::Printf(TEXT("DL_Act_%s_%s"), *StageName, *ActName);
		}
	}

	/** 获取用于显示的友好名称 */
	FString GetDisplayName() const
	{
		if (!bIsValid)
		{
			return FString();
		}

		if (bIsStageLayer)
		{
			return StageName;
		}
		else
		{
			return ActName;
		}
	}
};

/**
 * DataLayer 名称解析器
 *
 * 命名规范：
 * - Stage 级别: DL_Stage_<StageName>
 *   例：DL_Stage_Forest, DL_Stage_NightClub
 *
 * - Act 级别: DL_Act_<StageName>_<ActName>
 *   例：DL_Act_NightClub_Battle, DL_Act_Forest_RoomEnter
 *
 * 规则说明：
 * - DL_Act_ 后面第一个下划线分隔的部分是 StageName
 * - 剩余部分作为 ActName（支持包含下划线）
 */
class STAGEEDITOR_API FStageDataLayerNameParser
{
public:
	/**
	 * 解析 DataLayer 名称
	 *
	 * @param DataLayerName 待解析的 DataLayer 名称
	 * @return 解析结果，bIsValid 指示是否符合命名规范
	 */
	static FDataLayerNameParseResult Parse(const FString& DataLayerName);

	/**
	 * 检查名称是否符合 Stage 命名规范
	 *
	 * @param DataLayerName 待检查的名称
	 * @return 是否符合 DL_Stage_* 模式
	 */
	static bool IsStageDataLayer(const FString& DataLayerName);

	/**
	 * 检查名称是否符合 Act 命名规范
	 *
	 * @param DataLayerName 待检查的名称
	 * @return 是否符合 DL_Act_<StageName>_<ActName> 模式
	 */
	static bool IsActDataLayer(const FString& DataLayerName);

	/**
	 * 检查名称是否符合任一命名规范
	 *
	 * @param DataLayerName 待检查的名称
	 * @return 是否符合 Stage 或 Act 命名规范
	 */
	static bool IsValidStageEditorDataLayer(const FString& DataLayerName);

	/**
	 * 生成 Stage DataLayer 名称
	 *
	 * @param StageName Stage 名称
	 * @return 格式化的 DataLayer 名称 (DL_Stage_<StageName>)
	 */
	static FString MakeStageDataLayerName(const FString& StageName);

	/**
	 * 生成 Act DataLayer 名称
	 *
	 * @param StageName Stage 名称
	 * @param ActName Act 名称
	 * @return 格式化的 DataLayer 名称 (DL_Act_<StageName>_<ActName>)
	 */
	static FString MakeActDataLayerName(const FString& StageName, const FString& ActName);

private:
	// Stage 模式前缀
	static const FString StagePrefix;

	// Act 模式前缀
	static const FString ActPrefix;
};
