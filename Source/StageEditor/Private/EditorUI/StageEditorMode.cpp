// Copyright Stage Editor Plugin. All Rights Reserved.

#include "EditorUI/StageEditorMode.h"
#include "EditorUI/StageEditorHierarchy.h"
#include "EditorUI/StageEditorTreeItems.h"
#include "EditorUI/StageEditorOutliner.h"
#include "EditorLogic/StageEditorController.h"
#include "Actors/Stage.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Selection.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ToolMenus.h"
#include "SceneOutlinerDragDrop.h"
#include "DragAndDrop/ActorDragDropOp.h"
#include "DebugHeader.h"

#define LOCTEXT_NAMESPACE "StageEditorMode"

//----------------------------------------------------------------
// FStageEditorModeParams
//----------------------------------------------------------------

FStageEditorModeParams::FStageEditorModeParams(
	SSceneOutliner* InSceneOutliner,
	SStageEditorOutliner* InStageOutliner,
	const TWeakObjectPtr<UWorld>& InSpecifiedWorldToDisplay,
	TWeakPtr<FStageEditorController> InController)
	: SpecifiedWorldToDisplay(InSpecifiedWorldToDisplay)
	, StageOutliner(InStageOutliner)
	, SceneOutliner(InSceneOutliner)
	, Controller(InController)
{
}

//----------------------------------------------------------------
// FStageEditorMode
//----------------------------------------------------------------

FStageEditorMode::FStageEditorMode(const FStageEditorModeParams& Params)
	: ISceneOutlinerMode(Params.SceneOutliner)
	, SpecifiedWorldToDisplay(Params.SpecifiedWorldToDisplay)
	, StageOutliner(Params.StageOutliner)
	, WeakController(Params.Controller)
{
	// Initial build - this creates the Hierarchy which is required for SceneOutliner to function
	Rebuild();
}

FStageEditorMode::~FStageEditorMode()
{
}

void FStageEditorMode::Rebuild()
{
	// Reset counters
	FilteredStageCount = 0;
	FilteredEntityCount = 0;

	// Choose which world to represent
	ChooseRepresentingWorld();

	// Create hierarchy if needed (IMPORTANT: This is required for SceneOutliner to function)
	if (!Hierarchy.IsValid())
	{
		Hierarchy = CreateHierarchy();
	}
}

TUniquePtr<ISceneOutlinerHierarchy> FStageEditorMode::CreateHierarchy()
{
	return FStageEditorHierarchy::Create(this, RepresentingWorld);
}

int32 FStageEditorMode::GetTypeSortPriority(const ISceneOutlinerTreeItem& Item) const
{
	if (Item.IsA<FStageEditorStageTreeItem>())
	{
		return static_cast<int32>(EItemSortOrder::Stage);
	}
	if (Item.IsA<FStageEditorActsFolderTreeItem>())
	{
		return static_cast<int32>(EItemSortOrder::ActsFolder);
	}
	if (Item.IsA<FStageEditorEntitiesFolderTreeItem>())
	{
		return static_cast<int32>(EItemSortOrder::EntitiesFolder);
	}
	if (Item.IsA<FStageEditorActTreeItem>())
	{
		return static_cast<int32>(EItemSortOrder::Act);
	}
	if (Item.IsA<FStageEditorEntityTreeItem>() || Item.IsA<FStageEditorEntityUnderActTreeItem>())
	{
		return static_cast<int32>(EItemSortOrder::Entity);
	}
	return 0;
}

bool FStageEditorMode::CanRenameItem(const ISceneOutlinerTreeItem& Item) const
{
	// Only Stages and Acts can be renamed
	return Item.IsA<FStageEditorStageTreeItem>() || Item.IsA<FStageEditorActTreeItem>();
}

FText FStageEditorMode::GetStatusText() const
{
	return FText::Format(
		LOCTEXT("StatusText", "Stages: {0} | Entities: {1}"),
		FText::AsNumber(FilteredStageCount),
		FText::AsNumber(FilteredEntityCount));
}

void FStageEditorMode::OnItemAdded(FSceneOutlinerTreeItemPtr Item)
{
	if (!Item.IsValid())
	{
		return;
	}

	if (Item->IsA<FStageEditorStageTreeItem>())
	{
		++FilteredStageCount;
	}
	else if (Item->IsA<FStageEditorEntityTreeItem>())
	{
		++FilteredEntityCount;
	}
}

void FStageEditorMode::OnItemRemoved(FSceneOutlinerTreeItemPtr Item)
{
	if (!Item.IsValid())
	{
		return;
	}

	if (Item->IsA<FStageEditorStageTreeItem>())
	{
		--FilteredStageCount;
	}
	else if (Item->IsA<FStageEditorEntityTreeItem>())
	{
		--FilteredEntityCount;
	}
}

void FStageEditorMode::OnItemPassesFilters(const ISceneOutlinerTreeItem& Item)
{
	// Currently unused
}

void FStageEditorMode::OnItemSelectionChanged(
	FSceneOutlinerTreeItemPtr TreeItem,
	ESelectInfo::Type SelectionType,
	const FSceneOutlinerItemSelection& Selection)
{
	if (bUpdatingSelection)
	{
		return;
	}

	// Sync selection to viewport
	TGuardValue<bool> Guard(bUpdatingSelection, true);

	if (GEditor)
	{
		GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true);

		for (const TWeakPtr<ISceneOutlinerTreeItem>& WeakItem : Selection.SelectedItems)
		{
			FSceneOutlinerTreeItemPtr SelectedItem = WeakItem.Pin();
			if (!SelectedItem.IsValid())
			{
				continue;
			}

			AActor* ActorToSelect = nullptr;

			// Stage -> Select Stage Actor
			if (FStageEditorStageTreeItem* StageItem = SelectedItem->CastTo<FStageEditorStageTreeItem>())
			{
				ActorToSelect = StageItem->GetStage();
			}
			// Entity -> Select Entity Actor
			else if (FStageEditorEntityTreeItem* EntityItem = SelectedItem->CastTo<FStageEditorEntityTreeItem>())
			{
				ActorToSelect = EntityItem->GetEntityActor();
			}
			else if (FStageEditorEntityUnderActTreeItem* EntityUnderActItem = SelectedItem->CastTo<FStageEditorEntityUnderActTreeItem>())
			{
				ActorToSelect = EntityUnderActItem->GetEntityActor();
			}

			if (ActorToSelect)
			{
				GEditor->SelectActor(ActorToSelect, /*bInSelected=*/true, /*bNotify=*/false);
			}
		}

		GEditor->NoteSelectionChange();
	}
}

void FStageEditorMode::OnItemDoubleClick(FSceneOutlinerTreeItemPtr Item)
{
	if (!Item.IsValid())
	{
		return;
	}

	AActor* ActorToFocus = nullptr;

	// Stage -> Focus Stage Actor
	if (FStageEditorStageTreeItem* StageItem = Item->CastTo<FStageEditorStageTreeItem>())
	{
		ActorToFocus = StageItem->GetStage();
	}
	// Entity -> Focus Entity Actor
	else if (FStageEditorEntityTreeItem* EntityItem = Item->CastTo<FStageEditorEntityTreeItem>())
	{
		ActorToFocus = EntityItem->GetEntityActor();
	}
	else if (FStageEditorEntityUnderActTreeItem* EntityUnderActItem = Item->CastTo<FStageEditorEntityUnderActTreeItem>())
	{
		ActorToFocus = EntityUnderActItem->GetEntityActor();
	}

	if (ActorToFocus)
	{
		FocusActorInViewport(ActorToFocus);
	}
}

FReply FStageEditorMode::OnKeyDown(const FKeyEvent& InKeyEvent)
{
	// Delete key - delete selected items
	if (InKeyEvent.GetKey() == EKeys::Delete)
	{
		// TODO: Implement delete for selected Stages/Entities
		return FReply::Handled();
	}

	// F key - focus selected item
	if (InKeyEvent.GetKey() == EKeys::F)
	{
		TArray<AActor*> SelectedEntities = GetSelectedEntities();
		if (SelectedEntities.Num() > 0)
		{
			FocusActorInViewport(SelectedEntities[0]);
			return FReply::Handled();
		}

		TArray<AStage*> SelectedStages = GetSelectedStages();
		if (SelectedStages.Num() > 0)
		{
			FocusActorInViewport(SelectedStages[0]);
			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

void FStageEditorMode::SynchronizeSelection()
{
	// Sync viewport selection to tree
	if (bUpdatingSelection || !GEditor)
	{
		return;
	}

	TGuardValue<bool> Guard(bUpdatingSelection, true);

	// Get currently selected actors in viewport
	TArray<AActor*> SelectedActors;
	USelection* Selection = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*Selection); It; ++It)
	{
		if (AActor* Actor = Cast<AActor>(*It))
		{
			SelectedActors.Add(Actor);
		}
	}

	// TODO: Find corresponding tree items and select them
}

TSharedPtr<SWidget> FStageEditorMode::CreateContextMenu()
{
	// Get current selection
	TArray<FSceneOutlinerTreeItemPtr> SelectedItems = SceneOutliner->GetSelectedItems();
	if (SelectedItems.Num() == 0)
	{
		return nullptr;
	}

	FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	// Single selection context menu
	if (SelectedItems.Num() == 1)
	{
		FSceneOutlinerTreeItemPtr Item = SelectedItems[0];

		if (FStageEditorStageTreeItem* StageItem = Item->CastTo<FStageEditorStageTreeItem>())
		{
			BuildStageContextMenu(MenuBuilder, StageItem->GetStage());
		}
		else if (FStageEditorActTreeItem* ActItem = Item->CastTo<FStageEditorActTreeItem>())
		{
			BuildActContextMenu(MenuBuilder, ActItem->GetOwnerStage(), ActItem->GetActIndex());
		}
		else if (FStageEditorEntityTreeItem* EntityItem = Item->CastTo<FStageEditorEntityTreeItem>())
		{
			BuildEntityContextMenu(MenuBuilder, EntityItem->GetOwnerStage(), EntityItem->GetEntityActor(), EntityItem->GetEntityID());
		}
		else if (FStageEditorEntityUnderActTreeItem* EntityUnderActItem = Item->CastTo<FStageEditorEntityUnderActTreeItem>())
		{
			BuildEntityContextMenu(MenuBuilder, EntityUnderActItem->GetOwnerStage(), EntityUnderActItem->GetEntityActor(), EntityUnderActItem->GetEntityID());
		}
	}
	// Multi-selection context menu
	else
	{
		// TODO: Build multi-selection context menu
		MenuBuilder.AddMenuEntry(
			LOCTEXT("MultipleSelection", "Multiple Items Selected"),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction()
		);
	}

	return MenuBuilder.MakeWidget();
}

bool FStageEditorMode::ParseDragDrop(FSceneOutlinerDragDropPayload& OutPayload, const FDragDropOperation& Operation) const
{
	// Check for Actor drag from World Outliner
	if (Operation.IsOfType<FActorDragDropOp>())
	{
		// We handle FActorDragDropOp separately in ValidateDrop/OnDrop
		return true;
	}

	// Check for internal SceneOutliner drag (Entity items)
	if (Operation.IsOfType<FSceneOutlinerDragDropOp>())
	{
		const FSceneOutlinerDragDropOp& OutlinerOp = static_cast<const FSceneOutlinerDragDropOp&>(Operation);
		// FSceneOutlinerDragDropOp contains dragged items in its internal state
		// The payload will be populated by the outliner itself
		return true;
	}

	return false;
}

FSceneOutlinerDragValidationInfo FStageEditorMode::ValidateDrop(
	const ISceneOutlinerTreeItem& DropTarget,
	const FSceneOutlinerDragDropPayload& Payload) const
{
	// Case 1: External Actor drag from World Outliner
	if (Payload.SourceOperation.IsOfType<FActorDragDropOp>())
	{
		// Can drop on Stage, ActsFolder, EntitiesFolder, Act, or Entity under Stage
		AStage* TargetStage = FindStageForItem(DropTarget);
		if (TargetStage)
		{
			return FSceneOutlinerDragValidationInfo(
				ESceneOutlinerDropCompatibility::CompatibleGeneric,
				FText::Format(LOCTEXT("DropActorToStage", "Register to Stage '{0}'"),
					FText::FromString(TargetStage->GetActorLabel())));
		}
		return FSceneOutlinerDragValidationInfo::Invalid();
	}

	// Case 2: Internal Entity drag to Act
	if (Payload.Has<FStageEditorEntityTreeItem>())
	{
		// Can only drop Entity on Act or Entity under Act
		int32 TargetActID = FindActIDForItem(DropTarget);
		if (TargetActID != -1)
		{
			AStage* TargetStage = FindStageForItem(DropTarget);
			if (TargetStage && TargetStage->Acts.IsValidIndex(TargetActID))
			{
				const FAct& Act = TargetStage->Acts[TargetActID];
				return FSceneOutlinerDragValidationInfo(
					ESceneOutlinerDropCompatibility::CompatibleGeneric,
					FText::Format(LOCTEXT("DropEntityToAct", "Add to Act '{0}'"),
						FText::FromString(Act.DisplayName)));
			}
		}
		return FSceneOutlinerDragValidationInfo(
			ESceneOutlinerDropCompatibility::IncompatibleGeneric,
			LOCTEXT("DropEntityToActInvalid", "Can only drop Entity on an Act"));
	}

	return FSceneOutlinerDragValidationInfo::Invalid();
}

void FStageEditorMode::OnDrop(
	ISceneOutlinerTreeItem& DropTarget,
	const FSceneOutlinerDragDropPayload& Payload,
	const FSceneOutlinerDragValidationInfo& ValidationInfo) const
{
	if (!ValidationInfo.IsValid())
	{
		return;
	}

	TSharedPtr<FStageEditorController> Controller = WeakController.Pin();
	if (!Controller.IsValid())
	{
		return;
	}

	// Case 1: External Actor drag from World Outliner
	if (Payload.SourceOperation.IsOfType<FActorDragDropOp>())
	{
		const FActorDragDropOp& ActorOp = static_cast<const FActorDragDropOp&>(Payload.SourceOperation);

		AStage* TargetStage = FindStageForItem(DropTarget);
		if (!TargetStage)
		{
			return;
		}

		// Extract actors from drag operation
		TArray<AActor*> ActorsToRegister;
		for (const TWeakObjectPtr<AActor>& WeakActor : ActorOp.Actors)
		{
			if (AActor* Actor = WeakActor.Get())
			{
				ActorsToRegister.Add(Actor);
			}
		}

		// Register actors to the Stage
		if (ActorsToRegister.Num() > 0)
		{
			if (Controller->RegisterEntities(ActorsToRegister, TargetStage))
			{
				DebugHeader::ShowNotifyInfo(FString::Printf(
					TEXT("Registered %d actor(s) to Stage '%s'"),
					ActorsToRegister.Num(),
					*TargetStage->GetActorLabel()));
			}
		}
		return;
	}

	// Case 2: Internal Entity drag to Act
	if (Payload.Has<FStageEditorEntityTreeItem>())
	{
		int32 TargetActID = FindActIDForItem(DropTarget);
		AStage* TargetStage = FindStageForItem(DropTarget);

		if (TargetActID == -1 || !TargetStage)
		{
			return;
		}

		// Set the active stage first
		Controller->SetActiveStage(TargetStage);

		// Add each Entity to the Act with default state 0
		int32 AddedCount = 0;
		TArray<FStageEditorEntityTreeItem*> EntityItems = Payload.Get<FStageEditorEntityTreeItem>();
		for (FStageEditorEntityTreeItem* EntityItem : EntityItems)
		{
			if (EntityItem && EntityItem->GetEntityID() > 0)
			{
				if (Controller->SetEntityStateInAct(EntityItem->GetEntityID(), TargetActID, 0))
				{
					AddedCount++;
				}
			}
		}

		if (AddedCount > 0)
		{
			DebugHeader::ShowNotifyInfo(FString::Printf(
				TEXT("Added %d Entity(s) to Act"),
				AddedCount));
		}
	}
}

TArray<AStage*> FStageEditorMode::GetSelectedStages() const
{
	TArray<AStage*> Result;
	TArray<FSceneOutlinerTreeItemPtr> SelectedItems = SceneOutliner->GetSelectedItems();

	for (const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
	{
		if (FStageEditorStageTreeItem* StageItem = Item->CastTo<FStageEditorStageTreeItem>())
		{
			if (AStage* Stage = StageItem->GetStage())
			{
				Result.Add(Stage);
			}
		}
	}

	return Result;
}

TArray<AActor*> FStageEditorMode::GetSelectedEntities() const
{
	TArray<AActor*> Result;
	TArray<FSceneOutlinerTreeItemPtr> SelectedItems = SceneOutliner->GetSelectedItems();

	for (const FSceneOutlinerTreeItemPtr& Item : SelectedItems)
	{
		AActor* Actor = nullptr;

		if (FStageEditorEntityTreeItem* EntityItem = Item->CastTo<FStageEditorEntityTreeItem>())
		{
			Actor = EntityItem->GetEntityActor();
		}
		else if (FStageEditorEntityUnderActTreeItem* EntityUnderActItem = Item->CastTo<FStageEditorEntityUnderActTreeItem>())
		{
			Actor = EntityUnderActItem->GetEntityActor();
		}

		if (Actor)
		{
			Result.AddUnique(Actor);
		}
	}

	return Result;
}

void FStageEditorMode::ChooseRepresentingWorld()
{
	if (SpecifiedWorldToDisplay.IsValid())
	{
		RepresentingWorld = SpecifiedWorldToDisplay;
		return;
	}

	// Default to editor world
	if (GEditor)
	{
		RepresentingWorld = GEditor->GetEditorWorldContext().World();
	}
}

UWorld* FStageEditorMode::GetOwningWorld() const
{
	return RepresentingWorld.Get();
}

void FStageEditorMode::FocusActorInViewport(AActor* Actor)
{
	if (Actor && GEditor)
	{
		GEditor->MoveViewportCamerasToActor(*Actor, /*bActiveViewportOnly=*/false);
	}
}

void FStageEditorMode::SelectActorInViewport(AActor* Actor, bool bAddToSelection)
{
	if (Actor && GEditor)
	{
		GEditor->SelectActor(Actor, /*bInSelected=*/true, /*bNotify=*/true, /*bSelectEvenIfHidden=*/false, /*bForceRefresh=*/true);
	}
}

void FStageEditorMode::BuildStageContextMenu(FMenuBuilder& MenuBuilder, AStage* Stage)
{
	if (!Stage)
	{
		return;
	}

	MenuBuilder.BeginSection("StageActions", LOCTEXT("StageActionsHeader", "Stage Actions"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CreateAct", "Create Act"),
			LOCTEXT("CreateActTooltip", "Create a new Act in this Stage"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([Stage]()
				{
					// TODO: Call StageEditorController::CreateAct
				})
			)
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("DeleteStage", "Delete Stage"),
			LOCTEXT("DeleteStageTooltip", "Delete this Stage and unregister all Entities"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([Stage]()
				{
					// TODO: Call StageEditorController::DeleteStage
				})
			)
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("StageNavigation", LOCTEXT("StageNavigationHeader", "Navigation"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("FocusStage", "Focus in Viewport"),
			LOCTEXT("FocusStageTooltip", "Focus the viewport camera on this Stage"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, Stage]()
				{
					FocusActorInViewport(Stage);
				})
			)
		);
	}
	MenuBuilder.EndSection();
}

void FStageEditorMode::BuildActContextMenu(FMenuBuilder& MenuBuilder, AStage* Stage, int32 ActIndex)
{
	if (!Stage || !Stage->Acts.IsValidIndex(ActIndex))
	{
		return;
	}

	FAct& Act = Stage->Acts[ActIndex];

	MenuBuilder.BeginSection("ActActions", LOCTEXT("ActActionsHeader", "Act Actions"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ActivateAct", "Activate Act"),
			LOCTEXT("ActivateActTooltip", "Activate this Act in the Stage"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([Stage, ActIndex]()
				{
					Stage->ActivateAct(Stage->Acts[ActIndex].SUID.ActID);
				})
			)
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("DeleteAct", "Delete Act"),
			LOCTEXT("DeleteActTooltip", "Delete this Act"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([Stage, ActIndex]()
				{
					// TODO: Call StageEditorController::DeleteAct
				}),
				FCanExecuteAction::CreateLambda([ActIndex]()
				{
					// Cannot delete Default Act (index 0)
					return ActIndex > 0;
				})
			)
		);
	}
	MenuBuilder.EndSection();
}

void FStageEditorMode::BuildEntityContextMenu(FMenuBuilder& MenuBuilder, AStage* Stage, AActor* Entity, int32 EntityID)
{
	if (!Stage || !Entity)
	{
		return;
	}

	MenuBuilder.BeginSection("EntityActions", LOCTEXT("EntityActionsHeader", "Entity Actions"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("UnregisterEntity", "Unregister Entity"),
			LOCTEXT("UnregisterEntityTooltip", "Unregister this Entity from the Stage"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([Stage, EntityID]()
				{
					Stage->UnregisterEntity(EntityID);
				})
			)
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("EntityNavigation", LOCTEXT("EntityNavigationHeader", "Navigation"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("FocusEntity", "Focus in Viewport"),
			LOCTEXT("FocusEntityTooltip", "Focus the viewport camera on this Entity"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, Entity]()
				{
					FocusActorInViewport(Entity);
				})
			)
		);
	}
	MenuBuilder.EndSection();
}

//----------------------------------------------------------------
// Drag & Drop Helpers
//----------------------------------------------------------------

AStage* FStageEditorMode::FindStageForItem(const ISceneOutlinerTreeItem& Item) const
{
	// Direct Stage item
	if (const FStageEditorStageTreeItem* StageItem = Item.CastTo<FStageEditorStageTreeItem>())
	{
		return StageItem->GetStage();
	}

	// ActsFolder -> Stage
	if (const FStageEditorActsFolderTreeItem* ActsFolderItem = Item.CastTo<FStageEditorActsFolderTreeItem>())
	{
		return ActsFolderItem->GetOwnerStage();
	}

	// EntitiesFolder -> Stage
	if (const FStageEditorEntitiesFolderTreeItem* EntitiesFolderItem = Item.CastTo<FStageEditorEntitiesFolderTreeItem>())
	{
		return EntitiesFolderItem->GetOwnerStage();
	}

	// Act -> Stage
	if (const FStageEditorActTreeItem* ActItem = Item.CastTo<FStageEditorActTreeItem>())
	{
		return ActItem->GetOwnerStage();
	}

	// Entity -> Stage
	if (const FStageEditorEntityTreeItem* EntityItem = Item.CastTo<FStageEditorEntityTreeItem>())
	{
		return EntityItem->GetOwnerStage();
	}

	// EntityUnderAct -> Stage
	if (const FStageEditorEntityUnderActTreeItem* EntityUnderActItem = Item.CastTo<FStageEditorEntityUnderActTreeItem>())
	{
		return EntityUnderActItem->GetOwnerStage();
	}

	return nullptr;
}

int32 FStageEditorMode::FindActIDForItem(const ISceneOutlinerTreeItem& Item) const
{
	// Direct Act item
	if (const FStageEditorActTreeItem* ActItem = Item.CastTo<FStageEditorActTreeItem>())
	{
		return ActItem->GetActIndex();
	}

	// EntityUnderAct -> Act index
	if (const FStageEditorEntityUnderActTreeItem* EntityUnderActItem = Item.CastTo<FStageEditorEntityUnderActTreeItem>())
	{
		// Need to find the Act index from the SUID
		AStage* Stage = EntityUnderActItem->GetOwnerStage();
		if (Stage)
		{
			const FSUID& ActSUID = EntityUnderActItem->GetActSUID();
			for (int32 i = 0; i < Stage->Acts.Num(); ++i)
			{
				if (Stage->Acts[i].SUID == ActSUID)
				{
					return i;
				}
			}
		}
	}

	return -1;
}

#undef LOCTEXT_NAMESPACE
