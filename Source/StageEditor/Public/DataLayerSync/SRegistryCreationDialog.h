// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Data/StageRegistryTypes.h"

// Forward declarations
class SWindow;
class UWorld;

/**
 * @brief Registry creation dialog with collaboration mode selection.
 *
 * Allows user to choose between Solo and Multi collaboration modes
 * when creating a new StageRegistryAsset.
 *
 * UI Layout:
 * ```
 * ┌──────────────────────────────────────────┐
 * │ Create Stage Registry                    │
 * ├──────────────────────────────────────────┤
 * │ Choose collaboration mode:               │
 * │                                          │
 * │ ○ Solo (Single Developer)               │
 * │   - No Source Control required          │
 * │   - Registry saved locally              │
 * │                                          │
 * │ ○ Multi (Team Collaboration)            │
 * │   - Requires Source Control enabled     │
 * │   - Registry auto checked-out           │
 * │   [Source Control Status: Enabled ✓]    │
 * │                                          │
 * │           [Cancel]  [Create Registry]   │
 * └──────────────────────────────────────────┘
 * ```
 *
 * Usage:
 * ```cpp
 * ECollaborationMode SelectedMode = ECollaborationMode::Solo;
 * bool bConfirmed = SRegistryCreationDialog::ShowDialog(SelectedMode);
 * if (bConfirmed)
 * {
 *     // Create Registry with SelectedMode
 * }
 * ```
 *
 * @see UStageEditorSubsystem::CreateRegistryAsset()
 * @see ECollaborationMode
 */
class STAGEEDITOR_API SRegistryCreationDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRegistryCreationDialog) {}
	SLATE_END_ARGS()

	/**
	 * Construct the dialog widget.
	 *
	 * @param InArgs - Slate arguments
	 * @param InOutSelectedMode - In/Out parameter for selected collaboration mode
	 */
	void Construct(const FArguments& InArgs, ECollaborationMode* InOutSelectedMode);

	/**
	 * Show creation dialog as modal window.
	 *
	 * Blocks until user clicks Cancel or Create Registry.
	 *
	 * @param OutSelectedMode - Output parameter for selected collaboration mode
	 * @return true if user clicked Create Registry, false if cancelled
	 */
	static bool ShowDialog(ECollaborationMode& OutSelectedMode);

private:
	//----------------------------------------------------------------
	// UI Construction
	//----------------------------------------------------------------

	/**
	 * Build collaboration mode selection section.
	 */
	TSharedRef<SWidget> BuildModeSelectionSection();

	/**
	 * Build Solo mode radio button with description.
	 */
	TSharedRef<SWidget> BuildSoloModeOption();

	/**
	 * Build Multi mode radio button with description.
	 */
	TSharedRef<SWidget> BuildMultiModeOption();

	/**
	 * Build Source Control status indicator.
	 */
	TSharedRef<SWidget> BuildSourceControlStatus();

	/**
	 * Build button row (Cancel/Create).
	 */
	TSharedRef<SWidget> BuildButtonRow();

	//----------------------------------------------------------------
	// Event Handlers
	//----------------------------------------------------------------

	/**
	 * Handle Solo mode radio button click.
	 */
	void OnSoloModeSelected();

	/**
	 * Handle Multi mode radio button click.
	 */
	void OnMultiModeSelected();

	/**
	 * Get checked state for Solo mode radio button.
	 */
	ECheckBoxState GetSoloModeCheckState() const;

	/**
	 * Get checked state for Multi mode radio button.
	 */
	ECheckBoxState GetMultiModeCheckState() const;

	/**
	 * Handle "Create Registry" button click.
	 */
	FReply OnCreateClicked();

	/**
	 * Handle "Cancel" button click.
	 */
	FReply OnCancelClicked();

	/**
	 * Check if "Create Registry" button should be enabled.
	 *
	 * Multi mode requires Source Control to be enabled.
	 */
	bool IsCreateButtonEnabled() const;

	//----------------------------------------------------------------
	// Helpers
	//----------------------------------------------------------------

	/**
	 * Check if Source Control is currently enabled.
	 */
	bool IsSourceControlEnabled() const;

	/**
	 * Get Source Control status text.
	 *
	 * @return "Enabled ✓" or "Disabled ✗"
	 */
	FText GetSourceControlStatusText() const;

	/**
	 * Get Source Control status color.
	 *
	 * @return Green if enabled, Red if disabled
	 */
	FSlateColor GetSourceControlStatusColor() const;

private:
	//----------------------------------------------------------------
	// Data
	//----------------------------------------------------------------

	/** Pointer to output selected mode (set in Construct) */
	ECollaborationMode* SelectedMode = nullptr;

	/** Parent window (for closing) */
	TWeakPtr<SWindow> ParentWindow;

	/** Result flag (set to true if user clicked Create Registry) */
	bool bConfirmed = false;
};
