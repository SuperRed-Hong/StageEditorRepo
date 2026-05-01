// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorUI/StageEditorDragDropHandler.h"
#include "EditorUI/StageEditorPanel.h"
#include "DataModels/StageTreeItem.h"
#include "EditorLogic/StageEditorController.h"
#include "Actors/Stage.h"
#include "DragAndDrop/ActorDragDropOp.h"
#include "Input/DragAndDrop.h"
#include "DebugHeader.h"

//----------------------------------------------------------------
// Constructor / Destructor
//----------------------------------------------------------------

FStageEditorDragDropHandler::FStageEditorDragDropHandler(
	TWeakPtr<FStageEditorController> InController,
	TWeakPtr<SStageEditorPanel> InPanel)
	: WeakController(InController)
	, WeakPanel(InPanel)
	, DraggedOverItem(nullptr)
{
}

FStageEditorDragDropHandler::~FStageEditorDragDropHandler()
{
	ClearDragState();
}

//----------------------------------------------------------------
// Drag Events
//----------------------------------------------------------------

FReply FStageEditorDragDropHandler::OnRowDragDetected(
	const FGeometry& MyGeometry,
	const FPointerEvent& MouseEvent,
	TSharedPtr<FStageTreeItem> Item)
{
	// Only allow dragging Entities from Registered Entities folder
	if (!Item.IsValid() || Item->Type != EStageTreeItemType::Entity || Item->ID == -1)
	{
		return FReply::Unhandled();
	}

	// Check if parent is EntitiesFolder
	TSharedPtr<FStageTreeItem> Parent = Item->Parent.Pin();
	bool bParentIsEntitiesFolder = Parent.IsValid() && Parent->Type == EStageTreeItemType::EntitiesFolder;

	if (!bParentIsEntitiesFolder)
	{
		return FReply::Unhandled();
	}

	// Get selected items for multi-select drag
	TArray<TSharedPtr<FStageTreeItem>> SelectedItems;
	if (TSharedPtr<SStageEditorPanel> Panel = WeakPanel.Pin())
	{
		if (Panel->StageTreeView.IsValid())
		{
			Panel->StageTreeView->GetSelectedItems(SelectedItems);

			// Filter to only include Entities from Registered Entities folder
			SelectedItems = SelectedItems.FilterByPredicate([](const TSharedPtr<FStageTreeItem>& SelectedItem) {
				if (!SelectedItem.IsValid()) return false;
				if (SelectedItem->Type != EStageTreeItemType::Entity) return false;
				TSharedPtr<FStageTreeItem> ItemParent = SelectedItem->Parent.Pin();
				return ItemParent.IsValid() && ItemParent->Type == EStageTreeItemType::EntitiesFolder;
			});

			// If current item is not in selection, use just current item
			if (!SelectedItems.Contains(Item))
			{
				SelectedItems.Empty();
				SelectedItems.Add(Item);
			}
		}
		else
		{
			SelectedItems.Add(Item);
		}
	}
	else
	{
		SelectedItems.Add(Item);
	}

	if (SelectedItems.Num() > 0)
	{
		// Create drag-drop operation for Entity
		TSharedRef<FEntityDragDropOp> DragDropOp = MakeShareable(new FEntityDragDropOp());
		DragDropOp->EntityItems = SelectedItems;

		// Create drag visual
		FString DragText = SelectedItems.Num() == 1
			? SelectedItems[0]->DisplayName
			: FString::Printf(TEXT("%d Entities"), SelectedItems.Num());

		DragDropOp->DefaultHoverText = FText::FromString(DragText);
		DragDropOp->CurrentHoverText = DragDropOp->DefaultHoverText;

		return FReply::Handled().BeginDragDrop(DragDropOp);
	}

	return FReply::Unhandled();
}

void FStageEditorDragDropHandler::OnRowDragEnter(
	const FDragDropEvent& DragDropEvent,
	TSharedPtr<FStageTreeItem> TargetItem)
{
	// Check if this is an actor drag operation from World Outliner or internal Entity drag
	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	bool bIsActorDrag = Operation.IsValid() && Operation->IsOfType<FActorDragDropOp>();
	bool bIsEntityDrag = Operation.IsValid() && Operation->IsOfType<FEntityDragDropOp>();

	if (!bIsActorDrag && !bIsEntityDrag)
	{
		return;
	}

	// For internal Entity drag (from Registered Entities to Act), highlight the target Act only
	if (bIsEntityDrag)
	{
		// Find the Act item that would receive this drop
		TSharedPtr<FStageTreeItem> ActItem;
		TSharedPtr<FStageTreeItem> Current = TargetItem;
		while (Current.IsValid())
		{
			if (Current->Type == EStageTreeItemType::Act)
			{
				ActItem = Current;
				break;
			}
			Current = Current->Parent.Pin();
		}

		// Only highlight if we found a valid Act target
		DraggedOverItem = ActItem; // Will be nullptr if not over an Act
		return;
	}

	// For Actor drag from World Outliner, highlight the entire Stage
	TSharedPtr<FStageTreeItem> StageItem;
	TSharedPtr<FStageTreeItem> Current = TargetItem;
	while (Current.IsValid())
	{
		if (Current->Type == EStageTreeItemType::Stage)
		{
			StageItem = Current;
			break;
		}
		Current = Current->Parent.Pin();
	}

	// Update the drag target (triggers visual update via TAttribute binding)
	if (StageItem.IsValid())
	{
		DraggedOverItem = StageItem;
	}
}

void FStageEditorDragDropHandler::OnRowDragLeave(
	const FDragDropEvent& DragDropEvent,
	TSharedPtr<FStageTreeItem> TargetItem)
{
	// Note: We don't clear DraggedOverItem here because drag leave might fire
	// when moving between child rows. The drag target should only be cleared
	// when the drag operation ends or enters a different Stage.
	// The OnDrop or drag end will handle cleanup.
}

FReply FStageEditorDragDropHandler::OnRowDrop(
	const FDragDropEvent& DragDropEvent,
	TSharedPtr<FStageTreeItem> TargetItem)
{
	// Clear drag highlight when drop occurs
	DraggedOverItem.Reset();

	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (!Operation.IsValid())
	{
		return FReply::Unhandled();
	}

	// Handle Entity Drag Drop (Internal)
	if (Operation->IsOfType<FEntityDragDropOp>())
	{
		return HandleEntityDrop(DragDropEvent, TargetItem);
	}

	// Handle Actor Drag Drop (From World Outliner)
	if (Operation->IsOfType<FActorDragDropOp>())
	{
		return HandleActorDrop(DragDropEvent, TargetItem);
	}

	return FReply::Unhandled();
}

//----------------------------------------------------------------
// Drag State
//----------------------------------------------------------------

void FStageEditorDragDropHandler::ClearDragState()
{
	DraggedOverItem.Reset();
}

bool FStageEditorDragDropHandler::IsDragTarget(TSharedPtr<FStageTreeItem> Item) const
{
	return IsItemOrDescendantOf(Item, DraggedOverItem);
}

//----------------------------------------------------------------
// Drop Validation
//----------------------------------------------------------------

bool FStageEditorDragDropHandler::CanDropActorsFromOutliner(TSharedPtr<FStageTreeItem> TargetItem) const
{
	// Can drop actors on any Stage or its children
	return FindStageForItem(TargetItem) != nullptr;
}

bool FStageEditorDragDropHandler::CanDropEntityToAct(
	TSharedPtr<FStageTreeItem> SourceItem,
	TSharedPtr<FStageTreeItem> TargetItem) const
{
	if (!SourceItem.IsValid() || !TargetItem.IsValid())
	{
		return false;
	}

	// Source must be a registered Entity
	if (SourceItem->Type != EStageTreeItemType::Entity || SourceItem->ID == -1)
	{
		return false;
	}

	// Target must be an Act or Entity (in which case we use its parent Act)
	int32 TargetActID = FindActIDForItem(TargetItem);
	return TargetActID != -1;
}

//----------------------------------------------------------------
// Drop Handlers
//----------------------------------------------------------------

FReply FStageEditorDragDropHandler::HandleActorDrop(
	const FDragDropEvent& DragDropEvent,
	TSharedPtr<FStageTreeItem> TargetItem)
{
	TSharedPtr<FActorDragDropOp> ActorDragDrop = StaticCastSharedPtr<FActorDragDropOp>(DragDropEvent.GetOperation());
	if (!ActorDragDrop.IsValid())
	{
		return FReply::Unhandled();
	}

	// Find Target Stage from the dropped item
	AStage* TargetStage = FindStageForItem(TargetItem);
	if (!TargetStage)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Drop Failed: Could not determine Target Stage."));
		return FReply::Unhandled();
	}

	// Extract actors from drag operation
	TArray<AActor*> ActorsToRegister;
	for (TWeakObjectPtr<AActor> WeakActor : ActorDragDrop->Actors)
	{
		if (AActor* Actor = WeakActor.Get())
		{
			ActorsToRegister.Add(Actor);
		}
	}

	// Register actors as entities to the specific Stage
	if (ActorsToRegister.Num() > 0)
	{
		if (TSharedPtr<FStageEditorController> Controller = WeakController.Pin())
		{
			if (Controller->RegisterEntities(ActorsToRegister, TargetStage))
			{
				DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Registered %d actors to Stage '%s'"), ActorsToRegister.Num(), *TargetStage->GetActorLabel()));
				RefreshUI();
			}
			else
			{
				DebugHeader::ShowNotifyInfo(TEXT("Registration Failed: No valid actors or already registered."));
			}
		}
	}

	return FReply::Handled();
}

FReply FStageEditorDragDropHandler::HandleEntityDrop(
	const FDragDropEvent& DragDropEvent,
	TSharedPtr<FStageTreeItem> TargetItem)
{
	TSharedPtr<FEntityDragDropOp> EntityDragDrop = StaticCastSharedPtr<FEntityDragDropOp>(DragDropEvent.GetOperation());
	if (!EntityDragDrop.IsValid())
	{
		return FReply::Unhandled();
	}

	// Identify Target Act
	int32 TargetActID = FindActIDForItem(TargetItem);
	if (TargetActID == -1)
	{
		return FReply::Unhandled();
	}

	if (TSharedPtr<FStageEditorController> Controller = WeakController.Pin())
	{
		// Find parent Stage to ensure we are in the correct context
		AStage* TargetStage = FindStageForItem(TargetItem);
		if (TargetStage)
		{
			Controller->SetActiveStage(TargetStage);
		}

		int32 AddedCount = 0;
		for (const TSharedPtr<FStageTreeItem>& EntityItem : EntityDragDrop->EntityItems)
		{
			if (EntityItem.IsValid() && EntityItem->ID != -1)
			{
				// Add entity to Act with default state 0
				// SetEntityStateInAct will add it if not present, or update if present
				if (Controller->SetEntityStateInAct(EntityItem->ID, TargetActID, 0))
				{
					AddedCount++;
				}
			}
		}

		if (AddedCount > 0)
		{
			DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Added %d Entities to Act"), AddedCount));
			RefreshUI();
		}

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

//----------------------------------------------------------------
// Private Helpers
//----------------------------------------------------------------

AStage* FStageEditorDragDropHandler::FindStageForItem(TSharedPtr<FStageTreeItem> Item) const
{
	TSharedPtr<FStageTreeItem> Current = Item;
	while (Current.IsValid())
	{
		if (Current->Type == EStageTreeItemType::Stage && Current->StagePtr.IsValid())
		{
			return Current->StagePtr.Get();
		}
		Current = Current->Parent.Pin();
	}
	return nullptr;
}

int32 FStageEditorDragDropHandler::FindActIDForItem(TSharedPtr<FStageTreeItem> Item) const
{
	if (!Item.IsValid())
	{
		return -1;
	}

	if (Item->Type == EStageTreeItemType::Act)
	{
		return Item->ID;
	}
	else if (Item->Type == EStageTreeItemType::Entity)
	{
		TSharedPtr<FStageTreeItem> Parent = Item->Parent.Pin();
		if (Parent.IsValid() && Parent->Type == EStageTreeItemType::Act)
		{
			return Parent->ID;
		}
	}

	return -1;
}

bool FStageEditorDragDropHandler::IsItemOrDescendantOf(
	TSharedPtr<FStageTreeItem> Item,
	TSharedPtr<FStageTreeItem> Ancestor) const
{
	if (!Item.IsValid() || !Ancestor.IsValid())
	{
		return false;
	}

	if (Item == Ancestor)
	{
		return true;
	}

	// Traverse up the parent chain
	TSharedPtr<FStageTreeItem> Current = Item->Parent.Pin();
	while (Current.IsValid())
	{
		if (Current == Ancestor)
		{
			return true;
		}
		Current = Current->Parent.Pin();
	}

	return false;
}

void FStageEditorDragDropHandler::RefreshUI()
{
	if (TSharedPtr<SStageEditorPanel> Panel = WeakPanel.Pin())
	{
		Panel->RefreshUI();
	}
}
