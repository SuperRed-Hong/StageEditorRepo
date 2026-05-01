// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerSync/SRegistryCreationDialog.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"

#define LOCTEXT_NAMESPACE "SRegistryCreationDialog"

//----------------------------------------------------------------
// Construction
//----------------------------------------------------------------

void SRegistryCreationDialog::Construct(
	const FArguments& InArgs,
	ECollaborationMode* InOutSelectedMode)
{
	SelectedMode = InOutSelectedMode;
	bConfirmed = false;

	// Default to Solo if not set
	if (!SelectedMode)
	{
		static ECollaborationMode DefaultMode = ECollaborationMode::Solo;
		SelectedMode = &DefaultMode;
	}

	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth(500.0f)
		.MinDesiredHeight(350.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(8.0f)
			[
				SNew(SVerticalBox)

				// Title
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 12)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DialogTitle", "Create Stage Registry"))
					.Font(FAppStyle::GetFontStyle("HeadingLarge"))
				]

				// Instructions
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 16)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Instructions", "Choose collaboration mode:"))
					.Font(FAppStyle::GetFontStyle("NormalText"))
				]

				// Mode Selection Section
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(0, 0, 0, 16)
				[
					BuildModeSelectionSection()
				]

				// Source Control Status
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 16)
				[
					BuildSourceControlStatus()
				]

				// Button Row
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BuildButtonRow()
				]
			]
		]
	];
}

//----------------------------------------------------------------
// Static Show Dialog
//----------------------------------------------------------------

bool SRegistryCreationDialog::ShowDialog(ECollaborationMode& OutSelectedMode)
{
	// Create dialog widget
	TSharedRef<SRegistryCreationDialog> DialogWidget =
		SNew(SRegistryCreationDialog, &OutSelectedMode);

	// Create window
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("DialogWindowTitle", "Create Stage Registry"))
		.ClientSize(FVector2D(550, 400))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.SizingRule(ESizingRule::FixedSize);

	Window->SetContent(DialogWidget);
	DialogWidget->ParentWindow = Window;

	// Show as modal dialog
	FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());

	return DialogWidget->bConfirmed;
}

//----------------------------------------------------------------
// UI Construction
//----------------------------------------------------------------

TSharedRef<SWidget> SRegistryCreationDialog::BuildModeSelectionSection()
{
	return SNew(SVerticalBox)

		// Solo Mode Option
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 0, 0, 16)
		[
			BuildSoloModeOption()
		]

		// Multi Mode Option
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildMultiModeOption()
		];
}

TSharedRef<SWidget> SRegistryCreationDialog::BuildSoloModeOption()
{
	// Wrap entire option in a clickable button for better UX
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")  // Transparent button style
		.OnClicked_Lambda([this]() -> FReply
		{
			OnSoloModeSelected();
			return FReply::Handled();
		})
		.ContentPadding(0)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(12.0f)
			[
				SNew(SVerticalBox)

				// Radio Button + Title
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
				[
					SNew(SHorizontalBox)

					// Radio Button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SNew(SCheckBox)
						.Style(FAppStyle::Get(), "RadioButton")
						.IsChecked(this, &SRegistryCreationDialog::GetSoloModeCheckState)
						.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
						{
							if (NewState == ECheckBoxState::Checked)
							{
								OnSoloModeSelected();
							}
						})
					]

					// Title
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("SoloModeTitle", "Solo (Single Developer)"))
						.Font(FAppStyle::GetFontStyle("BoldFont"))
					]
				]

				// Description
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(24, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SoloModeDesc",
						"• No Source Control required\n"
						"• Registry saved in Level directory\n"
						"• Suitable for single-developer projects"))
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f))
					.AutoWrapText(true)
				]
			]
		];
}

TSharedRef<SWidget> SRegistryCreationDialog::BuildMultiModeOption()
{
	// Wrap entire option in a clickable button for better UX
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")  // Transparent button style
		.OnClicked_Lambda([this]() -> FReply
		{
			if (IsSourceControlEnabled())
			{
				OnMultiModeSelected();
			}
			return FReply::Handled();
		})
		.IsEnabled(this, &SRegistryCreationDialog::IsSourceControlEnabled)
		.ContentPadding(0)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(12.0f)
			[
				SNew(SVerticalBox)

				// Radio Button + Title
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
				[
					SNew(SHorizontalBox)

					// Radio Button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SNew(SCheckBox)
						.Style(FAppStyle::Get(), "RadioButton")
						.IsChecked(this, &SRegistryCreationDialog::GetMultiModeCheckState)
						.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
						{
							if (NewState == ECheckBoxState::Checked)
							{
								OnMultiModeSelected();
							}
						})
						.IsEnabled(this, &SRegistryCreationDialog::IsSourceControlEnabled)
					]

					// Title
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("MultiModeTitle", "Multi (Team Collaboration)"))
						.Font(FAppStyle::GetFontStyle("BoldFont"))
						.ColorAndOpacity_Lambda([this]() -> FSlateColor
						{
							return IsSourceControlEnabled()
								? FSlateColor::UseForeground()
								: FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
						})
					]
				]

				// Description
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(24, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MultiModeDesc",
						"• Requires Source Control enabled\n"
						"• Registry automatically checked out\n"
						"• Supports multi-developer collaboration"))
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f))
					.AutoWrapText(true)
				]
			]
		];
}

TSharedRef<SWidget> SRegistryCreationDialog::BuildSourceControlStatus()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SHorizontalBox)

			// Label
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 8, 0)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SourceControlLabel", "Source Control Status:"))
				.Font(FAppStyle::GetFontStyle("SmallFont"))
			]

			// Status Text
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SRegistryCreationDialog::GetSourceControlStatusText)
				.ColorAndOpacity(this, &SRegistryCreationDialog::GetSourceControlStatusColor)
				.Font(FAppStyle::GetFontStyle("BoldFont"))
			]
		];
}

TSharedRef<SWidget> SRegistryCreationDialog::BuildButtonRow()
{
	return SNew(SHorizontalBox)

		// Spacer
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)

		// Cancel Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0, 0, 8, 0)
		[
			SNew(SButton)
			.Text(LOCTEXT("CancelButton", "Cancel"))
			.OnClicked(this, &SRegistryCreationDialog::OnCancelClicked)
			.ButtonStyle(FAppStyle::Get(), "Button")
		]

		// Create Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("CreateButton", "Create Registry"))
			.OnClicked(this, &SRegistryCreationDialog::OnCreateClicked)
			.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
			.IsEnabled(this, &SRegistryCreationDialog::IsCreateButtonEnabled)
		];
}

//----------------------------------------------------------------
// Event Handlers
//----------------------------------------------------------------

void SRegistryCreationDialog::OnSoloModeSelected()
{
	if (SelectedMode)
	{
		*SelectedMode = ECollaborationMode::Solo;
	}
}

void SRegistryCreationDialog::OnMultiModeSelected()
{
	if (SelectedMode)
	{
		*SelectedMode = ECollaborationMode::Multi;
	}
}

ECheckBoxState SRegistryCreationDialog::GetSoloModeCheckState() const
{
	if (SelectedMode && *SelectedMode == ECollaborationMode::Solo)
	{
		return ECheckBoxState::Checked;
	}
	return ECheckBoxState::Unchecked;
}

ECheckBoxState SRegistryCreationDialog::GetMultiModeCheckState() const
{
	if (SelectedMode && *SelectedMode == ECollaborationMode::Multi)
	{
		return ECheckBoxState::Checked;
	}
	return ECheckBoxState::Unchecked;
}

FReply SRegistryCreationDialog::OnCreateClicked()
{
	bConfirmed = true;

	// Close window
	if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SRegistryCreationDialog::OnCancelClicked()
{
	bConfirmed = false;

	// Close window
	if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

bool SRegistryCreationDialog::IsCreateButtonEnabled() const
{
	// If Multi mode selected, Source Control must be enabled
	if (SelectedMode && *SelectedMode == ECollaborationMode::Multi)
	{
		return IsSourceControlEnabled();
	}

	// Solo mode always enabled
	return true;
}

//----------------------------------------------------------------
// Helpers
//----------------------------------------------------------------

bool SRegistryCreationDialog::IsSourceControlEnabled() const
{
	ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
	return SourceControlModule.IsEnabled() && SourceControlModule.GetProvider().IsAvailable();
}

FText SRegistryCreationDialog::GetSourceControlStatusText() const
{
	if (IsSourceControlEnabled())
	{
		return LOCTEXT("SourceControlEnabled", "Enabled ✓");
	}
	else
	{
		return LOCTEXT("SourceControlDisabled", "Disabled ✗");
	}
}

FSlateColor SRegistryCreationDialog::GetSourceControlStatusColor() const
{
	if (IsSourceControlEnabled())
	{
		return FLinearColor::Green;
	}
	else
	{
		return FLinearColor::Red;
	}
}

#undef LOCTEXT_NAMESPACE
