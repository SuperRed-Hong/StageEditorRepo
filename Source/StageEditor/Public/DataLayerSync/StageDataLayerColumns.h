// Copyright Stage Editor Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISceneOutliner.h"
#include "ISceneOutlinerColumn.h"
#include "SceneOutlinerFwd.h"
#include "SceneOutlinerGutter.h"
#include "Widgets/Views/SHeaderRow.h"

class SWidget;
class UDataLayerAsset;
class UDataLayerInstance;
template<typename ItemType> class STableRow;

/**
 * Column showing the sync status of a DataLayer with Stage/Act.
 *
 * Displays status icons:
 * - Green checkmark: Synced (DataLayer matches Stage/Act)
 * - Yellow warning: OutOfSync (DataLayer differs from Stage/Act)
 * - Blue plus: Unmanaged (DataLayer not imported yet)
 * - Gray dash: NotApplicable (DataLayer naming doesn't match convention)
 */
class STAGEEDITOR_API FStageDataLayerSyncStatusColumn : public ISceneOutlinerColumn
{
public:
	FStageDataLayerSyncStatusColumn(ISceneOutliner& SceneOutliner);
	virtual ~FStageDataLayerSyncStatusColumn() {}

	/** Get the static column ID */
	static FName GetID();

	//~ Begin ISceneOutlinerColumn Interface
	virtual FName GetColumnID() override { return GetID(); }
	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;
	virtual const TSharedRef<SWidget> ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row) override;
	virtual bool SupportsSorting() const override { return true; }
	virtual void SortItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems, const EColumnSortMode::Type SortMode) const override;
	//~ End ISceneOutlinerColumn Interface

private:
	TWeakPtr<ISceneOutliner> WeakSceneOutliner;
};

/**
 * Column with action buttons for DataLayer operations.
 *
 * Shows contextual buttons based on sync status:
 * - Unmanaged: "Import" button
 * - OutOfSync: "Sync" button
 * - Synced: No button (or subtle indicator)
 */
class STAGEEDITOR_API FStageDataLayerActionsColumn : public ISceneOutlinerColumn
{
public:
	FStageDataLayerActionsColumn(ISceneOutliner& SceneOutliner);
	virtual ~FStageDataLayerActionsColumn() {}

	/** Get the static column ID */
	static FName GetID();

	//~ Begin ISceneOutlinerColumn Interface
	virtual FName GetColumnID() override { return GetID(); }
	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;
	virtual const TSharedRef<SWidget> ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row) override;
	virtual bool SupportsSorting() const override { return false; }
	//~ End ISceneOutlinerColumn Interface

private:
	/** Handle Import button click */
	FReply OnImportClicked(UDataLayerAsset* DataLayerAsset, UDataLayerInstance* DataLayerInstance);

	/** Handle Sync button click */
	FReply OnSyncClicked(UDataLayerAsset* DataLayerAsset);

	/** Handle Std Rename button click */
	FReply OnStdRenameClicked(UDataLayerAsset* DataLayerAsset, UDataLayerInstance* DataLayerInstance);

	TWeakPtr<ISceneOutliner> WeakSceneOutliner;
};

/**
 * Column showing the visibility state of a DataLayer (eye icon).
 *
 * Inherits from FSceneOutlinerGutter for consistent gutter behavior.
 * Uses SVisibilityWidget for the eye icon toggle.
 */
class STAGEEDITOR_API FStageDataLayerVisibilityColumn : public FSceneOutlinerGutter
{
public:
	FStageDataLayerVisibilityColumn(ISceneOutliner& SceneOutliner);
	virtual ~FStageDataLayerVisibilityColumn() {}

	/** Get the static column ID */
	static FName GetID();

	//~ Begin ISceneOutlinerColumn Interface
	virtual FName GetColumnID() override { return GetID(); }
	virtual const TSharedRef<SWidget> ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row) override;
	virtual bool SupportsSorting() const override { return false; }
	//~ End ISceneOutlinerColumn Interface
};

/**
 * Column showing the loaded-in-editor state of a DataLayer (checkbox).
 *
 * Displays a checkbox that toggles whether the DataLayer is loaded in editor.
 * Supports multi-selection toggle.
 */
class STAGEEDITOR_API FStageDataLayerLoadedInEditorColumn : public ISceneOutlinerColumn
{
public:
	FStageDataLayerLoadedInEditorColumn(ISceneOutliner& SceneOutliner);
	virtual ~FStageDataLayerLoadedInEditorColumn() {}

	/** Get the static column ID */
	static FName GetID();

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
 * Column showing the SUID (Stage Unique ID) of a DataLayer.
 *
 * Displays the SUID format:
 * - "S:X" for Stage-level DataLayers
 * - "A:X.Y" for Act-level DataLayers
 * - "-" for DataLayers not yet imported
 */
class STAGEEDITOR_API FStageDataLayerSUIDColumn : public ISceneOutlinerColumn
{
public:
	FStageDataLayerSUIDColumn(ISceneOutliner& SceneOutliner);
	virtual ~FStageDataLayerSUIDColumn() {}

	/** Get the static column ID */
	static FName GetID();

	//~ Begin ISceneOutlinerColumn Interface
	virtual FName GetColumnID() override { return GetID(); }
	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;
	virtual const TSharedRef<SWidget> ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row) override;
	virtual bool SupportsSorting() const override { return true; }
	virtual void SortItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems, const EColumnSortMode::Type SortMode) const override;
	//~ End ISceneOutlinerColumn Interface

private:
	/** Get SUID display string for a DataLayer */
	FString GetSUIDDisplay(const class UDataLayerAsset* DataLayerAsset) const;

	TWeakPtr<ISceneOutliner> WeakSceneOutliner;
};
