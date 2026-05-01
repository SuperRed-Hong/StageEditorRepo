// Copyright Stage Editor Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISceneOutlinerColumn.h"
#include "SceneOutlinerFwd.h"

class ISceneOutliner;

// Forward declarations
class FStageEditorController;

/**
 * Column IDs for StageEditor custom columns
 */
struct FStageEditorColumnIDs
{
	static FName Watch() { return FName("StageEditor_Watch"); }
	static FName Name() { return FName("StageEditor_Name"); }
	static FName ID() { return FName("StageEditor_ID"); }
	static FName State() { return FName("StageEditor_State"); }
	static FName Actions() { return FName("StageEditor_Actions"); }
};

/**
 * Name column - displays the display name for each tree item.
 * For Stages: Parsed from DataLayer name (DL_Stage_<Name> -> <Name>)
 * For Acts: Parsed from DataLayer name (DL_Act_<StageName>_<ActName> -> <ActName>)
 * For Entities: Actor label
 * For Folders: Fixed text ("Acts" / "Entities")
 */
class FStageEditorNameColumn : public ISceneOutlinerColumn
{
public:
	FStageEditorNameColumn(ISceneOutliner& SceneOutliner);

	static FName GetID() { return FStageEditorColumnIDs::Name(); }

	//~ Begin ISceneOutlinerColumn Interface
	virtual FName GetColumnID() override { return GetID(); }
	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;
	virtual const TSharedRef<SWidget> ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row) override;
	virtual void PopulateSearchStrings(const ISceneOutlinerTreeItem& Item, TArray<FString>& OutSearchStrings) const override;
	virtual bool SupportsSorting() const override { return true; }
	virtual void SortItems(TArray<FSceneOutlinerTreeItemPtr>& RootItems, const EColumnSortMode::Type SortMode) const override;
	//~ End ISceneOutlinerColumn Interface

private:
	/** Get display name for an item */
	FString GetDisplayName(const ISceneOutlinerTreeItem& Item) const;

	TWeakPtr<ISceneOutliner> WeakSceneOutliner;
};

/**
 * ID column - displays the ID for Stage/Entity items.
 * - Stage: StageID
 * - Entity: EntityID
 * - Others: Empty
 */
class FStageEditorIDColumn : public ISceneOutlinerColumn
{
public:
	FStageEditorIDColumn(ISceneOutliner& SceneOutliner);

	static FName GetID() { return FStageEditorColumnIDs::ID(); }

	//~ Begin ISceneOutlinerColumn Interface
	virtual FName GetColumnID() override { return GetID(); }
	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;
	virtual const TSharedRef<SWidget> ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row) override;
	virtual bool SupportsSorting() const override { return true; }
	virtual void SortItems(TArray<FSceneOutlinerTreeItemPtr>& RootItems, const EColumnSortMode::Type SortMode) const override;
	//~ End ISceneOutlinerColumn Interface

private:
	/** Get ID text for an item */
	FText GetIDText(const ISceneOutlinerTreeItem& Item) const;

	/** Get ID value for sorting */
	int32 GetIDValue(const ISceneOutlinerTreeItem& Item) const;

	TWeakPtr<ISceneOutliner> WeakSceneOutliner;
};

/**
 * State column - displays the EntityState value for Entities under Acts.
 * Only visible for FStageEditorEntityUnderActTreeItem items.
 */
class FStageEditorStateColumn : public ISceneOutlinerColumn
{
public:
	FStageEditorStateColumn(ISceneOutliner& SceneOutliner);

	static FName GetID() { return FStageEditorColumnIDs::State(); }

	//~ Begin ISceneOutlinerColumn Interface
	virtual FName GetColumnID() override { return GetID(); }
	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;
	virtual const TSharedRef<SWidget> ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row) override;
	virtual bool SupportsSorting() const override { return true; }
	virtual void SortItems(TArray<FSceneOutlinerTreeItemPtr>& RootItems, const EColumnSortMode::Type SortMode) const override;
	//~ End ISceneOutlinerColumn Interface

private:
	/** Get state value for an item (-1 if not applicable) */
	int32 GetStateValue(const ISceneOutlinerTreeItem& Item) const;

	TWeakPtr<ISceneOutliner> WeakSceneOutliner;
};

/**
 * Watch column - toggle Debug HUD watch state for Stages.
 * Only shown for Stage items.
 */
class FStageEditorWatchColumn : public ISceneOutlinerColumn
{
public:
	FStageEditorWatchColumn(ISceneOutliner& SceneOutliner);

	static FName GetID() { return FStageEditorColumnIDs::Watch(); }

	//~ Begin ISceneOutlinerColumn Interface
	virtual FName GetColumnID() override { return GetID(); }
	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;
	virtual const TSharedRef<SWidget> ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row) override;
	virtual bool SupportsSorting() const override { return false; }
	//~ End ISceneOutlinerColumn Interface

private:
	TWeakPtr<ISceneOutliner> WeakSceneOutliner;
};

/**
 * Actions column - quick action buttons for items.
 * Stage: Expand/Collapse, Browse BP, Edit BP, Delete
 * ActsFolder: Create Act
 * Entity: Remove from Stage
 */
class FStageEditorActionsColumn : public ISceneOutlinerColumn
{
public:
	FStageEditorActionsColumn(ISceneOutliner& SceneOutliner, TWeakPtr<FStageEditorController> InController);

	static FName GetID() { return FStageEditorColumnIDs::Actions(); }

	//~ Begin ISceneOutlinerColumn Interface
	virtual FName GetColumnID() override { return GetID(); }
	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;
	virtual const TSharedRef<SWidget> ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row) override;
	virtual bool SupportsSorting() const override { return false; }
	//~ End ISceneOutlinerColumn Interface

private:
	TWeakPtr<ISceneOutliner> WeakSceneOutliner;
	TWeakPtr<FStageEditorController> WeakController;
};
