// Copyright Stage Editor Plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UDataLayerAsset;
class UDataLayerInstance;
class SEditableTextBox;
class SWindow;

/**
 * Simplified popup for standard DataLayer renaming.
 *
 * Displays a text input with pre-filled prefix:
 * - Stage (no parent): "DL_Stage_"
 * - Act (has parent): "DL_Act_"
 *
 * User completes the name and confirms with Enter or OK button.
 */
class STAGEEDITOR_API SStdRenamePopup : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SStdRenamePopup) {}
		SLATE_ARGUMENT(UDataLayerAsset*, DataLayerAsset)
		SLATE_ARGUMENT(UDataLayerInstance*, DataLayerInstance)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Show the popup dialog for a DataLayer.
	 * @param DataLayerAsset The asset to rename
	 * @param DataLayerInstance The instance (used to check parent hierarchy)
	 */
	static void ShowPopup(UDataLayerAsset* DataLayerAsset, UDataLayerInstance* DataLayerInstance);

	/**
	 * Check if a DataLayerInstance is a Stage (top-level, no parent).
	 */
	static bool IsStageDataLayer(const UDataLayerInstance* Instance);

private:
	/** Execute the rename operation */
	bool ExecuteRename();

	/** Handle OK button click */
	FReply OnOKClicked();

	/** Handle Cancel button click */
	FReply OnCancelClicked();

	/** Handle text committed (Enter key) */
	void OnTextCommitted(const FText& Text, ETextCommit::Type CommitType);

	/** Close the popup window */
	void ClosePopup();

	/** The DataLayer asset being renamed */
	UDataLayerAsset* DataLayerAsset = nullptr;

	/** The DataLayer instance (for hierarchy check) */
	UDataLayerInstance* DataLayerInstance = nullptr;

	/** Whether this is a Stage (true) or Act (false) */
	bool bIsStage = true;

	/** The name prefix (DL_Stage_ or DL_Act_) */
	FString NamePrefix;

	/** Input text box */
	TSharedPtr<SEditableTextBox> NameInputBox;

	/** Weak reference to parent window */
	TWeakPtr<SWindow> WeakParentWindow;
};
