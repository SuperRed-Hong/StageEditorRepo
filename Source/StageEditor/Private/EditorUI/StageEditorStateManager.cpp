// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorUI/StageEditorStateManager.h"
#include "EditorLogic/StageEditorController.h"
#include "Actors/Stage.h"
#include "DataModels/StageTreeItem.h"
#include "Subsystems/StageEditorSubsystem.h"
#include "Data/StageRegistryAsset.h"
#include "Widgets/Views/STreeView.h"
#include "Logging/StageEditorLog.h"
#include "Editor.h"

//----------------------------------------------------------------
// Constructor / Destructor
//----------------------------------------------------------------

FStageEditorStateManager::FStageEditorStateManager(TWeakPtr<FStageEditorController> InController)
	: WeakController(InController)
	, CachedRegistry(nullptr)
	, CachedLockStatusText(FText::GetEmpty())
	, CachedSyncStatusText(FText::GetEmpty())
	, CachedSyncWarningVisibility(EVisibility::Collapsed)
	, CachedLockStatusBarVisibility(EVisibility::Collapsed)
{
}

FStageEditorStateManager::~FStageEditorStateManager()
{
	// Clear caches
	CachedRegistry.Reset();
}

//----------------------------------------------------------------
// Registry Management
//----------------------------------------------------------------

UStageRegistryAsset* FStageEditorStateManager::GetCachedRegistry() const
{
	// Check if cached Registry is still valid
	if (CachedRegistry.IsValid())
	{
		return CachedRegistry.Get();
	}

	// Cache miss or stale - reload
	if (!GEditor)
	{
		return nullptr;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return nullptr;
	}

	UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (!EditorSubsystem)
	{
		return nullptr;
	}

	UStageRegistryAsset* Registry = EditorSubsystem->GetOrLoadRegistryAsset(World);
	CachedRegistry = Registry; // Update cache
	return Registry;
}

bool FStageEditorStateManager::HasRegistryAsset() const
{
	if (!GEditor)
	{
		return false;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return false;
	}

	// Use StageEditorSubsystem to check for Registry existence (lightweight, no log spam)
	if (UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>())
	{
		return EditorSubsystem->DoesRegistryExist(World);
	}

	return false;
}

void FStageEditorStateManager::InvalidateAllCaches()
{
	// Clear Registry cache (forces reload on next access)
	CachedRegistry.Reset();

	// Clear status text caches
	CachedLockStatusText = FText::GetEmpty();
	CachedSyncStatusText = FText::GetEmpty();
	CachedSyncWarningVisibility = EVisibility::Collapsed;
	CachedLockStatusBarVisibility = EVisibility::Collapsed;

	UE_LOG(LogStageEditor, Log, TEXT("InvalidateAllCaches: All caches invalidated (map changed)"));
}

FString FStageEditorStateManager::GetRegistryAssetPath() const
{
	UStageRegistryAsset* Registry = GetCachedRegistry();
	if (!Registry)
	{
		return TEXT("No Registry");
	}

	// Get package name (e.g., "/Game/Maps/Level1_StageRegistry")
	FString PackageName = Registry->GetPackage()->GetName();

	// Remove "/Script" prefix if present
	if (PackageName.StartsWith(TEXT("/Script/")))
	{
		PackageName = PackageName.RightChop(8);  // Remove "/Script/"
	}

	return PackageName;
}

//----------------------------------------------------------------
// Status Text Caching (Performance Fix)
//----------------------------------------------------------------

void FStageEditorStateManager::RefreshLockStatusText()
{
	if (!GEditor)
	{
		CachedLockStatusText = FText::GetEmpty();
		CachedLockStatusBarVisibility = EVisibility::Collapsed;
		return;
	}

	UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (!EditorSubsystem)
	{
		CachedLockStatusText = FText::GetEmpty();
		CachedLockStatusBarVisibility = EVisibility::Collapsed;
		return;
	}

	UStageRegistryAsset* Registry = GetCachedRegistry();
	if (!Registry)
	{
		CachedLockStatusText = FText::GetEmpty();
		CachedLockStatusBarVisibility = EVisibility::Collapsed;
		return;
	}

	// Update lock status bar visibility based on Registry mode
	CachedLockStatusBarVisibility = (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
		? EVisibility::Visible
		: EVisibility::Collapsed;

	// Query lock info from Subsystem (Phase 18.1: Use cached version for UI performance)
	FRegistryLockInfo LockInfo = EditorSubsystem->GetCachedRegistryLockInfo(Registry);
	CachedLockStatusText = LockInfo.GetDisplayText();
}

void FStageEditorStateManager::RefreshSyncStatusText()
{
	if (!GEditor)
	{
		CachedSyncStatusText = FText::GetEmpty();
		CachedSyncWarningVisibility = EVisibility::Collapsed;
		return;
	}

	TSharedPtr<FStageEditorController> Controller = WeakController.Pin();
	if (!Controller.IsValid())
	{
		CachedSyncStatusText = FText::GetEmpty();
		CachedSyncWarningVisibility = EVisibility::Collapsed;
		return;
	}

	UStageRegistryAsset* Registry = GetCachedRegistry();
	if (!Registry)
	{
		CachedSyncStatusText = FText::GetEmpty();
		CachedSyncWarningVisibility = EVisibility::Collapsed;
		return;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		CachedSyncStatusText = FText::GetEmpty();
		CachedSyncWarningVisibility = EVisibility::Collapsed;
		return;
	}

	// Calculate sync status (Phase 18.1: Use cached version for UI performance)
	FRegistrySyncStatus SyncStatus = Controller->GetCachedSyncStatus(World, Registry);
	CachedSyncStatusText = SyncStatus.GetDisplayText();

	// Compute button text and tooltip from the same cached SyncStatus
	int32 ReconcileCount = SyncStatus.GetPendingReconciliationCount();
	if (ReconcileCount > 0)
	{
		CachedSyncButtonText = FText::Format(NSLOCTEXT("StageEditor", "ReconcileStages", "Reconcile {0} Stage(s)"), FText::AsNumber(ReconcileCount));
		CachedSyncButtonTooltip = NSLOCTEXT("StageEditor", "ReconcileStages_Tooltip", "Convert temporary IDs to real IDs for offline-created Stages");
	}
	else
	{
		CachedSyncButtonText = NSLOCTEXT("StageEditor", "SyncRegistry", "Sync Registry");
		CachedSyncButtonTooltip = NSLOCTEXT("StageEditor", "SyncRegistry_Tooltip", "Assign IDs to pending Stages and remove orphaned Registry entries");
	}

	// Update sync warning visibility
	// Only show in Multi mode when there are out-of-sync Stages
	CachedSyncWarningVisibility = (!SyncStatus.IsSynced())
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

void FStageEditorStateManager::RefreshAllStatusTexts()
{
	RefreshLockStatusText();
	RefreshSyncStatusText();
}

//----------------------------------------------------------------
// Expansion State Management
//----------------------------------------------------------------

void FStageEditorStateManager::SaveExpansionState(
	const TArray<TSharedPtr<FStageTreeItem>>& RootItems,
	TSet<FString>& OutExpansionState,
	TSharedPtr<STreeView<TSharedPtr<FStageTreeItem>>> TreeView) const
{
	OutExpansionState.Empty();

	if (!TreeView.IsValid())
	{
		return;
	}

	// Traverse all items to check expansion (iterative with stack)
	TArray<TSharedPtr<FStageTreeItem>> Stack = RootItems;
	while (Stack.Num() > 0)
	{
		TSharedPtr<FStageTreeItem> Current = Stack.Pop();
		if (TreeView->IsItemExpanded(Current))
		{
			OutExpansionState.Add(GetItemHash(Current));
			Stack.Append(Current->Children);
		}
	}
}

void FStageEditorStateManager::RestoreExpansionState(
	const TArray<TSharedPtr<FStageTreeItem>>& RootItems,
	const TSet<FString>& InExpansionState,
	TSharedPtr<STreeView<TSharedPtr<FStageTreeItem>>> TreeView)
{
	if (!TreeView.IsValid())
	{
		return;
	}

	// Traverse tree and restore expansion
	TArray<TSharedPtr<FStageTreeItem>> Stack = RootItems;
	while (Stack.Num() > 0)
	{
		TSharedPtr<FStageTreeItem> Current = Stack.Pop();
		FString Hash = GetItemHash(Current);

		if (InExpansionState.Contains(Hash))
		{
			TreeView->SetItemExpansion(Current, true);
			Stack.Append(Current->Children);
		}
	}
}

FString FStageEditorStateManager::GetItemHash(TSharedPtr<FStageTreeItem> Item) const
{
	if (!Item.IsValid())
	{
		return TEXT("");
	}

	FString Hash = FString::Printf(TEXT("%d_%s"), (int32)Item->Type, *Item->DisplayName);

	if (Item->Parent.IsValid())
	{
		Hash = GetItemHash(Item->Parent.Pin()) + TEXT("/") + Hash;
	}

	return Hash;
}

//----------------------------------------------------------------
// Viewport Selection Sync
//----------------------------------------------------------------

void FStageEditorStateManager::SyncTreeSelectionWithViewport(
	TSharedPtr<STreeView<TSharedPtr<FStageTreeItem>>> TreeView,
	const TArray<TSharedPtr<FStageTreeItem>>& RootItems)
{
	// NOTE: This method requires ActorPathToTreeItem map which is built during tree construction
	// The actual selection sync logic is handled in SStageEditorPanel::HandleViewportSelectionChanged
	// This is a placeholder for future refactoring when selection state is moved here
}

void FStageEditorStateManager::UpdateActorPathMap(
	const TArray<TSharedPtr<FStageTreeItem>>& RootItems,
	TMap<FString, TWeakPtr<FStageTreeItem>>& ActorPathToTreeItem)
{
	RecursivelyUpdateActorPathMap(RootItems, ActorPathToTreeItem);
}

//----------------------------------------------------------------
// Private Helpers
//----------------------------------------------------------------

void FStageEditorStateManager::RecursivelyFindItemByHash(
	const TArray<TSharedPtr<FStageTreeItem>>& Items,
	const FString& Hash,
	TArray<TSharedPtr<FStageTreeItem>>& OutItems) const
{
	for (const TSharedPtr<FStageTreeItem>& Item : Items)
	{
		if (GetItemHash(Item) == Hash)
		{
			OutItems.Add(Item);
		}

		if (Item->Children.Num() > 0)
		{
			RecursivelyFindItemByHash(Item->Children, Hash, OutItems);
		}
	}
}

void FStageEditorStateManager::RecursivelyUpdateActorPathMap(
	const TArray<TSharedPtr<FStageTreeItem>>& Items,
	TMap<FString, TWeakPtr<FStageTreeItem>>& ActorPathToTreeItem)
{
	for (const TSharedPtr<FStageTreeItem>& Item : Items)
	{
		// Only add items with valid actor paths (Stages and Entities)
		if (!Item->ActorPath.IsEmpty())
		{
			ActorPathToTreeItem.Add(Item->ActorPath, Item);
		}

		// Recursively process children
		if (Item->Children.Num() > 0)
		{
			RecursivelyUpdateActorPathMap(Item->Children, ActorPathToTreeItem);
		}
	}
}
