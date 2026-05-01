// Copyright Stage Editor Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class UDataLayerAsset;
class SEditableTextBox;
class SWindow;

/**
 * Quick rename dialog for DataLayer assets.
 *
 * Provides two rename modes:
 * - Rename as Stage: Generates "DL_Stage_{UserInput}"
 * - Rename as Act: Generates "DL_Act_{ParentStageName}_{UserInput}"
 */
class STAGEEDITOR_API SDataLayerQuickRenameDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDataLayerQuickRenameDialog) {}
		SLATE_ARGUMENT(UDataLayerAsset*, DataLayerAsset)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Show the dialog for a DataLayer asset */
	static void ShowDialog(UDataLayerAsset* DataLayerAsset);

private:
	/** Handle rename as Stage button click */
	FReply OnRenameAsStageClicked();

	/** Handle rename as Act button click */
	FReply OnRenameAsActClicked();

	/** Handle cancel button click */
	FReply OnCancelClicked();

	/** Perform the actual rename operation */
	bool ExecuteRename(const FString& NewName);

	/** Get the parent Stage name for Act naming */
	FString GetParentStageName() const;

	/** The DataLayer asset being renamed */
	UDataLayerAsset* DataLayerAsset = nullptr;

	/** Input text box for user to enter the name */
	TSharedPtr<SEditableTextBox> NameInputBox;

	/** Reference to the dialog window */
	TWeakPtr<SWindow> WeakParentWindow;
};
