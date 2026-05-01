// Copyright Stage Editor Plugin. All Rights Reserved.

#include "EditorUI/StageEditorTreeItems.h"
#include "Actors/Stage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"

//----------------------------------------------------------------
// Static Type Definitions
//----------------------------------------------------------------

const FSceneOutlinerTreeItemType FStageEditorStageTreeItem::Type(&ISceneOutlinerTreeItem::Type);
const FSceneOutlinerTreeItemType FStageEditorActsFolderTreeItem::Type(&ISceneOutlinerTreeItem::Type);
const FSceneOutlinerTreeItemType FStageEditorEntitiesFolderTreeItem::Type(&ISceneOutlinerTreeItem::Type);
const FSceneOutlinerTreeItemType FStageEditorActTreeItem::Type(&ISceneOutlinerTreeItem::Type);
const FSceneOutlinerTreeItemType FStageEditorEntityTreeItem::Type(&ISceneOutlinerTreeItem::Type);
const FSceneOutlinerTreeItemType FStageEditorEntityUnderActTreeItem::Type(&ISceneOutlinerTreeItem::Type);
const FSceneOutlinerTreeItemType FStageEditorOrphanedFolderTreeItem::Type(&ISceneOutlinerTreeItem::Type);
const FSceneOutlinerTreeItemType FStageEditorOrphanedEntityTreeItem::Type(&ISceneOutlinerTreeItem::Type);

//----------------------------------------------------------------
// 1. FStageEditorStageTreeItem Implementation
//----------------------------------------------------------------

FStageEditorStageTreeItem::FStageEditorStageTreeItem(AStage* InStage)
	: ISceneOutlinerTreeItem(Type)
	, Stage(InStage)
	, CachedID(InStage)
{
}

int32 FStageEditorStageTreeItem::GetStageID() const
{
	if (AStage* StagePtr = Stage.Get())
	{
		return StagePtr->GetStageID();
	}
	return -1;
}

FSceneOutlinerTreeItemID FStageEditorStageTreeItem::GetID() const
{
	return FSceneOutlinerTreeItemID(CachedID);
}

FString FStageEditorStageTreeItem::GetDisplayString() const
{
	if (AStage* StagePtr = Stage.Get())
	{
#if WITH_EDITOR
		return StagePtr->GetActorLabel();
#else
		return StagePtr->GetName();
#endif
	}
	return TEXT("Invalid Stage");
}

TSharedRef<SWidget> FStageEditorStageTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(GetDisplayString()))
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

//----------------------------------------------------------------
// 2. FStageEditorActsFolderTreeItem Implementation
//----------------------------------------------------------------

FStageEditorActsFolderTreeItem::FStageEditorActsFolderTreeItem(AStage* InOwnerStage)
	: ISceneOutlinerTreeItem(Type)
	, OwnerStage(InOwnerStage)
	, OwnerStageID(InOwnerStage ? InOwnerStage->GetStageID() : -1)
{
	CachedIDHash = HashCombine(GetTypeHash(OwnerStageID), GetTypeHash(StageEditorTreeItemIDs::ActsFolderMagic));
}

FSceneOutlinerTreeItemID FStageEditorActsFolderTreeItem::GetID() const
{
	return FSceneOutlinerTreeItemID(CachedIDHash);
}

TSharedRef<SWidget> FStageEditorActsFolderTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Acts")))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}

//----------------------------------------------------------------
// 3. FStageEditorEntitiesFolderTreeItem Implementation
//----------------------------------------------------------------

FStageEditorEntitiesFolderTreeItem::FStageEditorEntitiesFolderTreeItem(AStage* InOwnerStage)
	: ISceneOutlinerTreeItem(Type)
	, OwnerStage(InOwnerStage)
	, OwnerStageID(InOwnerStage ? InOwnerStage->GetStageID() : -1)
{
	CachedIDHash = HashCombine(GetTypeHash(OwnerStageID), GetTypeHash(StageEditorTreeItemIDs::EntitiesFolderMagic));
}

FSceneOutlinerTreeItemID FStageEditorEntitiesFolderTreeItem::GetID() const
{
	return FSceneOutlinerTreeItemID(CachedIDHash);
}

TSharedRef<SWidget> FStageEditorEntitiesFolderTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	// Get entity count for display
	int32 EntityCount = 0;
	if (AStage* StagePtr = OwnerStage.Get())
	{
		EntityCount = StagePtr->EntityRegistry.Num();
	}

	FString DisplayText = FString::Printf(TEXT("Entities (%d)"), EntityCount);

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(DisplayText))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}

//----------------------------------------------------------------
// 4. FStageEditorActTreeItem Implementation
//----------------------------------------------------------------

FStageEditorActTreeItem::FStageEditorActTreeItem(AStage* InOwnerStage, int32 InActIndex)
	: ISceneOutlinerTreeItem(Type)
	, OwnerStage(InOwnerStage)
	, ActIndex(InActIndex)
{
	// Cache SUID for ID generation and validation
	if (InOwnerStage && InOwnerStage->Acts.IsValidIndex(InActIndex))
	{
		CachedActSUID = InOwnerStage->Acts[InActIndex].SUID;
	}
	CachedIDHash = GetTypeHash(CachedActSUID);
}

bool FStageEditorActTreeItem::IsValid() const
{
	return OwnerStage.IsValid() && GetAct() != nullptr;
}

FAct* FStageEditorActTreeItem::GetAct() const
{
	AStage* StagePtr = OwnerStage.Get();
	if (!StagePtr)
	{
		return nullptr;
	}

	// First try direct index access
	if (StagePtr->Acts.IsValidIndex(ActIndex))
	{
		FAct& Act = StagePtr->Acts[ActIndex];
		// Validate SUID matches
		if (Act.SUID == CachedActSUID)
		{
			return &Act;
		}
	}

	// Fallback: search by SUID if index is stale
	return FindActBySUID();
}

FAct* FStageEditorActTreeItem::FindActBySUID() const
{
	AStage* StagePtr = OwnerStage.Get();
	if (!StagePtr)
	{
		return nullptr;
	}

	for (FAct& Act : StagePtr->Acts)
	{
		if (Act.SUID == CachedActSUID)
		{
			return &Act;
		}
	}
	return nullptr;
}

FSceneOutlinerTreeItemID FStageEditorActTreeItem::GetID() const
{
	return FSceneOutlinerTreeItemID(CachedIDHash);
}

FString FStageEditorActTreeItem::GetDisplayString() const
{
	if (FAct* Act = GetAct())
	{
		if (!Act->DisplayName.IsEmpty())
		{
			return Act->DisplayName;
		}
		return FString::Printf(TEXT("Act %d"), Act->SUID.ActID);
	}
	return TEXT("Invalid Act");
}

TSharedRef<SWidget> FStageEditorActTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(GetDisplayString()))
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

//----------------------------------------------------------------
// 5. FStageEditorEntityTreeItem Implementation (Flat)
//----------------------------------------------------------------

FStageEditorEntityTreeItem::FStageEditorEntityTreeItem(AActor* InEntityActor, int32 InEntityID, AStage* InOwnerStage)
	: ISceneOutlinerTreeItem(Type)
	, EntityActor(InEntityActor)
	, EntityID(InEntityID)
	, OwnerStage(InOwnerStage)
	, CachedID(InEntityActor)
{
}

FSceneOutlinerTreeItemID FStageEditorEntityTreeItem::GetID() const
{
	return FSceneOutlinerTreeItemID(CachedID);
}

FString FStageEditorEntityTreeItem::GetDisplayString() const
{
	if (AActor* Actor = EntityActor.Get())
	{
#if WITH_EDITOR
		return Actor->GetActorLabel();
#else
		return Actor->GetName();
#endif
	}
	return TEXT("Invalid Entity");
}

TSharedRef<SWidget> FStageEditorEntityTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(GetDisplayString()))
			.ColorAndOpacity(FSlateColor::UseForeground())
		];
}

//----------------------------------------------------------------
// 6. FStageEditorEntityUnderActTreeItem Implementation
//----------------------------------------------------------------

FStageEditorEntityUnderActTreeItem::FStageEditorEntityUnderActTreeItem(
	AActor* InEntityActor,
	int32 InEntityID,
	AStage* InOwnerStage,
	const FSUID& InActSUID,
	int32 InEntityState)
	: ISceneOutlinerTreeItem(Type)
	, EntityActor(InEntityActor)
	, EntityID(InEntityID)
	, OwnerStage(InOwnerStage)
	, ActSUID(InActSUID)
	, EntityState(InEntityState)
{
	// ID includes Act context to differentiate from flat entity
	CachedIDHash = HashCombine(
		HashCombine(GetTypeHash(InEntityActor), GetTypeHash(InActSUID)),
		GetTypeHash(StageEditorTreeItemIDs::EntityUnderActMagic)
	);
}

FSceneOutlinerTreeItemID FStageEditorEntityUnderActTreeItem::GetID() const
{
	return FSceneOutlinerTreeItemID(CachedIDHash);
}

FString FStageEditorEntityUnderActTreeItem::GetDisplayString() const
{
	if (AActor* Actor = EntityActor.Get())
	{
#if WITH_EDITOR
		return FString::Printf(TEXT("%s (State: %d)"), *Actor->GetActorLabel(), EntityState);
#else
		return FString::Printf(TEXT("%s (State: %d)"), *Actor->GetName(), EntityState);
#endif
	}
	return TEXT("Invalid Entity");
}

TSharedRef<SWidget> FStageEditorEntityUnderActTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	FString ActorName;
	if (AActor* Actor = EntityActor.Get())
	{
#if WITH_EDITOR
		ActorName = Actor->GetActorLabel();
#else
		ActorName = Actor->GetName();
#endif
	}
	else
	{
		ActorName = TEXT("Invalid Entity");
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(ActorName))
			.ColorAndOpacity(FSlateColor::UseForeground())
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 0, 0, 0)
		[
			SNew(STextBlock)
			.Text(FText::Format(NSLOCTEXT("StageEditor", "EntityState", "(State: {0})"), FText::AsNumber(EntityState)))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}

//----------------------------------------------------------------
// 7. FStageEditorOrphanedFolderTreeItem Implementation
//----------------------------------------------------------------

FStageEditorOrphanedFolderTreeItem::FStageEditorOrphanedFolderTreeItem()
	: ISceneOutlinerTreeItem(Type)
	, OrphanedCount(0)
{
	CachedIDHash = GetTypeHash(StageEditorTreeItemIDs::OrphanedFolderMagic);
}

FSceneOutlinerTreeItemID FStageEditorOrphanedFolderTreeItem::GetID() const
{
	return FSceneOutlinerTreeItemID(CachedIDHash);
}

FString FStageEditorOrphanedFolderTreeItem::GetDisplayString() const
{
	return FString::Printf(TEXT("Orphaned Entities (%d)"), OrphanedCount);
}

TSharedRef<SWidget> FStageEditorOrphanedFolderTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(GetDisplayString()))
			.ColorAndOpacity(FLinearColor(1.0f, 0.3f, 0.3f)) // Warning red color
		];
}

//----------------------------------------------------------------
// 8. FStageEditorOrphanedEntityTreeItem Implementation
//----------------------------------------------------------------

FStageEditorOrphanedEntityTreeItem::FStageEditorOrphanedEntityTreeItem(AActor* InEntityActor, int32 InEntityID, const FString& InPreviousStageName)
	: ISceneOutlinerTreeItem(Type)
	, EntityActor(InEntityActor)
	, EntityID(InEntityID)
	, PreviousStageName(InPreviousStageName)
{
	// ID includes orphan magic to differentiate from normal entity
	CachedIDHash = HashCombine(
		GetTypeHash(InEntityActor),
		GetTypeHash(StageEditorTreeItemIDs::OrphanedEntityMagic)
	);
}

FSceneOutlinerTreeItemID FStageEditorOrphanedEntityTreeItem::GetID() const
{
	return FSceneOutlinerTreeItemID(CachedIDHash);
}

FString FStageEditorOrphanedEntityTreeItem::GetDisplayString() const
{
	if (AActor* Actor = EntityActor.Get())
	{
#if WITH_EDITOR
		return FString::Printf(TEXT("%s [Orphaned from: %s]"), *Actor->GetActorLabel(), *PreviousStageName);
#else
		return FString::Printf(TEXT("%s [Orphaned from: %s]"), *Actor->GetName(), *PreviousStageName);
#endif
	}
	return TEXT("Invalid Entity");
}

TSharedRef<SWidget> FStageEditorOrphanedEntityTreeItem::GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow)
{
	FString ActorName;
	if (AActor* Actor = EntityActor.Get())
	{
#if WITH_EDITOR
		ActorName = Actor->GetActorLabel();
#else
		ActorName = Actor->GetName();
#endif
	}
	else
	{
		ActorName = TEXT("Invalid Entity");
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0)
		[
			SNew(STextBlock)
			.Text(FText::FromString(ActorName))
			.ColorAndOpacity(FLinearColor(1.0f, 0.3f, 0.3f)) // Warning red color
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4, 0, 0, 0)
		[
			SNew(STextBlock)
			.Text(FText::Format(NSLOCTEXT("StageEditor", "OrphanedFrom", "[Was: {0}]"), FText::FromString(PreviousStageName)))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}
