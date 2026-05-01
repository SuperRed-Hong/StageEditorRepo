// Copyright Stage Editor Plugin. All Rights Reserved.

#include "EditorUI/StageEditorColumns.h"
#include "EditorUI/StageEditorTreeItems.h"
#include "EditorLogic/StageEditorController.h"
#include "Actors/Stage.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"

#include "DataLayerSync/StageDataLayerNameParser.h"
#include "ISceneOutliner.h"
#include "SSceneOutliner.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Engine/Blueprint.h"

#define LOCTEXT_NAMESPACE "StageEditorColumns"

//----------------------------------------------------------------
// FStageEditorNameColumn
//----------------------------------------------------------------

FStageEditorNameColumn::FStageEditorNameColumn(ISceneOutliner& SceneOutliner)
	: WeakSceneOutliner(StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared()))
{
}

SHeaderRow::FColumn::FArguments FStageEditorNameColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.DefaultLabel(LOCTEXT("NameColumnHeader", "Name"))
		.DefaultTooltip(LOCTEXT("NameColumnTooltip", "Item name"))
		.FillWidth(1.0f);
}

const TSharedRef<SWidget> FStageEditorNameColumn::ConstructRowWidget(
	FSceneOutlinerTreeItemRef TreeItem,
	const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	FString DisplayName = GetDisplayName(*TreeItem);

	// Get highlight text for search filtering
	TAttribute<FText> HighlightText;
	if (TSharedPtr<ISceneOutliner> Outliner = WeakSceneOutliner.Pin())
	{
		HighlightText = Outliner->GetFilterHighlightText();
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(FMargin(2.0f, 0.0f))
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(DisplayName))
			.HighlightText(HighlightText)
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

void FStageEditorNameColumn::PopulateSearchStrings(
	const ISceneOutlinerTreeItem& Item,
	TArray<FString>& OutSearchStrings) const
{
	OutSearchStrings.Add(GetDisplayName(Item));
}

void FStageEditorNameColumn::SortItems(
	TArray<FSceneOutlinerTreeItemPtr>& RootItems,
	const EColumnSortMode::Type SortMode) const
{
	RootItems.Sort([this, SortMode](const FSceneOutlinerTreeItemPtr& A, const FSceneOutlinerTreeItemPtr& B)
	{
		FString NameA = GetDisplayName(*A);
		FString NameB = GetDisplayName(*B);

		return SortMode == EColumnSortMode::Ascending
			? NameA < NameB
			: NameA > NameB;
	});
}

FString FStageEditorNameColumn::GetDisplayName(const ISceneOutlinerTreeItem& Item) const
{
	// Stage - parse from DataLayer name
	if (const FStageEditorStageTreeItem* StageItem = Item.CastTo<FStageEditorStageTreeItem>())
	{
		AStage* Stage = StageItem->GetStage();
		if (Stage && Stage->StageDataLayerAsset)
		{
			FDataLayerNameParseResult Result = FStageDataLayerNameParser::Parse(
				Stage->StageDataLayerAsset->GetName());
			if (Result.bIsValid && Result.bIsStageLayer)
			{
				return Result.StageName;
			}
		}
		// Fallback to actor label
		return Stage ? Stage->GetActorLabel() : TEXT("(Invalid Stage)");
	}

	// ActsFolder
	if (Item.IsA<FStageEditorActsFolderTreeItem>())
	{
		return TEXT("Acts");
	}

	// EntitiesFolder
	if (Item.IsA<FStageEditorEntitiesFolderTreeItem>())
	{
		return TEXT("Registered Entities");
	}

	// Act - parse from DataLayer name or use DisplayName
	if (const FStageEditorActTreeItem* ActItem = Item.CastTo<FStageEditorActTreeItem>())
	{
		FAct* Act = ActItem->GetAct();
		if (Act)
		{
			// Try to parse from AssociatedDataLayer
			if (Act->AssociatedDataLayer)
			{
				FDataLayerNameParseResult Result = FStageDataLayerNameParser::Parse(
					Act->AssociatedDataLayer->GetName());
				if (Result.bIsValid && !Result.bIsStageLayer)
				{
					return Result.ActName;
				}
			}
			// Fallback to DisplayName
			return Act->DisplayName;
		}
		return TEXT("(Invalid Act)");
	}

	// Entity (flat list under EntitiesFolder)
	if (const FStageEditorEntityTreeItem* EntityItem = Item.CastTo<FStageEditorEntityTreeItem>())
	{
		AActor* Actor = EntityItem->GetEntityActor();
		return Actor ? Actor->GetActorLabel() : TEXT("(Invalid Entity)");
	}

	// Entity under Act (with state override)
	if (const FStageEditorEntityUnderActTreeItem* EntityUnderActItem = Item.CastTo<FStageEditorEntityUnderActTreeItem>())
	{
		AActor* Actor = EntityUnderActItem->GetEntityActor();
		return Actor ? Actor->GetActorLabel() : TEXT("(Invalid Entity)");
	}

	return TEXT("(Unknown)");
}

//----------------------------------------------------------------
// FStageEditorIDColumn
//----------------------------------------------------------------

FStageEditorIDColumn::FStageEditorIDColumn(ISceneOutliner& SceneOutliner)
	: WeakSceneOutliner(StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared()))
{
}

SHeaderRow::FColumn::FArguments FStageEditorIDColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.DefaultLabel(LOCTEXT("IDColumnHeader", "ID"))
		.DefaultTooltip(LOCTEXT("IDColumnTooltip", "Stage ID or Entity ID"))
		.FixedWidth(50.0f);
}

const TSharedRef<SWidget> FStageEditorIDColumn::ConstructRowWidget(
	FSceneOutlinerTreeItemRef TreeItem,
	const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	FText IDText = GetIDText(*TreeItem);

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(FMargin(2.0f, 0.0f))
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Right)
		[
			SNew(STextBlock)
			.Text(IDText)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}

void FStageEditorIDColumn::SortItems(
	TArray<FSceneOutlinerTreeItemPtr>& RootItems,
	const EColumnSortMode::Type SortMode) const
{
	RootItems.Sort([this, SortMode](const FSceneOutlinerTreeItemPtr& A, const FSceneOutlinerTreeItemPtr& B)
	{
		int32 IDA = GetIDValue(*A);
		int32 IDB = GetIDValue(*B);

		return SortMode == EColumnSortMode::Ascending
			? IDA < IDB
			: IDA > IDB;
	});
}

FText FStageEditorIDColumn::GetIDText(const ISceneOutlinerTreeItem& Item) const
{
	int32 ID = GetIDValue(Item);
	if (ID > 0)
	{
		return FText::AsNumber(ID);
	}
	return FText::GetEmpty();
}

int32 FStageEditorIDColumn::GetIDValue(const ISceneOutlinerTreeItem& Item) const
{
	// Stage
	if (const FStageEditorStageTreeItem* StageItem = Item.CastTo<FStageEditorStageTreeItem>())
	{
		AStage* Stage = StageItem->GetStage();
		return Stage ? Stage->GetStageID() : 0;
	}

	// Entity (flat)
	if (const FStageEditorEntityTreeItem* EntityItem = Item.CastTo<FStageEditorEntityTreeItem>())
	{
		return EntityItem->GetEntityID();
	}

	// Entity under Act
	if (const FStageEditorEntityUnderActTreeItem* EntityUnderActItem = Item.CastTo<FStageEditorEntityUnderActTreeItem>())
	{
		return EntityUnderActItem->GetEntityID();
	}

	// Act - show ActID (index)
	if (const FStageEditorActTreeItem* ActItem = Item.CastTo<FStageEditorActTreeItem>())
	{
		return ActItem->GetActIndex();
	}

	return 0;
}

//----------------------------------------------------------------
// FStageEditorStateColumn
//----------------------------------------------------------------

FStageEditorStateColumn::FStageEditorStateColumn(ISceneOutliner& SceneOutliner)
	: WeakSceneOutliner(StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared()))
{
}

SHeaderRow::FColumn::FArguments FStageEditorStateColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.DefaultLabel(LOCTEXT("StateColumnHeader", "State"))
		.DefaultTooltip(LOCTEXT("StateColumnTooltip", "Entity state value in this Act"))
		.FixedWidth(50.0f);
}

const TSharedRef<SWidget> FStageEditorStateColumn::ConstructRowWidget(
	FSceneOutlinerTreeItemRef TreeItem,
	const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	int32 StateValue = GetStateValue(*TreeItem);

	// Only show state for EntityUnderAct items
	FText StateText = StateValue >= 0
		? FText::AsNumber(StateValue)
		: FText::GetEmpty();

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(FMargin(2.0f, 0.0f))
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Text(StateText)
			.ColorAndOpacity(StateValue >= 0 ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground())
		];
}

void FStageEditorStateColumn::SortItems(
	TArray<FSceneOutlinerTreeItemPtr>& RootItems,
	const EColumnSortMode::Type SortMode) const
{
	RootItems.Sort([this, SortMode](const FSceneOutlinerTreeItemPtr& A, const FSceneOutlinerTreeItemPtr& B)
	{
		int32 StateA = GetStateValue(*A);
		int32 StateB = GetStateValue(*B);

		return SortMode == EColumnSortMode::Ascending
			? StateA < StateB
			: StateA > StateB;
	});
}

int32 FStageEditorStateColumn::GetStateValue(const ISceneOutlinerTreeItem& Item) const
{
	// Only EntityUnderAct items have state
	if (const FStageEditorEntityUnderActTreeItem* EntityUnderActItem = Item.CastTo<FStageEditorEntityUnderActTreeItem>())
	{
		return EntityUnderActItem->GetEntityState();
	}

	return -1; // Invalid/not applicable
}

//----------------------------------------------------------------
// Helper to get StageManagerSubsystem from PIE or Game world
//----------------------------------------------------------------

namespace StageEditorColumnsPrivate
{
	static UStageManagerSubsystem* GetStageManagerSubsystemForWatch()
	{
		if (!GEngine) return nullptr;

		// Try PIE world first
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
			{
				if (UWorld* World = Context.World())
				{
					if (UStageManagerSubsystem* Subsystem = World->GetSubsystem<UStageManagerSubsystem>())
					{
						return Subsystem;
					}
				}
			}
		}

		return nullptr;
	}
}

//----------------------------------------------------------------
// FStageEditorWatchColumn
//----------------------------------------------------------------

FStageEditorWatchColumn::FStageEditorWatchColumn(ISceneOutliner& SceneOutliner)
	: WeakSceneOutliner(StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared()))
{
}

SHeaderRow::FColumn::FArguments FStageEditorWatchColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.DefaultLabel(FText::GetEmpty())
		.DefaultTooltip(LOCTEXT("WatchColumnHeaderTooltip", "Toggle Stage watch state for Debug HUD"))
		.FixedWidth(24.0f)
		.HeaderContent()
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush("Level.VisibleIcon16x"))
			.ToolTipText(LOCTEXT("WatchColumnHeader_Tooltip", "Toggle Stage watch state for Debug HUD"))
		];
}

const TSharedRef<SWidget> FStageEditorWatchColumn::ConstructRowWidget(
	FSceneOutlinerTreeItemRef TreeItem,
	const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	// Only show watch toggle for Stage items
	const FStageEditorStageTreeItem* StageItem = TreeItem->CastTo<FStageEditorStageTreeItem>();
	if (!StageItem)
	{
		return SNullWidget::NullWidget;
	}

	AStage* Stage = StageItem->GetStage();
	if (!Stage)
	{
		return SNullWidget::NullWidget;
	}

	TWeakObjectPtr<AStage> WeakStage = Stage;
	int32 StageID = Stage->GetStageID();

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(0))
			.ToolTipText_Lambda([WeakStage, StageID]()
			{
				// Check PIE first
				UStageManagerSubsystem* Subsystem = StageEditorColumnsPrivate::GetStageManagerSubsystemForWatch();
				if (Subsystem)
				{
					bool bIsWatched = Subsystem->IsStageWatched(StageID);
					return bIsWatched
						? LOCTEXT("WatchButton_Unwatch_PIE", "Click to stop watching (PIE active)")
						: LOCTEXT("WatchButton_Watch_PIE", "Click to watch in Debug HUD (PIE active)");
				}

				// Editor mode - show editor watch state
				if (AStage* StagePtr = WeakStage.Get())
				{
					return StagePtr->bEditorWatched
						? LOCTEXT("WatchButton_Unwatch_Editor", "Click to unmark for Debug HUD (will sync when PIE starts)")
						: LOCTEXT("WatchButton_Watch_Editor", "Click to mark for Debug HUD (will sync when PIE starts)");
				}

				return LOCTEXT("WatchButton_Invalid", "Stage not available");
			})
			.OnClicked_Lambda([WeakStage, StageID]()
			{
				// Check PIE first
				UStageManagerSubsystem* Subsystem = StageEditorColumnsPrivate::GetStageManagerSubsystemForWatch();
				if (Subsystem)
				{
					// PIE mode - toggle in Subsystem AND sync back to Stage
					if (Subsystem->IsStageWatched(StageID))
					{
						Subsystem->UnwatchStage(StageID);
						if (AStage* StagePtr = WeakStage.Get())
						{
							StagePtr->bEditorWatched = false;
						}
					}
					else
					{
						Subsystem->WatchStage(StageID);
						if (AStage* StagePtr = WeakStage.Get())
						{
							StagePtr->bEditorWatched = true;
						}
					}
				}
				else
				{
					// Editor mode - toggle bEditorWatched
					if (AStage* StagePtr = WeakStage.Get())
					{
						StagePtr->Modify();  // Support Undo
						StagePtr->bEditorWatched = !StagePtr->bEditorWatched;
					}
				}
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image_Lambda([WeakStage, StageID]() -> const FSlateBrush*
				{
					// Check PIE first
					UStageManagerSubsystem* Subsystem = StageEditorColumnsPrivate::GetStageManagerSubsystemForWatch();
					if (Subsystem)
					{
						bool bIsWatched = Subsystem->IsStageWatched(StageID);
						return bIsWatched
							? FAppStyle::GetBrush("Level.VisibleIcon16x")
							: FAppStyle::GetBrush("Level.NotVisibleIcon16x");
					}

					// Editor mode - use bEditorWatched
					if (AStage* StagePtr = WeakStage.Get())
					{
						return StagePtr->bEditorWatched
							? FAppStyle::GetBrush("Level.VisibleIcon16x")
							: FAppStyle::GetBrush("Level.NotVisibleIcon16x");
					}

					return FAppStyle::GetBrush("Level.NotVisibleIcon16x");
				})
				.ColorAndOpacity_Lambda([WeakStage, StageID]() -> FSlateColor
				{
					// Check PIE first
					UStageManagerSubsystem* Subsystem = StageEditorColumnsPrivate::GetStageManagerSubsystemForWatch();
					if (Subsystem)
					{
						bool bIsWatched = Subsystem->IsStageWatched(StageID);
						return bIsWatched
							? FSlateColor(FLinearColor(0.2f, 0.8f, 0.2f))  // Green when watched
							: FSlateColor::UseForeground();
					}

					// Editor mode - use bEditorWatched with different color
					if (AStage* StagePtr = WeakStage.Get())
					{
						return StagePtr->bEditorWatched
							? FSlateColor(FLinearColor(0.5f, 0.7f, 1.0f))  // Light blue for editor preset
							: FSlateColor::UseForeground();
					}

					return FSlateColor::UseForeground();
				})
			]
		];
}

//----------------------------------------------------------------
// FStageEditorActionsColumn
//----------------------------------------------------------------

FStageEditorActionsColumn::FStageEditorActionsColumn(ISceneOutliner& SceneOutliner, TWeakPtr<FStageEditorController> InController)
	: WeakSceneOutliner(StaticCastSharedRef<ISceneOutliner>(SceneOutliner.AsShared()))
	, WeakController(InController)
{
}

SHeaderRow::FColumn::FArguments FStageEditorActionsColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.DefaultLabel(LOCTEXT("ActionsColumnHeader", "Actions"))
		.DefaultTooltip(LOCTEXT("ActionsColumnTooltip", "Quick actions for the item"))
		.FixedWidth(120.0f);
}

const TSharedRef<SWidget> FStageEditorActionsColumn::ConstructRowWidget(
	FSceneOutlinerTreeItemRef TreeItem,
	const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	TSharedRef<SHorizontalBox> ColumnContent = SNew(SHorizontalBox);

	// Stage actions
	if (const FStageEditorStageTreeItem* StageItem = TreeItem->CastTo<FStageEditorStageTreeItem>())
	{
		AStage* Stage = StageItem->GetStage();
		if (!Stage)
		{
			return ColumnContent;
		}

		TWeakObjectPtr<AStage> WeakStage = Stage;
		TWeakPtr<ISceneOutliner> WeakOutliner = WeakSceneOutliner;

		// Browse to Asset button
		ColumnContent->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2, 0, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("BrowseToStageBP_Tooltip", "Browse to Stage Blueprint in Content Browser"))
			.OnClicked_Lambda([WeakStage]()
			{
				if (AStage* StagePtr = WeakStage.Get())
				{
					if (UBlueprint* Blueprint = Cast<UBlueprint>(StagePtr->GetClass()->ClassGeneratedBy))
					{
						TArray<UObject*> Assets;
						Assets.Add(Blueprint);
						GEditor->SyncBrowserToObjects(Assets);
					}
				}
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.BrowseContent"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];

		// Edit BP button
		ColumnContent->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2, 0, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("EditStageBP_Tooltip", "Edit Stage Blueprint"))
			.OnClicked_Lambda([WeakStage]()
			{
				if (AStage* StagePtr = WeakStage.Get())
				{
					if (UBlueprint* Blueprint = Cast<UBlueprint>(StagePtr->GetClass()->ClassGeneratedBy))
					{
						GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
					}
				}
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Edit"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];

		// Focus in Viewport button
		ColumnContent->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2, 0, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("FocusStage_Tooltip", "Focus this Stage in the viewport"))
			.OnClicked_Lambda([WeakStage]()
			{
				if (AStage* StagePtr = WeakStage.Get())
				{
					// Select and focus on the Stage
					GEditor->SelectNone(false, true);
					GEditor->SelectActor(StagePtr, true, true);
					GEditor->MoveViewportCamerasToActor(*StagePtr, false);
				}
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Search"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
	}

	// ActsFolder actions - Create Act button
	if (const FStageEditorActsFolderTreeItem* ActsFolder = TreeItem->CastTo<FStageEditorActsFolderTreeItem>())
	{
		TWeakPtr<FStageEditorController> CapturedController = WeakController;
		AStage* ParentStage = ActsFolder->GetOwnerStage();
		TWeakObjectPtr<AStage> WeakStage = ParentStage;

		ColumnContent->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(2, 0, 0, 0)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("CreateActInline_Tooltip", "Create a new Act in this Stage"))
			.OnClicked_Lambda([CapturedController, WeakStage]()
			{
				if (TSharedPtr<FStageEditorController> Controller = CapturedController.Pin())
				{
					if (AStage* StagePtr = WeakStage.Get())
					{
						Controller->SetActiveStage(StagePtr);
						Controller->CreateNewAct();
					}
				}
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Plus"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
	}

	// Entity actions - Focus button
	if (const FStageEditorEntityTreeItem* EntityItem = TreeItem->CastTo<FStageEditorEntityTreeItem>())
	{
		AActor* EntityActor = EntityItem->GetEntityActor();
		if (EntityActor)
		{
			TWeakObjectPtr<AActor> WeakEntity = EntityActor;

			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("FocusEntity_Tooltip", "Focus this Entity in the viewport"))
				.OnClicked_Lambda([WeakEntity]()
				{
					if (AActor* EntityPtr = WeakEntity.Get())
					{
						GEditor->SelectNone(false, true);
						GEditor->SelectActor(EntityPtr, true, true);
						GEditor->MoveViewportCamerasToActor(*EntityPtr, false);
					}
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Search"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
		}
	}

	// EntityUnderAct actions - Focus button
	if (const FStageEditorEntityUnderActTreeItem* EntityUnderActItem = TreeItem->CastTo<FStageEditorEntityUnderActTreeItem>())
	{
		AActor* EntityActor = EntityUnderActItem->GetEntityActor();
		if (EntityActor)
		{
			TWeakObjectPtr<AActor> WeakEntity = EntityActor;

			ColumnContent->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("FocusEntityUnderAct_Tooltip", "Focus this Entity in the viewport"))
				.OnClicked_Lambda([WeakEntity]()
				{
					if (AActor* EntityPtr = WeakEntity.Get())
					{
						GEditor->SelectNone(false, true);
						GEditor->SelectActor(EntityPtr, true, true);
						GEditor->MoveViewportCamerasToActor(*EntityPtr, false);
					}
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Search"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
		}
	}

	return ColumnContent;
}

#undef LOCTEXT_NAMESPACE
