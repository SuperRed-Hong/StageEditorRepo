// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "DataLayerSync/DataLayerSyncStatus.h"
#include "DataLayerSync/StageDataLayerNameParser.h"

class UDataLayerAsset;
class SWindow;

/**
 * 单个需要同步的 DataLayer 条目
 */
struct FSyncPreviewEntry
{
	/** DataLayer Asset */
	TWeakObjectPtr<UDataLayerAsset> DataLayerAsset;

	/** DataLayer 名称 */
	FString DataLayerName;

	/** 是否是 Stage 级别（否则是 Act 级别） */
	bool bIsStageLevel = false;

	/** 同步状态信息 */
	FDataLayerSyncStatusInfo StatusInfo;

	/** 命名解析结果 */
	FDataLayerNameParseResult ParseResult;

	/** 是否有命名警告 */
	bool bHasNamingWarning = false;

	/** 命名警告消息 */
	FString NamingWarningMessage;
};

/**
 * 同步预览对话框
 *
 * 在执行 SyncAll 之前显示预览，包括：
 * - 需要同步的 DataLayer 列表
 * - 每个 DataLayer 的变化详情
 * - 命名规范警告
 */
class STAGEEDITOR_API SSyncPreviewDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSyncPreviewDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * 打开同步预览对话框
	 *
	 * @return 是否执行了同步
	 */
	static bool ShowDialog();

private:
	/** 收集需要同步的 DataLayer 列表 */
	void CollectSyncTargets();

	/** 检查命名规范 */
	void ValidateNaming(FSyncPreviewEntry& Entry);

	/** 构建主内容 */
	TSharedRef<SWidget> BuildContent();

	/** 构建 Stage 级别同步项 */
	TSharedRef<SWidget> BuildStageSyncItems();

	/** 构建 Act 级别同步项 */
	TSharedRef<SWidget> BuildActSyncItems();

	/** 构建命名警告区域 */
	TSharedRef<SWidget> BuildNamingWarnings();

	/** 构建单个条目的 UI */
	TSharedRef<SWidget> BuildEntryWidget(const FSyncPreviewEntry& Entry, bool bIsLast);

	/** 构建变化详情文本 */
	FText GetChangeDetailsText(const FDataLayerSyncStatusInfo& StatusInfo) const;

	/** 处理同步按钮点击 */
	FReply OnSyncClicked();

	/** 处理取消按钮点击 */
	FReply OnCancelClicked();

	/** 获取同步按钮是否可用 */
	bool IsSyncButtonEnabled() const;

	/** 获取汇总文本 */
	FText GetSummaryText() const;

private:
	/** Stage 级别的同步条目 */
	TArray<TSharedPtr<FSyncPreviewEntry>> StageLevelEntries;

	/** Act 级别的同步条目 */
	TArray<TSharedPtr<FSyncPreviewEntry>> ActLevelEntries;

	/** 有命名警告的条目 */
	TArray<TSharedPtr<FSyncPreviewEntry>> EntriesWithWarnings;

	/** 对话框窗口 */
	TWeakPtr<SWindow> OwnerWindow;

	/** 是否执行了同步 */
	bool bSyncExecuted = false;

	/** 是否有严重警告（阻止同步） */
	bool bHasCriticalWarning = false;
};
