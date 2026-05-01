// Copyright Stage Editor Plugin. All Rights Reserved.

#include "DataLayerSync/StageDataLayerColumns.h"
#include "DataLayerSync/StageDataLayerTreeItem.h"
#include "DataLayerSync/DataLayerSyncStatus.h"
#include "DataLayerSync/DataLayerSyncStatusCache.h"
#include "DataLayerSync/DataLayerImporter.h"
#include "DataLayerSync/DataLayerSynchronizer.h"
#include "DataLayerSync/SDataLayerImportPreviewDialog.h"
#include "DataLayerSync/SStdRenamePopup.h"
#include "DataLayerSync/StageDataLayerNameParser.h"

#include "Misc/MessageDialog.h"

#include "Actors/Stage.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "Engine/World.h"
#include "ISceneOutlinerTreeItem.h"
#include "Math/ColorList.h"
#include "ScopedTransaction.h"
#include "SSceneOutliner.h"
#include "Styling/AppStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STreeView.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"

#define LOCTEXT_NAMESPACE "StageDataLayerColumns"

//----------------------------------------------------------------
// FStageDataLayerSyncStatusColumn
//----------------------------------------------------------------

FStageDataLayerSyncStatusColumn::FStageDataLayerSyncStatusColumn(ISceneOutliner& SceneOutliner)
	: WeakSceneOutliner(StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared()))
{
}

FName FStageDataLayerSyncStatusColumn::GetID()
{
	static FName ColumnID("SyncStatus");
	return ColumnID;
}

SHeaderRow::FColumn::FArguments FStageDataLayerSyncStatusColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.FixedWidth(24.f)
		.HAlignHeader(HAlign_Center)
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Center)
		.VAlignCell(VAlign_Center)
		.DefaultTooltip(LOCTEXT("SyncStatusColumnTooltip", "Sync Status"))
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush(TEXT("Icons.Info")))
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

const TSharedRef<SWidget> FStageDataLayerSyncStatusColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (const FStageDataLayerTreeItem* DataLayerItem = TreeItem->CastTo<FStageDataLayerTreeItem>())
	{
		UDataLayerInstance* DataLayerInstance = DataLayerItem->GetDataLayer();
		if (!DataLayerInstance)
		{
			return SNullWidget::NullWidget;
		}

		const UDataLayerAsset* DataLayerAsset = DataLayerInstance->GetAsset();
		if (!DataLayerAsset)
		{
			return SNullWidget::NullWidget;
		}

		// Get status from cache - compute once at widget construction time
		// UI will be rebuilt when events trigger RefreshTree()
		FDataLayerSyncStatusInfo StatusInfo = FDataLayerSyncStatusCache::Get().GetCachedStatus(DataLayerAsset);

		const FSlateBrush* StatusBrush = nullptr;
		FSlateColor StatusColor;
		FText StatusTooltip;

		switch (StatusInfo.Status)
		{
		case EDataLayerSyncStatus::Synced:
			StatusBrush = FAppStyle::GetBrush(TEXT("Icons.Check"));
			StatusColor = FLinearColor::Green;
			StatusTooltip = LOCTEXT("StatusSynced", "Synced with Stage/Act");
			break;
		case EDataLayerSyncStatus::OutOfSync:
			StatusBrush = FAppStyle::GetBrush(TEXT("Icons.Warning"));
			StatusColor = FLinearColor::Yellow;
			StatusTooltip = FText::Format(LOCTEXT("StatusOutOfSync", "Out of sync: {0}"),
				FText::FromString(StatusInfo.GetChangeSummary()));
			break;
		case EDataLayerSyncStatus::NotImported:
		default:
			StatusBrush = FAppStyle::GetBrush(TEXT("Icons.Plus"));
			StatusColor = FLinearColor(0.3f, 0.7f, 1.0f); // Light blue
			StatusTooltip = LOCTEXT("StatusNotImported", "Not imported, or associated Stage Actor not loaded (WP streaming)");
			break;
		}

		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(StatusBrush)
				.ColorAndOpacity(StatusColor)
				.ToolTipText(StatusTooltip)
			];
	}

	return SNullWidget::NullWidget;
}

void FStageDataLayerSyncStatusColumn::SortItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems, const EColumnSortMode::Type SortMode) const
{
	// Sort by status priority: OutOfSync > NotImported > Synced
	OutItems.Sort([SortMode](const FSceneOutlinerTreeItemPtr& A, const FSceneOutlinerTreeItemPtr& B)
	{
		auto GetStatusPriority = [](const FSceneOutlinerTreeItemPtr& Item) -> int32
		{
			if (const FStageDataLayerTreeItem* DataLayerItem = Item->CastTo<FStageDataLayerTreeItem>())
			{
				if (UDataLayerInstance* Instance = DataLayerItem->GetDataLayer())
				{
					if (const UDataLayerAsset* Asset = Instance->GetAsset())
					{
						FDataLayerSyncStatusInfo StatusInfo = FDataLayerSyncStatusCache::Get().GetCachedStatus(Asset);
						switch (StatusInfo.Status)
						{
						case EDataLayerSyncStatus::OutOfSync: return 0;
						case EDataLayerSyncStatus::NotImported: return 1;
						case EDataLayerSyncStatus::Synced: return 2;
						}
					}
				}
			}
			return 3;
		};

		int32 PriorityA = GetStatusPriority(A);
		int32 PriorityB = GetStatusPriority(B);

		return SortMode == EColumnSortMode::Ascending ? PriorityA < PriorityB : PriorityA > PriorityB;
	});
}

//----------------------------------------------------------------
// FStageDataLayerActionsColumn
//----------------------------------------------------------------

FStageDataLayerActionsColumn::FStageDataLayerActionsColumn(ISceneOutliner& SceneOutliner)
	: WeakSceneOutliner(StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared()))
{
}

FName FStageDataLayerActionsColumn::GetID()
{
	static FName ColumnID("Actions");
	return ColumnID;
}

SHeaderRow::FColumn::FArguments FStageDataLayerActionsColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		// Width controlled by FillSize (0.2f) in ColumnInfo - SceneOutliner auto-applies FillWidth
		.HAlignHeader(HAlign_Center)
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Left)
		.VAlignCell(VAlign_Center)
		.DefaultTooltip(LOCTEXT("ActionsColumnTooltip", "Actions"))
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ActionsHeader", "Actions"))
		];
}

const TSharedRef<SWidget> FStageDataLayerActionsColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (const FStageDataLayerTreeItem* DataLayerItem = TreeItem->CastTo<FStageDataLayerTreeItem>())
	{
		UDataLayerInstance* DataLayerInstance = DataLayerItem->GetDataLayer();
		if (!DataLayerInstance)
		{
			return SNullWidget::NullWidget;
		}

		UDataLayerAsset* DataLayerAsset = const_cast<UDataLayerAsset*>(DataLayerInstance->GetAsset());
		if (!DataLayerAsset)
		{
			return SNullWidget::NullWidget;
		}

		// Get current status
		FDataLayerSyncStatusInfo StatusInfo = FDataLayerSyncStatusCache::Get().GetCachedStatus(DataLayerAsset);

		// Parse DataLayer name to determine type
		FDataLayerNameParseResult ParseResult = FStageDataLayerNameParser::Parse(DataLayerAsset->GetName());

		// Build horizontal box with buttons
		TSharedRef<SHorizontalBox> ButtonBox = SNew(SHorizontalBox);

		// Always show Std Rename button first
		ButtonBox->AddSlot()
			.AutoWidth()
			.Padding(0, 0, 2, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("StdRenameButton", "Std Rename"))
				.ToolTipText(LOCTEXT("StdRenameButtonTooltip", "Rename to standard format (DL_Stage_ or DL_Act_)"))
				.ContentPadding(FMargin(4.f, 1.f))
				.OnClicked(this, &FStageDataLayerActionsColumn::OnStdRenameClicked, DataLayerAsset, DataLayerInstance)
			];

		// Always show Import button for Stage-level DataLayers (regardless of SyncStatus)
		// This allows re-importing or importing with different settings
		if (ParseResult.bIsValid && ParseResult.bIsStageLayer)
		{
			ButtonBox->AddSlot()
				.AutoWidth()
				.Padding(2, 0, 0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ImportButton", "Import"))
					.ToolTipText(LOCTEXT("ImportButtonTooltip", "Import this DataLayer into StageEditor"))
					.ContentPadding(FMargin(4.f, 1.f))
					.OnClicked(this, &FStageDataLayerActionsColumn::OnImportClicked, DataLayerAsset, DataLayerInstance)
				];
		}

		// Show Sync button only for OutOfSync status
		if (StatusInfo.Status == EDataLayerSyncStatus::OutOfSync)
		{
			ButtonBox->AddSlot()
				.AutoWidth()
				.Padding(2, 0, 0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("SyncButton", "Sync"))
					.ToolTipText(FText::Format(LOCTEXT("SyncButtonTooltip", "Sync changes: {0}"),
						FText::FromString(StatusInfo.GetChangeSummary())))
					.ContentPadding(FMargin(4.f, 1.f))
					.OnClicked(this, &FStageDataLayerActionsColumn::OnSyncClicked, DataLayerAsset)
				];
		}

		return ButtonBox;
	}

	return SNullWidget::NullWidget;
}

FReply FStageDataLayerActionsColumn::OnImportClicked(UDataLayerAsset* DataLayerAsset, UDataLayerInstance* DataLayerInstance)
{
	if (!DataLayerAsset)
	{
		return FReply::Handled();
	}

	// Check if naming follows convention
	FString AssetName = DataLayerAsset->GetName();
	FDataLayerNameParseResult ParseResult = FStageDataLayerNameParser::Parse(AssetName);

	if (!ParseResult.bIsValid)
	{
		// Warning: Non-standard naming
		FText WarningTitle = LOCTEXT("ImportWarningTitle", "Non-Standard DataLayer Name");
		FText WarningMessage = FText::Format(
			LOCTEXT("ImportWarningMessage",
				"The DataLayer \"{0}\" does not follow the naming convention.\n\n"
				"Expected formats:\n"
				"  - Stage: DL_Stage_<Name>\n"
				"  - Act: DL_Act_<Name>\n\n"
				"Do you want to import it anyway?"),
			FText::FromString(DataLayerAsset->GetName()));

		EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, WarningMessage, WarningTitle);
		if (Result != EAppReturnType::Yes)
		{
			return FReply::Handled();
		}
	}

	// Proceed with import
	SDataLayerImportPreviewDialog::ShowDialog(DataLayerAsset);
	return FReply::Handled();
}

FReply FStageDataLayerActionsColumn::OnSyncClicked(UDataLayerAsset* DataLayerAsset)
{
	if (DataLayerAsset)
	{
		FDataLayerSyncResult Result = FDataLayerSynchronizer::SyncDataLayer(DataLayerAsset);
		if (Result.bSuccess)
		{
			UE_LOG(LogTemp, Log, TEXT("Synced DataLayer %s: Added %d Acts, Removed %d Acts, Added %d Entities, Removed %d Entities"),
				*DataLayerAsset->GetName(),
				Result.AddedActCount, Result.RemovedActCount,
				Result.AddedEntityCount, Result.RemovedEntityCount);

			// Invalidate cache and refresh outliner
			FDataLayerSyncStatusCache::Get().InvalidateCache(DataLayerAsset);
			if (TSharedPtr<ISceneOutliner> Outliner = WeakSceneOutliner.Pin())
			{
				Outliner->FullRefresh();
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to sync DataLayer %s: %s"),
				*DataLayerAsset->GetName(), *Result.ErrorMessage.ToString());
		}
	}
	return FReply::Handled();
}

FReply FStageDataLayerActionsColumn::OnStdRenameClicked(UDataLayerAsset* DataLayerAsset, UDataLayerInstance* DataLayerInstance)
{
	if (DataLayerAsset)
	{
		SStdRenamePopup::ShowPopup(DataLayerAsset, DataLayerInstance);
	}
	return FReply::Handled();
}

//----------------------------------------------------------------
// FStageDataLayerVisibilityColumn
//----------------------------------------------------------------

/** Custom visibility widget for Stage DataLayer items */
class SStageDataLayerVisibilityWidget : public SVisibilityWidget
{
protected:
	virtual bool IsEnabled() const override
	{
		auto TreeItem = WeakTreeItem.Pin();
		if (FStageDataLayerTreeItem* DataLayerTreeItem = TreeItem.IsValid() ? TreeItem->CastTo<FStageDataLayerTreeItem>() : nullptr)
		{
			const UDataLayerInstance* DataLayer = DataLayerTreeItem->GetDataLayer();
			const UDataLayerInstance* ParentDataLayer = DataLayer ? DataLayer->GetParent() : nullptr;
			const bool bIsParentVisible = ParentDataLayer ? ParentDataLayer->IsEffectiveVisible() : true;
			return bIsParentVisible && DataLayer && DataLayer->GetWorld() && !DataLayer->GetWorld()->IsPlayInEditor() && DataLayer->IsEffectiveLoadedInEditor();
		}
		return false;
	}

	virtual const FSlateBrush* GetBrush() const override
	{
		bool bIsEffectiveVisible = false;
		auto TreeItem = WeakTreeItem.Pin();
		if (FStageDataLayerTreeItem* DataLayerTreeItem = TreeItem.IsValid() ? TreeItem->CastTo<FStageDataLayerTreeItem>() : nullptr)
		{
			UDataLayerInstance* DataLayer = DataLayerTreeItem->GetDataLayer();
			bIsEffectiveVisible = DataLayer && DataLayer->IsEffectiveVisible();
		}

		if (bIsEffectiveVisible)
		{
			return IsHovered() ? VisibleHoveredBrush : VisibleNotHoveredBrush;
		}
		else
		{
			return IsHovered() ? NotVisibleHoveredBrush : NotVisibleNotHoveredBrush;
		}
	}

	virtual FSlateColor GetForegroundColor() const override
	{
		if (IsEnabled())
		{
			auto Outliner = WeakOutliner.Pin();
			auto TreeItem = WeakTreeItem.Pin();
			const bool bIsSelected = Outliner.IsValid() && Outliner->GetTree().IsItemSelected(TreeItem.ToSharedRef());
			if (IsVisible() && !Row->IsHovered() && !bIsSelected)
			{
				return FLinearColor::Transparent;
			}
			return FAppStyle::Get().GetSlateColor("Colors.ForegroundHover");
		}
		else
		{
			return FLinearColor(FColorList::DimGrey);
		}
	}

	virtual bool ShouldPropagateVisibilityChangeOnChildren() const override { return false; }
};

FStageDataLayerVisibilityColumn::FStageDataLayerVisibilityColumn(ISceneOutliner& SceneOutliner)
	: FSceneOutlinerGutter(SceneOutliner)
{
}

FName FStageDataLayerVisibilityColumn::GetID()
{
	static FName ColumnID("StageDataLayerVisibility");
	return ColumnID;
}

const TSharedRef<SWidget> FStageDataLayerVisibilityColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (TreeItem->ShouldShowVisibilityState())
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SStageDataLayerVisibilityWidget, SharedThis(this), WeakOutliner, TreeItem, &Row)
			];
	}
	return SNullWidget::NullWidget;
}

//----------------------------------------------------------------
// FStageDataLayerLoadedInEditorColumn
//----------------------------------------------------------------

FStageDataLayerLoadedInEditorColumn::FStageDataLayerLoadedInEditorColumn(ISceneOutliner& SceneOutliner)
	: WeakSceneOutliner(StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared()))
{
}

FName FStageDataLayerLoadedInEditorColumn::GetID()
{
	static FName ColumnID("StageDataLayerLoadedInEditor");
	return ColumnID;
}

SHeaderRow::FColumn::FArguments FStageDataLayerLoadedInEditorColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.FixedWidth(24.f)
		.HAlignHeader(HAlign_Center)
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Center)
		.VAlignCell(VAlign_Center)
		.DefaultTooltip(LOCTEXT("LoadedInEditorColumnTooltip", "Loaded In Editor"))
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush(TEXT("DataLayer.LoadedInEditor")))
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

const TSharedRef<SWidget> FStageDataLayerLoadedInEditorColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (TreeItem->IsA<FStageDataLayerTreeItem>())
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(0, 0, 0, 0)
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsEnabled_Lambda([TreeItem]()
				{
					FStageDataLayerTreeItem* DataLayerTreeItem = TreeItem->CastTo<FStageDataLayerTreeItem>();
					const UDataLayerInstance* DataLayer = DataLayerTreeItem ? DataLayerTreeItem->GetDataLayer() : nullptr;
					const UDataLayerInstance* ParentDataLayer = DataLayer ? DataLayer->GetParent() : nullptr;
					const bool bIsParentLoaded = ParentDataLayer ? ParentDataLayer->IsEffectiveLoadedInEditor() : true;
					return bIsParentLoaded && DataLayer && DataLayer->GetWorld() && !DataLayer->GetWorld()->IsPlayInEditor();
				})
				.Visibility_Lambda([TreeItem]()
				{
					FStageDataLayerTreeItem* DataLayerTreeItem = TreeItem->CastTo<FStageDataLayerTreeItem>();
					const UDataLayerInstance* DataLayerInstance = DataLayerTreeItem ? DataLayerTreeItem->GetDataLayer() : nullptr;
					const AWorldDataLayers* OuterWorldDataLayers = DataLayerInstance ? DataLayerInstance->GetDirectOuterWorldDataLayers() : nullptr;
					const bool bIsSubWorldDataLayers = OuterWorldDataLayers && OuterWorldDataLayers->IsSubWorldDataLayers();
					return DataLayerInstance && !DataLayerInstance->IsReadOnly() && !bIsSubWorldDataLayers ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.IsChecked_Lambda([TreeItem]()
				{
					FStageDataLayerTreeItem* DataLayerTreeItem = TreeItem->CastTo<FStageDataLayerTreeItem>();
					UDataLayerInstance* DataLayer = DataLayerTreeItem ? DataLayerTreeItem->GetDataLayer() : nullptr;
					return DataLayer && DataLayer->IsEffectiveLoadedInEditor() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this, TreeItem](ECheckBoxState NewState)
				{
					FStageDataLayerTreeItem* DataLayerTreeItem = TreeItem->CastTo<FStageDataLayerTreeItem>();
					if (UDataLayerInstance* DataLayer = DataLayerTreeItem ? DataLayerTreeItem->GetDataLayer() : nullptr)
					{
						UDataLayerEditorSubsystem* DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get();
						TSharedPtr<ISceneOutliner> SceneOutliner = WeakSceneOutliner.Pin();
						if (SceneOutliner.IsValid() && SceneOutliner->GetTree().IsItemSelected(TreeItem))
						{
							// Toggle IsLoadedInEditor flag of selected DataLayers to the same state as the given DataLayer
							const bool bIsLoadedInEditor = DataLayer->IsLoadedInEditor();

							TArray<UDataLayerInstance*> AllSelectedDataLayers;
							for (auto& SelectedItem : SceneOutliner->GetTree().GetSelectedItems())
							{
								FStageDataLayerTreeItem* SelectedDataLayerTreeItem = SelectedItem->CastTo<FStageDataLayerTreeItem>();
								UDataLayerInstance* SelectedDataLayer = SelectedDataLayerTreeItem ? SelectedDataLayerTreeItem->GetDataLayer() : nullptr;
								if (SelectedDataLayer && SelectedDataLayer->IsLoadedInEditor() == bIsLoadedInEditor)
								{
									AllSelectedDataLayers.Add(SelectedDataLayer);
								}
							}

							const FScopedTransaction Transaction(LOCTEXT("ToggleDataLayersIsLoadedInEditor", "Toggle Data Layers Loaded In Editor"));
							DataLayerEditorSubsystem->ToggleDataLayersIsLoadedInEditor(AllSelectedDataLayers, /*bIsFromUserChange*/true);
						}
						else
						{
							const FScopedTransaction Transaction(LOCTEXT("ToggleDataLayerIsLoadedInEditor", "Toggle Data Layer Loaded In Editor"));
							DataLayerEditorSubsystem->ToggleDataLayerIsLoadedInEditor(DataLayer, /*bIsFromUserChange*/true);
						}
					}
				})
				.ToolTipText(LOCTEXT("IsLoadedInEditorCheckBoxToolTip", "Toggle Loaded In Editor"))
				.HAlign(HAlign_Center)
			];
	}
	return SNullWidget::NullWidget;
}

//----------------------------------------------------------------
// FStageDataLayerSUIDColumn
//----------------------------------------------------------------

FStageDataLayerSUIDColumn::FStageDataLayerSUIDColumn(ISceneOutliner& SceneOutliner)
	: WeakSceneOutliner(StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared()))
{
}

FName FStageDataLayerSUIDColumn::GetID()
{
	static FName ColumnID("SUID");
	return ColumnID;
}

SHeaderRow::FColumn::FArguments FStageDataLayerSUIDColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		// Width controlled by FillSize (0.1f) in ColumnInfo - SceneOutliner auto-applies FillWidth
		.HAlignHeader(HAlign_Center)
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Center)
		.VAlignCell(VAlign_Center)
		.DefaultTooltip(LOCTEXT("SUIDColumnTooltip", "Stage Unique ID"))
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SUIDHeader", "SUID"))
		];
}

const TSharedRef<SWidget> FStageDataLayerSUIDColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (const FStageDataLayerTreeItem* DataLayerItem = TreeItem->CastTo<FStageDataLayerTreeItem>())
	{
		UDataLayerInstance* DataLayerInstance = DataLayerItem->GetDataLayer();
		if (!DataLayerInstance)
		{
			return SNullWidget::NullWidget;
		}

		const UDataLayerAsset* DataLayerAsset = DataLayerInstance->GetAsset();
		if (!DataLayerAsset)
		{
			return SNullWidget::NullWidget;
		}

		return SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this, DataLayerAsset]() -> FText
				{
					return FText::FromString(GetSUIDDisplay(DataLayerAsset));
				})
				.ColorAndOpacity_Lambda([DataLayerAsset]() -> FSlateColor
				{
					// Get status to determine color
					FDataLayerSyncStatusInfo StatusInfo = FDataLayerSyncStatusCache::Get().GetCachedStatus(DataLayerAsset);
					if (StatusInfo.Status == EDataLayerSyncStatus::Synced)
					{
						return FSlateColor::UseForeground();
					}
					return FLinearColor::Gray;
				})
			];
	}

	return SNullWidget::NullWidget;
}

FString FStageDataLayerSUIDColumn::GetSUIDDisplay(const UDataLayerAsset* DataLayerAsset) const
{
	if (!DataLayerAsset)
	{
		return TEXT("-");
	}

	// Get StageManagerSubsystem
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return TEXT("-");
	}

	UStageManagerSubsystem* Subsystem = World->GetSubsystem<UStageManagerSubsystem>();
	if (!Subsystem)
	{
		return TEXT("-");
	}

	// Find Stage for this DataLayer
	AStage* Stage = Subsystem->FindStageByDataLayer(const_cast<UDataLayerAsset*>(DataLayerAsset));
	if (!Stage)
	{
		return TEXT("-");
	}

	// Check if this is a Stage-level or Act-level DataLayer
	FDataLayerNameParseResult ParseResult = FStageDataLayerNameParser::Parse(DataLayerAsset->GetName());

	if (ParseResult.bIsValid)
	{
		if (ParseResult.bIsStageLayer)
		{
			// Stage-level DataLayer: "S:X"
			return FString::Printf(TEXT("S:%d"), Stage->GetStageID());
		}
		else
		{
			// Act-level DataLayer: "A:X.Y"
			int32 ActID = Subsystem->FindActIDByDataLayer(Stage, const_cast<UDataLayerAsset*>(DataLayerAsset));
			if (ActID > 0)
			{
				return FString::Printf(TEXT("A:%d.%d"), Stage->GetStageID(), ActID);
			}
		}
	}

	return TEXT("-");
}

void FStageDataLayerSUIDColumn::SortItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems, const EColumnSortMode::Type SortMode) const
{
	// Sort by SUID string
	OutItems.Sort([this, SortMode](const FSceneOutlinerTreeItemPtr& A, const FSceneOutlinerTreeItemPtr& B)
	{
		auto GetSortKey = [this](const FSceneOutlinerTreeItemPtr& Item) -> FString
		{
			if (const FStageDataLayerTreeItem* DataLayerItem = Item->CastTo<FStageDataLayerTreeItem>())
			{
				if (UDataLayerInstance* Instance = DataLayerItem->GetDataLayer())
				{
					if (const UDataLayerAsset* Asset = Instance->GetAsset())
					{
						return GetSUIDDisplay(Asset);
					}
				}
			}
			return TEXT("~"); // Sort unmanaged to end
		};

		FString KeyA = GetSortKey(A);
		FString KeyB = GetSortKey(B);

		return SortMode == EColumnSortMode::Ascending ? KeyA < KeyB : KeyA > KeyB;
	});
}

#undef LOCTEXT_NAMESPACE
