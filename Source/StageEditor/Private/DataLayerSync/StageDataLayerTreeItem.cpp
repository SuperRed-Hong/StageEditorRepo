// Copyright Stage Editor Plugin. All Rights Reserved.

#include "DataLayerSync/StageDataLayerTreeItem.h"

#include "DataLayer/DataLayerEditorSubsystem.h"
#include "ISceneOutliner.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerInstanceWithAsset.h"
#include "WorldPartition/DataLayer/DataLayerUtils.h"

#define LOCTEXT_NAMESPACE "StageDataLayerTreeItem"

//----------------------------------------------------------------
// Static Type Definition
//----------------------------------------------------------------

const FSceneOutlinerTreeItemType FStageDataLayerTreeItem::Type(&ISceneOutlinerTreeItem::Type);

//----------------------------------------------------------------
// Construction
//----------------------------------------------------------------

FStageDataLayerTreeItem::FStageDataLayerTreeItem(UDataLayerInstance* InDataLayerInstance)
	: ISceneOutlinerTreeItem(Type)
	, DataLayerInstance(InDataLayerInstance)
	, ID(InDataLayerInstance)
	, bIsHighlightedIfSelected(false)
{
}

//----------------------------------------------------------------
// ISceneOutlinerTreeItem Implementation
//----------------------------------------------------------------

FString FStageDataLayerTreeItem::GetDisplayString() const
{
	if (const UDataLayerInstance* DataLayer = DataLayerInstance.Get())
	{
		return DataLayer->GetDataLayerShortName();
	}
	return FString();
}

bool FStageDataLayerTreeItem::CanInteract() const
{
	if (const UDataLayerInstance* DataLayer = DataLayerInstance.Get())
	{
		return !DataLayer->IsReadOnly();
	}
	return false;
}

TSharedRef<SWidget> FStageDataLayerTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	// Get display name
	FString DisplayName;
	if (const UDataLayerInstance* DataLayer = DataLayerInstance.Get())
	{
		DisplayName = DataLayer->GetDataLayerShortName();
	}

	// Build label with inline editable text support
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.0f, 0.0f, 2.0f, 0.0f))
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush(TEXT("DataLayer.Editor")))
			.ColorAndOpacity_Lambda([this]() -> FSlateColor
			{
				if (const UDataLayerInstance* DataLayer = GetDataLayer())
				{
					return DataLayer->GetDebugColor();
				}
				return FSlateColor::UseForeground();
			})
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SInlineEditableTextBlock)
			.Text(FText::FromString(DisplayName))
			.HighlightText(Outliner.GetFilterHighlightText())
			.ColorAndOpacity_Lambda([this]() -> FSlateColor
			{
				if (ShouldBeHighlighted())
				{
					return FSlateColor(FLinearColor::Yellow);
				}
				return FSlateColor::UseForeground();
			})
			.OnTextCommitted_Lambda([this](const FText& InText, ETextCommit::Type CommitType)
			{
				if (UDataLayerInstance* DataLayer = GetDataLayer())
				{
					if (!DataLayer->IsReadOnly() && DataLayer->CanEditDataLayerShortName())
					{
						FDataLayerUtils::SetDataLayerShortName(DataLayer, InText.ToString());
					}
				}
			})
			.IsReadOnly_Lambda([this]()
			{
				if (const UDataLayerInstance* DataLayer = GetDataLayer())
				{
					return DataLayer->IsReadOnly() || !DataLayer->CanEditDataLayerShortName();
				}
				return true;
			})
		];
}

void FStageDataLayerTreeItem::OnVisibilityChanged(const bool bNewVisibility)
{
	if (UDataLayerInstance* DataLayer = DataLayerInstance.Get())
	{
		if (UDataLayerEditorSubsystem* DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get())
		{
			DataLayerEditorSubsystem->SetDataLayerIsLoadedInEditor(DataLayer, bNewVisibility, /*bIsFromUserChange*/ true);
		}
	}
}

bool FStageDataLayerTreeItem::GetVisibility() const
{
	if (const UDataLayerInstance* DataLayer = DataLayerInstance.Get())
	{
		return DataLayer->IsEffectiveLoadedInEditor();
	}
	return false;
}

bool FStageDataLayerTreeItem::ShouldShowVisibilityState() const
{
	if (const UDataLayerInstance* DataLayer = DataLayerInstance.Get())
	{
		return DataLayer->IsRuntime();
	}
	return false;
}

//----------------------------------------------------------------
// Highlight Support
//----------------------------------------------------------------

bool FStageDataLayerTreeItem::ShouldBeHighlighted() const
{
	if (!bIsHighlightedIfSelected)
	{
		return false;
	}

	if (const UDataLayerInstance* DataLayer = DataLayerInstance.Get())
	{
		if (UDataLayerEditorSubsystem* DataLayerEditorSubsystem = UDataLayerEditorSubsystem::Get())
		{
			return DataLayerEditorSubsystem->DoesDataLayerContainSelectedActors(DataLayer);
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
