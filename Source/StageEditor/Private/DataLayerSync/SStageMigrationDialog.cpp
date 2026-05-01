// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerSync/SStageMigrationDialog.h"
#include "DataLayerSync/StageMigrationAnalyzer.h"
#include "DataLayerSync/StageMigrationExecutor.h"
#include "Data/StageRegistryAsset.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "EditorStyleSet.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "SStageMigrationDialog"

//----------------------------------------------------------------
// Construction
//----------------------------------------------------------------

void SStageMigrationDialog::Construct(
	const FArguments& InArgs,
	UWorld* InWorld,
	const FMigrationAnalysisResult& InAnalysis,
	UStageRegistryAsset* InRegistry)
{
	World = InWorld;
	Analysis = InAnalysis;
	Registry = InRegistry;
	bMigrationExecuted = false;

	ChildSlot
	[
		SNew(SBox)
		.MinDesiredWidth(600.0f)
		.MinDesiredHeight(400.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(8.0f)
			[
				SNew(SVerticalBox)

				// Title
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DialogTitle", "Stage Migration Preview"))
					.Font(FAppStyle::GetFontStyle("HeadingLarge"))
				]

				// Summary Section
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 0, 0, 8)
				[
					BuildSummarySection()
				]

				// Details Section (scrollable)
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(0, 0, 0, 8)
				[
					BuildDetailsSection()
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

bool SStageMigrationDialog::ShowDialog(
	UWorld* World,
	const FMigrationAnalysisResult& Analysis,
	UStageRegistryAsset* Registry)
{
	if (!World || !Registry)
	{
		return false;
	}

	// Create dialog widget
	TSharedRef<SStageMigrationDialog> DialogWidget = SNew(SStageMigrationDialog, World, Analysis, Registry);

	// Create window
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("DialogWindowTitle", "Stage Migration"))
		.ClientSize(FVector2D(700, 500))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.SizingRule(ESizingRule::UserSized);

	Window->SetContent(DialogWidget);
	DialogWidget->ParentWindow = Window;

	// Show as modal dialog
	FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow());

	return DialogWidget->bMigrationExecuted;
}

//----------------------------------------------------------------
// UI Construction
//----------------------------------------------------------------

TSharedRef<SWidget> SStageMigrationDialog::BuildSummarySection()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(8.0f)
		[
			SNew(SVerticalBox)

			// Title
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SummaryTitle", "Summary"))
				.Font(FAppStyle::GetFontStyle("HeadingSmall"))
			]

			// Statistics
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Analysis.GetSummary()))
				.AutoWrapText(true)
			]
		];
}

TSharedRef<SWidget> SStageMigrationDialog::BuildDetailsSection()
{
	TSharedRef<SScrollBox> ScrollBox = SNew(SScrollBox);

	// Add header
	ScrollBox->AddSlot()
	.Padding(0, 0, 0, 4)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
		.Padding(4.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("DetailsTitle", "Detailed Analysis"))
			.Font(FAppStyle::GetFontStyle("HeadingSmall"))
		]
	];

	// Add each Stage analysis
	for (const FStageMigrationAnalysis& StageAnalysis : Analysis.StageAnalyses)
	{
		FLinearColor RowColor = FLinearColor::White;
		if (StageAnalysis.Status != EStageMigrationStatus::Valid)
		{
			RowColor = FLinearColor(1.0f, 1.0f, 0.8f, 1.0f); // Light yellow for issues
		}

		ScrollBox->AddSlot()
		.Padding(0, 2)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.BorderBackgroundColor(RowColor)
			.Padding(8.0f)
			[
				SNew(SHorizontalBox)

				// Status Icon
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(SImage)
					.Image(GetStatusIcon(StageAnalysis.Status))
					.ColorAndOpacity(GetStatusColor(StageAnalysis.Status))
				]

				// Stage Name
				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(StageAnalysis.StageName))
					.Font(FAppStyle::GetFontStyle("BoldFont"))
				]

				// ID Change
				+ SHorizontalBox::Slot()
				.FillWidth(0.3f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(
						StageAnalysis.WillChangeID()
						? FString::Printf(TEXT("%d → %d"), StageAnalysis.CurrentStageID, StageAnalysis.NewStageID)
						: FString::Printf(TEXT("ID: %d"), StageAnalysis.CurrentStageID)))
				]

				// Status
				+ SHorizontalBox::Slot()
				.FillWidth(0.4f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(UEnum::GetValueAsString(StageAnalysis.Status)))
						.ColorAndOpacity(GetStatusColor(StageAnalysis.Status))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(StageAnalysis.SuggestedAction))
						.Font(FAppStyle::GetFontStyle("SmallFont"))
						.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f))
					]
				]
			]
		];
	}

	return ScrollBox;
}

TSharedRef<SWidget> SStageMigrationDialog::BuildButtonRow()
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
			.OnClicked(this, &SStageMigrationDialog::OnCancel)
			.ButtonStyle(FAppStyle::Get(), "Button")
		]

		// Execute Button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("ExecuteButton", "Execute Migration"))
			.OnClicked(this, &SStageMigrationDialog::OnExecuteMigration)
			.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
		];
}

//----------------------------------------------------------------
// Event Handlers
//----------------------------------------------------------------

FReply SStageMigrationDialog::OnExecuteMigration()
{
	// Execute migration
	FMigrationExecutionResult Result = FStageMigrationExecutor::ExecuteMigration(World, Analysis, Registry);

	if (Result.IsSuccess())
	{
		// Show success message
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(Result.MigrationReport),
			LOCTEXT("MigrationSuccessTitle", "Migration Successful"));

		bMigrationExecuted = true;

		// Close window
		if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
		{
			Window->RequestDestroyWindow();
		}
	}
	else
	{
		// Show error message
		FMessageDialog::Open(
			EAppMsgType::Ok,
			FText::FromString(Result.ErrorMessage),
			LOCTEXT("MigrationErrorTitle", "Migration Failed"));
	}

	return FReply::Handled();
}

FReply SStageMigrationDialog::OnCancel()
{
	bMigrationExecuted = false;

	// Close window
	if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

//----------------------------------------------------------------
// Helpers
//----------------------------------------------------------------

FSlateColor SStageMigrationDialog::GetStatusColor(EStageMigrationStatus Status) const
{
	switch (Status)
	{
	case EStageMigrationStatus::Valid:
		return FLinearColor::Green;

	case EStageMigrationStatus::Uninitialized:
		return FLinearColor::Yellow;

	case EStageMigrationStatus::Conflict:
		return FLinearColor(1.0f, 0.5f, 0.0f, 1.0f); // Orange

	case EStageMigrationStatus::Corrupted:
		return FLinearColor::Red;

	default:
		return FLinearColor::White;
	}
}

const FSlateBrush* SStageMigrationDialog::GetStatusIcon(EStageMigrationStatus Status) const
{
	switch (Status)
	{
	case EStageMigrationStatus::Valid:
		return FAppStyle::GetBrush("Icons.Check");

	case EStageMigrationStatus::Uninitialized:
		return FAppStyle::GetBrush("Icons.Warning");

	case EStageMigrationStatus::Conflict:
		return FAppStyle::GetBrush("Icons.Error");

	case EStageMigrationStatus::Corrupted:
		return FAppStyle::GetBrush("Icons.Error");

	default:
		return FAppStyle::GetBrush("Icons.Help");
	}
}

#undef LOCTEXT_NAMESPACE
