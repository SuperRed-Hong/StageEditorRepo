// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "DataLayerSync/DataLayerImporter.h"

class UDataLayerAsset;
class SWindow;
template<typename OptionType> class SComboBox;

/**
 * 单个 Stage 的导入配置
 */
struct FBatchImportStageEntry
{
	/** Stage DataLayer Asset */
	TWeakObjectPtr<UDataLayerAsset> StageDataLayerAsset;

	/** Stage 名称 */
	FString StageName;

	/** 子 Act DataLayers */
	TArray<TSharedPtr<FDataLayerImportPreviewItem>> ActItems;

	/** DefaultAct 选项列表 */
	TArray<TSharedPtr<FString>> DefaultActOptions;

	/** 当前选中的 DefaultAct 索引 */
	int32 SelectedDefaultActIndex = 0;

	/** 命名警告列表 */
	TArray<FDataLayerNamingWarning> NamingWarnings;

	FBatchImportStageEntry()
		: SelectedDefaultActIndex(0)
	{}
};

/**
 * 被跳过的 DataLayer 信息
 */
struct FSkippedDataLayerInfo
{
	/** DataLayer 名称 */
	FString DataLayerName;

	/** 跳过原因 */
	FString SkipReason;
};

/**
 * 批量导入预览对话框
 *
 * 支持同时预览和导入多个 Stage DataLayers。
 * 每个 Stage 独立配置 DefaultAct 选择。
 */
class STAGEEDITOR_API SBatchImportPreviewDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBatchImportPreviewDialog) {}
		SLATE_ARGUMENT(TArray<UDataLayerAsset*>, DataLayerAssets)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * 打开批量导入预览对话框
	 *
	 * @param DataLayerAssets 要导入的 DataLayerAssets 列表
	 * @return 是否执行了导入
	 */
	static bool ShowDialog(const TArray<UDataLayerAsset*>& DataLayerAssets);

private:
	/** 处理输入的 DataLayer 列表，分类为可导入的 Stages 和跳过项 */
	void ProcessInputDataLayers(const TArray<UDataLayerAsset*>& DataLayerAssets);

	/** 为单个 Stage 生成预览数据 */
	void GenerateStagePreview(UDataLayerAsset* StageAsset, FBatchImportStageEntry& OutEntry);

	/** 生成 Stage 列表内容 */
	TSharedRef<SWidget> BuildStageListContent();

	/** 生成单个 Stage 的 UI 区块 */
	TSharedRef<SWidget> BuildStageEntryWidget(int32 StageIndex);

	/** 生成跳过项警告 */
	TSharedRef<SWidget> BuildSkippedWarnings();

	/** 处理导入按钮点击 */
	FReply OnImportClicked();

	/** 处理取消按钮点击 */
	FReply OnCancelClicked();

	/** 获取导入按钮是否可用 */
	bool IsImportButtonEnabled() const;

	/** 获取汇总文本 */
	FText GetSummaryText() const;

	/** ComboBox 生成行 Widget */
	TSharedRef<SWidget> GenerateDefaultActOptionWidget(TSharedPtr<FString> Option);

	/** ComboBox 选择变更 */
	void OnDefaultActSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo, int32 StageIndex);

	/** 获取指定 Stage 当前选中的 DefaultAct 显示文本 */
	FText GetSelectedDefaultActText(int32 StageIndex) const;

private:
	/** 可导入的 Stage 条目列表 */
	TArray<TSharedPtr<FBatchImportStageEntry>> StageEntries;

	/** 被跳过的 DataLayer 列表 */
	TArray<FSkippedDataLayerInfo> SkippedDataLayers;

	/** 对话框窗口 */
	TWeakPtr<SWindow> OwnerWindow;

	/** 是否执行了导入 */
	bool bImportExecuted = false;

	/** 总 Act 数量 */
	int32 TotalActCount = 0;

	/** DefaultAct ComboBoxes (每个 Stage 一个) */
	TArray<TSharedPtr<SComboBox<TSharedPtr<FString>>>> DefaultActComboBoxes;
};
