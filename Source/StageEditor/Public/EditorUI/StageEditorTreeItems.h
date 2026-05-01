// Copyright Stage Editor Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISceneOutlinerTreeItem.h"
#include "SceneOutlinerFwd.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "UObject/ObjectKey.h"
#include "UObject/WeakObjectPtr.h"
#include "Core/StageCoreTypes.h"

class AStage;
class AActor;
class ISceneOutliner;
class SWidget;
template <typename ItemType> class STableRow;

//----------------------------------------------------------------
// Forward Declarations
//----------------------------------------------------------------

struct FStageEditorStageTreeItem;
struct FStageEditorActsFolderTreeItem;
struct FStageEditorEntitiesFolderTreeItem;
struct FStageEditorActTreeItem;
struct FStageEditorEntityTreeItem;

//----------------------------------------------------------------
// Magic Numbers for Folder Type IDs
//----------------------------------------------------------------

namespace StageEditorTreeItemIDs
{
	/** Magic number for ActsFolder ID generation */
	constexpr uint64 ActsFolderMagic = 0x5354414745414354;  // "STAGEACT"

	/** Magic number for EntitiesFolder ID generation */
	constexpr uint64 EntitiesFolderMagic = 0x53544147454E5459;  // "STAGEENTY"

	/** Magic number for Entity under Act (to differentiate from flat entity) */
	constexpr uint64 EntityUnderActMagic = 0x454E5459554E4441;  // "ENTYUNDA"

	/** Magic number for OrphanedFolder ID generation */
	constexpr uint64 OrphanedFolderMagic = 0x4F525048414E4544;  // "ORPHANED"

	/** Magic number for Orphaned Entity (to differentiate from normal entity) */
	constexpr uint64 OrphanedEntityMagic = 0x4F525048454E5459;  // "ORPHENTY"
}

//----------------------------------------------------------------
// 1. Stage TreeItem
//----------------------------------------------------------------

/**
 * Tree item representing a Stage Actor in the StageEditor SceneOutliner.
 * Wraps an AStage* and provides display/interaction logic.
 */
struct STAGEEDITOR_API FStageEditorStageTreeItem : ISceneOutlinerTreeItem
{
public:
	/** Static type identifier */
	static const FSceneOutlinerTreeItemType Type;

	/** Constructor */
	explicit FStageEditorStageTreeItem(AStage* InStage);

	/** Get the underlying Stage actor */
	AStage* GetStage() const { return Stage.Get(); }

	/** Get the StageID */
	int32 GetStageID() const;

	//----------------------------------------------------------------
	// ISceneOutlinerTreeItem Interface
	//----------------------------------------------------------------

	virtual bool IsValid() const override { return Stage.IsValid(); }
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override { return true; }
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;

private:
	/** Weak pointer to the Stage actor */
	TWeakObjectPtr<AStage> Stage;

	/** Cached object key for ID */
	FObjectKey CachedID;
};

//----------------------------------------------------------------
// 2. ActsFolder TreeItem (Virtual)
//----------------------------------------------------------------

/**
 * Virtual tree item representing the "Acts" folder under a Stage.
 * No backing data - purely organizational.
 */
struct STAGEEDITOR_API FStageEditorActsFolderTreeItem : ISceneOutlinerTreeItem
{
public:
	/** Static type identifier */
	static const FSceneOutlinerTreeItemType Type;

	/** Constructor */
	explicit FStageEditorActsFolderTreeItem(AStage* InOwnerStage);

	/** Get the owner Stage */
	AStage* GetOwnerStage() const { return OwnerStage.Get(); }

	/** Get the owner StageID */
	int32 GetOwnerStageID() const { return OwnerStageID; }

	//----------------------------------------------------------------
	// ISceneOutlinerTreeItem Interface
	//----------------------------------------------------------------

	virtual bool IsValid() const override { return OwnerStage.IsValid(); }
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override { return TEXT("Acts"); }
	virtual bool CanInteract() const override { return true; }
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;

private:
	/** Weak pointer to the owner Stage */
	TWeakObjectPtr<AStage> OwnerStage;

	/** Cached StageID for ID generation (survives Stage pointer invalidation) */
	int32 OwnerStageID;

	/** Cached hash for ID */
	uint64 CachedIDHash;
};

//----------------------------------------------------------------
// 3. EntitiesFolder TreeItem (Virtual)
//----------------------------------------------------------------

/**
 * Virtual tree item representing the "Entities" folder under a Stage.
 * No backing data - purely organizational.
 */
struct STAGEEDITOR_API FStageEditorEntitiesFolderTreeItem : ISceneOutlinerTreeItem
{
public:
	/** Static type identifier */
	static const FSceneOutlinerTreeItemType Type;

	/** Constructor */
	explicit FStageEditorEntitiesFolderTreeItem(AStage* InOwnerStage);

	/** Get the owner Stage */
	AStage* GetOwnerStage() const { return OwnerStage.Get(); }

	/** Get the owner StageID */
	int32 GetOwnerStageID() const { return OwnerStageID; }

	//----------------------------------------------------------------
	// ISceneOutlinerTreeItem Interface
	//----------------------------------------------------------------

	virtual bool IsValid() const override { return OwnerStage.IsValid(); }
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override { return TEXT("Entities"); }
	virtual bool CanInteract() const override { return true; }
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;

private:
	/** Weak pointer to the owner Stage */
	TWeakObjectPtr<AStage> OwnerStage;

	/** Cached StageID for ID generation */
	int32 OwnerStageID;

	/** Cached hash for ID */
	uint64 CachedIDHash;
};

//----------------------------------------------------------------
// 4. Act TreeItem
//----------------------------------------------------------------

/**
 * Tree item representing an Act (FAct struct) in the StageEditor SceneOutliner.
 * Since FAct is a USTRUCT stored in TArray, we reference it via Stage + Index + SUID.
 */
struct STAGEEDITOR_API FStageEditorActTreeItem : ISceneOutlinerTreeItem
{
public:
	/** Static type identifier */
	static const FSceneOutlinerTreeItemType Type;

	/** Constructor */
	FStageEditorActTreeItem(AStage* InOwnerStage, int32 InActIndex);

	/** Get the owner Stage */
	AStage* GetOwnerStage() const { return OwnerStage.Get(); }

	/** Get the Act data (may return nullptr if invalid) */
	FAct* GetAct() const;

	/** Get the cached SUID */
	const FSUID& GetActSUID() const { return CachedActSUID; }

	/** Get the Act index in Stage->Acts array */
	int32 GetActIndex() const { return ActIndex; }

	//----------------------------------------------------------------
	// ISceneOutlinerTreeItem Interface
	//----------------------------------------------------------------

	virtual bool IsValid() const override;
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override { return true; }
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;

private:
	/** Weak pointer to the owner Stage */
	TWeakObjectPtr<AStage> OwnerStage;

	/** Index in Stage->Acts array */
	int32 ActIndex;

	/** Cached SUID for ID generation and validation */
	FSUID CachedActSUID;

	/** Cached hash for ID */
	uint64 CachedIDHash;

	/** Internal helper to find Act by SUID if index is stale */
	FAct* FindActBySUID() const;
};

//----------------------------------------------------------------
// 5. Entity TreeItem (Flat - under EntitiesFolder)
//----------------------------------------------------------------

/**
 * Tree item representing an Entity (Actor with StageEntityComponent) in flat list.
 * Appears under EntitiesFolder as a simple list.
 */
struct STAGEEDITOR_API FStageEditorEntityTreeItem : ISceneOutlinerTreeItem
{
public:
	/** Static type identifier */
	static const FSceneOutlinerTreeItemType Type;

	/** Constructor */
	FStageEditorEntityTreeItem(AActor* InEntityActor, int32 InEntityID, AStage* InOwnerStage);

	/** Get the Entity actor */
	AActor* GetEntityActor() const { return EntityActor.Get(); }

	/** Get the EntityID */
	int32 GetEntityID() const { return EntityID; }

	/** Get the owner Stage */
	AStage* GetOwnerStage() const { return OwnerStage.Get(); }

	//----------------------------------------------------------------
	// ISceneOutlinerTreeItem Interface
	//----------------------------------------------------------------

	virtual bool IsValid() const override { return EntityActor.IsValid(); }
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override { return true; }
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;

private:
	/** Weak pointer to the Entity actor */
	TWeakObjectPtr<AActor> EntityActor;

	/** EntityID in the Stage's registry */
	int32 EntityID;

	/** Weak pointer to the owner Stage */
	TWeakObjectPtr<AStage> OwnerStage;

	/** Cached object key for ID */
	FObjectKey CachedID;
};

//----------------------------------------------------------------
// 6. Entity Under Act TreeItem (with State)
//----------------------------------------------------------------

/**
 * Tree item representing an Entity under an Act node.
 * Displays the Entity's state value for this Act.
 * Different from FStageEditorEntityTreeItem in that it has Act context.
 */
struct STAGEEDITOR_API FStageEditorEntityUnderActTreeItem : ISceneOutlinerTreeItem
{
public:
	/** Static type identifier */
	static const FSceneOutlinerTreeItemType Type;

	/** Constructor */
	FStageEditorEntityUnderActTreeItem(AActor* InEntityActor, int32 InEntityID, AStage* InOwnerStage, const FSUID& InActSUID, int32 InEntityState);

	/** Get the Entity actor */
	AActor* GetEntityActor() const { return EntityActor.Get(); }

	/** Get the EntityID */
	int32 GetEntityID() const { return EntityID; }

	/** Get the owner Stage */
	AStage* GetOwnerStage() const { return OwnerStage.Get(); }

	/** Get the parent Act's SUID */
	const FSUID& GetActSUID() const { return ActSUID; }

	/** Get the Entity state value for this Act */
	int32 GetEntityState() const { return EntityState; }

	/** Set the Entity state value */
	void SetEntityState(int32 NewState) { EntityState = NewState; }

	//----------------------------------------------------------------
	// ISceneOutlinerTreeItem Interface
	//----------------------------------------------------------------

	virtual bool IsValid() const override { return EntityActor.IsValid(); }
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override { return true; }
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;

private:
	/** Weak pointer to the Entity actor */
	TWeakObjectPtr<AActor> EntityActor;

	/** EntityID in the Stage's registry */
	int32 EntityID;

	/** Weak pointer to the owner Stage */
	TWeakObjectPtr<AStage> OwnerStage;

	/** Parent Act's SUID (for context differentiation) */
	FSUID ActSUID;

	/** Entity state value for this Act */
	int32 EntityState;

	/** Cached hash for ID (includes Act context) */
	uint64 CachedIDHash;
};

//----------------------------------------------------------------
// 7. Orphaned Entities Folder TreeItem (Virtual Root)
//----------------------------------------------------------------

/**
 * Virtual tree item representing the "Orphaned Entities" folder.
 * This is the root node shown when orphan filter is enabled.
 * Groups all entities whose OwnerStage was deleted.
 */
struct STAGEEDITOR_API FStageEditorOrphanedFolderTreeItem : ISceneOutlinerTreeItem
{
public:
	/** Static type identifier */
	static const FSceneOutlinerTreeItemType Type;

	/** Constructor */
	FStageEditorOrphanedFolderTreeItem();

	/** Get the orphaned entity count */
	int32 GetOrphanedCount() const { return OrphanedCount; }

	/** Set the orphaned entity count */
	void SetOrphanedCount(int32 Count) { OrphanedCount = Count; }

	//----------------------------------------------------------------
	// ISceneOutlinerTreeItem Interface
	//----------------------------------------------------------------

	virtual bool IsValid() const override { return true; }
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override { return true; }
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;

private:
	/** Cached hash for ID */
	uint64 CachedIDHash;

	/** Count of orphaned entities (for display) */
	int32 OrphanedCount = 0;
};

//----------------------------------------------------------------
// 8. Orphaned Entity TreeItem
//----------------------------------------------------------------

/**
 * Tree item representing an orphaned Entity (Entity whose OwnerStage was deleted).
 * Shows the Entity with warning indicator and allows re-registration.
 */
struct STAGEEDITOR_API FStageEditorOrphanedEntityTreeItem : ISceneOutlinerTreeItem
{
public:
	/** Static type identifier */
	static const FSceneOutlinerTreeItemType Type;

	/** Constructor */
	FStageEditorOrphanedEntityTreeItem(AActor* InEntityActor, int32 InEntityID, const FString& InPreviousStageName);

	/** Get the Entity actor */
	AActor* GetEntityActor() const { return EntityActor.Get(); }

	/** Get the EntityID (from when it was registered) */
	int32 GetEntityID() const { return EntityID; }

	/** Get the name of the Stage that was deleted */
	const FString& GetPreviousStageName() const { return PreviousStageName; }

	//----------------------------------------------------------------
	// ISceneOutlinerTreeItem Interface
	//----------------------------------------------------------------

	virtual bool IsValid() const override { return EntityActor.IsValid(); }
	virtual FSceneOutlinerTreeItemID GetID() const override;
	virtual FString GetDisplayString() const override;
	virtual bool CanInteract() const override { return true; }
	virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FSceneOutlinerTreeItemPtr>& InRow) override;

private:
	/** Weak pointer to the Entity actor */
	TWeakObjectPtr<AActor> EntityActor;

	/** EntityID that was assigned when registered */
	int32 EntityID;

	/** Name of the Stage that was deleted (for display) */
	FString PreviousStageName;

	/** Cached hash for ID */
	uint64 CachedIDHash;
};
