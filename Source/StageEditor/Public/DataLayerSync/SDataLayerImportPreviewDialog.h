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
 * DefaultAct 选择项
 */
struct FDefaultActOption
{
	/** 显示名称 */
	FString DisplayName;

	/** 对应的子 DataLayer 索引 (-1 表示空 DefaultAct) */
	int32 ChildDataLayerIndex;

	/** 对应的 DataLayerAsset (空 DefaultAct 时为 nullptr) */
	TWeakObjectPtr<UDataLayerAsset> DataLayerAsset;

	FDefaultActOption()
		: ChildDataLayerIndex(-1)
	{}

	FDefaultActOption(const FString& InDisplayName, int32 InIndex, UDataLayerAsset* InAsset = nullptr)
		: DisplayName(InDisplayName)
		, ChildDataLayerIndex(InIndex)
		, DataLayerAsset(InAsset)
	{}
};

/**
 * DataLayer 导入预览对话框
 *
 * 展示将要导入的 Stage-Act-Entity 层级结构，
 * 让用户确认后再执行导入。
 */
class STAGEEDITOR_API SDataLayerImportPreviewDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDataLayerImportPreviewDialog)
		: _DataLayerAsset(nullptr)
	{}
		SLATE_ARGUMENT(UDataLayerAsset*, DataLayerAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * 打开导入预览对话框
	 *
	 * @param DataLayerAsset 要导入的 DataLayerAsset
	 * @return 是否执行了导入
	 */
	static bool ShowDialog(UDataLayerAsset* DataLayerAsset);

private:
	/** 生成 DefaultAct 选择器 */
	TSharedRef<SWidget> BuildDefaultActSelector();

	/** 生成 Acts 列表内容 */
	TSharedRef<SWidget> BuildActsListContent();

	/** 生成命名警告内容 */
	TSharedRef<SWidget> BuildWarningContent();

	/** 处理导入按钮点击 */
	FReply OnImportClicked();

	/** 处理取消按钮点击 */
	FReply OnCancelClicked();

	/** 获取导入按钮是否可用 */
	bool IsImportButtonEnabled() const;

	/** 获取汇总文本 */
	FText GetSummaryText() const;

	/** ComboBox 生成行 Widget */
	TSharedRef<SWidget> GenerateDefaultActOptionWidget(TSharedPtr<FDefaultActOption> Option);

	/** ComboBox 选择变更 */
	void OnDefaultActSelectionChanged(TSharedPtr<FDefaultActOption> NewSelection, ESelectInfo::Type SelectInfo);

	/** 获取当前选中的 DefaultAct 显示文本 */
	FText GetSelectedDefaultActText() const;

	/** 初始化 DefaultAct 选项列表 */
	void InitializeDefaultActOptions();

private:
	/** 源 DataLayerAsset */
	TObjectPtr<UDataLayerAsset> DataLayerAsset;

	/** 导入预览 */
	FDataLayerImportPreview Preview;

	/** Act 预览项列表（仅 Act 类型项） */
	TArray<TSharedPtr<FDataLayerImportPreviewItem>> ActPreviewItems;

	/** 对话框窗口 */
	TWeakPtr<SWindow> OwnerWindow;

	/** 是否执行了导入 */
	bool bImportExecuted = false;

	/** DefaultAct 选项列表 */
	TArray<TSharedPtr<FDefaultActOption>> DefaultActOptions;

	/** 当前选中的 DefaultAct 选项 */
	TSharedPtr<FDefaultActOption> SelectedDefaultActOption;

	/** DefaultAct ComboBox */
	TSharedPtr<SComboBox<TSharedPtr<FDefaultActOption>>> DefaultActComboBox;
};
