// Copyright Stage Editor Plugin. All Rights Reserved.

#include "EditorUI/StageEditorOutliner.h"
#include "EditorUI/StageEditorMode.h"
#include "EditorUI/StageEditorTreeItems.h"
#include "EditorUI/StageEditorColumns.h"
#include "EditorLogic/StageEditorController.h"
#include "Actors/Stage.h"
#include "SSceneOutliner.h"
#include "SceneOutlinerModule.h"
#include "SceneOutlinerPublicTypes.h"
#include "Modules/ModuleManager.h"
#include "Editor.h"

void SStageEditorOutliner::Construct(const FArguments& InArgs)
{
	WeakController = InArgs._Controller;
	World = InArgs._World;

	// If no world specified, use editor world
	if (!World.IsValid() && GEditor)
	{
		World = GEditor->GetEditorWorldContext().World();
	}

	ChildSlot
	[
		CreateOutliner()
	];
}

SStageEditorOutliner::~SStageEditorOutliner()
{
	// Note: ModePtr is owned by SceneOutliner, do NOT delete it here
	ModePtr = nullptr;
	SceneOutliner.Reset();
}

TSharedRef<SWidget> SStageEditorOutliner::CreateOutliner()
{
	// Get SceneOutliner module
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");

	// Capture raw pointer - safe because ModeFactory is called synchronously during CreateSceneOutliner
	// and this widget outlives the SceneOutliner
	SStageEditorOutliner* RawThis = this;
	TWeakObjectPtr<UWorld> CapturedWorld = World;
	TWeakPtr<FStageEditorController> CapturedController = WeakController;

	// Setup initialization options with custom mode factory
	FSceneOutlinerInitializationOptions InitOptions;
	InitOptions.bShowHeaderRow = true;
	InitOptions.bShowSearchBox = true;
	InitOptions.bShowCreateNewFolder = false;
	InitOptions.bFocusSearchBoxWhenOpened = false;

	// Register custom columns
	// Watch column (leftmost, icon only)
	InitOptions.ColumnMap.Add(
		FStageEditorColumnIDs::Watch(),
		FSceneOutlinerColumnInfo(
			ESceneOutlinerColumnVisibility::Visible,
			-10, // Priority (negative = leftmost)
			FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& Outliner) {
				return MakeShareable(new FStageEditorWatchColumn(Outliner));
			}),
			true, // bCanBeHidden
			TOptional<float>(), // FillSize
			NSLOCTEXT("StageEditorOutliner", "WatchColumn", "Watch")
		)
	);

	// Name column (primary, with fill width)
	InitOptions.ColumnMap.Add(
		FStageEditorColumnIDs::Name(),
		FSceneOutlinerColumnInfo(
			ESceneOutlinerColumnVisibility::Visible,
			0, // Priority (lower = more to the left)
			FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& Outliner) {
				return MakeShareable(new FStageEditorNameColumn(Outliner));
			}),
			false, // bCanBeHidden
			TOptional<float>(), // FillSize (auto)
			NSLOCTEXT("StageEditorOutliner", "NameColumn", "Name")
		)
	);

	// ID column (fixed width)
	InitOptions.ColumnMap.Add(
		FStageEditorColumnIDs::ID(),
		FSceneOutlinerColumnInfo(
			ESceneOutlinerColumnVisibility::Visible,
			10, // Priority
			FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& Outliner) {
				return MakeShareable(new FStageEditorIDColumn(Outliner));
			}),
			true, // bCanBeHidden
			TOptional<float>(), // FillSize
			NSLOCTEXT("StageEditorOutliner", "IDColumn", "ID")
		)
	);

	// State column (fixed width)
	InitOptions.ColumnMap.Add(
		FStageEditorColumnIDs::State(),
		FSceneOutlinerColumnInfo(
			ESceneOutlinerColumnVisibility::Visible,
			20, // Priority
			FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& Outliner) {
				return MakeShareable(new FStageEditorStateColumn(Outliner));
			}),
			true, // bCanBeHidden
			TOptional<float>(), // FillSize
			NSLOCTEXT("StageEditorOutliner", "StateColumn", "State")
		)
	);

	// Actions column (rightmost)
	InitOptions.ColumnMap.Add(
		FStageEditorColumnIDs::Actions(),
		FSceneOutlinerColumnInfo(
			ESceneOutlinerColumnVisibility::Visible,
			30, // Priority (higher = more to the right)
			FCreateSceneOutlinerColumn::CreateLambda([CapturedController](ISceneOutliner& Outliner) {
				return MakeShareable(new FStageEditorActionsColumn(Outliner, CapturedController));
			}),
			true, // bCanBeHidden
			TOptional<float>(), // FillSize
			NSLOCTEXT("StageEditorOutliner", "ActionsColumn", "Actions")
		)
	);

	// Set the mode factory - this is how custom modes are created in UE 5.6
	// Note: ModeFactory delegate returns raw pointer (ISceneOutlinerMode*), SceneOutliner takes ownership
	// Do NOT use MakeShared - SceneOutliner manages the lifetime
	InitOptions.ModeFactory = FCreateSceneOutlinerMode::CreateLambda(
		[RawThis, CapturedWorld, CapturedController](SSceneOutliner* InOutliner) -> ISceneOutlinerMode*
		{
			FStageEditorModeParams ModeParams(InOutliner, RawThis, CapturedWorld, CapturedController);
			FStageEditorMode* NewMode = new FStageEditorMode(ModeParams);
			RawThis->ModePtr = NewMode;  // Store raw pointer for access
			return NewMode;
		}
	);

	// Create the SceneOutliner
	SceneOutliner = StaticCastSharedRef<SSceneOutliner>(
		SceneOutlinerModule.CreateSceneOutliner(InitOptions)
	);

	return SceneOutliner.ToSharedRef();
}

void SStageEditorOutliner::Refresh()
{
	if (SceneOutliner.IsValid())
	{
		SceneOutliner->Refresh();
	}
}

TArray<AStage*> SStageEditorOutliner::GetSelectedStages() const
{
	if (ModePtr)
	{
		return ModePtr->GetSelectedStages();
	}
	return TArray<AStage*>();
}

TArray<AActor*> SStageEditorOutliner::GetSelectedEntities() const
{
	if (ModePtr)
	{
		return ModePtr->GetSelectedEntities();
	}
	return TArray<AActor*>();
}

void SStageEditorOutliner::SetSelection(AStage* Stage)
{
	if (!SceneOutliner.IsValid() || !Stage)
	{
		return;
	}

	// Find the tree item for this Stage
	FSceneOutlinerTreeItemID ItemID(static_cast<const UObject*>(Stage));
	FSceneOutlinerTreeItemPtr Item = SceneOutliner->GetTreeItem(ItemID);

	// Clear current selection and select the Stage item
	SceneOutliner->ClearSelection();
	if (Item.IsValid())
	{
		SceneOutliner->SetItemSelection(Item, true, ESelectInfo::Direct);
	}
}

void SStageEditorOutliner::SetSelection(AActor* Entity)
{
	if (!SceneOutliner.IsValid() || !Entity)
	{
		return;
	}

	// Find the tree item for this Entity
	FSceneOutlinerTreeItemID ItemID(static_cast<const UObject*>(Entity));
	FSceneOutlinerTreeItemPtr Item = SceneOutliner->GetTreeItem(ItemID);

	// Clear current selection and select the Entity item
	SceneOutliner->ClearSelection();
	if (Item.IsValid())
	{
		SceneOutliner->SetItemSelection(Item, true, ESelectInfo::Direct);
	}
}

void SStageEditorOutliner::ExpandAll()
{
	if (SceneOutliner.IsValid())
	{
		SceneOutliner->ExpandAll();
	}
}

void SStageEditorOutliner::CollapseAll()
{
	if (SceneOutliner.IsValid())
	{
		SceneOutliner->CollapseAll();
	}
}
