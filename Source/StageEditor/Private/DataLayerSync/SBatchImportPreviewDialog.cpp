// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerSync/SBatchImportPreviewDialog.h"
#include "DataLayerSync/DataLayerImporter.h"
#include "DataLayerSync/StageDataLayerNameParser.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "StageEditorBatchImport"

//----------------------------------------------------------------
// SBatchImportPreviewDialog Implementation
//----------------------------------------------------------------

void SBatchImportPreviewDialog::Construct(const FArguments& InArgs)
{
	// Process input DataLayers
	ProcessInputDataLayers(InArgs._DataLayerAssets);

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(12.0f)
		[
			SNew(SVerticalBox)

			// Title
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DialogTitle", "Batch Import Preview"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
			]

			// No valid Stages message
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(FLinearColor(0.8f, 0.2f, 0.2f, 0.3f))
				.Padding(8.0f)
				.Visibility(StageEntries.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(TEXT("Icons.Error")))
						.ColorAndOpacity(FLinearColor(1.0f, 0.3f, 0.3f))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NoValidStages", "No valid Stage DataLayers selected for import."))
						.ColorAndOpacity(FLinearColor(1.0f, 0.5f, 0.5f))
						.AutoWrapText(true)
					]
				]
			]

			// Skipped warnings
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				BuildSkippedWarnings()
			]

			// Stage list
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0, 0, 0, 12)
			[
				SNew(SBox)
				.MinDesiredHeight(200.0f)
				.MaxDesiredHeight(500.0f)
				[
					BuildStageListContent()
				]
			]

			// Separator
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(SSeparator)
				.Visibility(StageEntries.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed)
			]

			// Summary
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(STextBlock)
				.Text(this, &SBatchImportPreviewDialog::GetSummaryText)
				.Visibility(StageEntries.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed)
			]

			// Buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 8, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("CancelButton", "Cancel"))
					.OnClicked(this, &SBatchImportPreviewDialog::OnCancelClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("ImportButton", "Import All"))
					.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
					.IsEnabled(this, &SBatchImportPreviewDialog::IsImportButtonEnabled)
					.OnClicked(this, &SBatchImportPreviewDialog::OnImportClicked)
				]
			]
		]
	];
}

void SBatchImportPreviewDialog::ProcessInputDataLayers(const TArray<UDataLayerAsset*>& DataLayerAssets)
{
	StageEntries.Empty();
	SkippedDataLayers.Empty();
	TotalActCount = 0;

	// First pass: collect all Stage DataLayers
	TSet<UDataLayerAsset*> StageAssets;
	for (UDataLayerAsset* Asset : DataLayerAssets)
	{
		if (!Asset)
		{
			continue;
		}

		FDataLayerNameParseResult ParseResult = FStageDataLayerNameParser::Parse(Asset->GetName());
		if (ParseResult.bIsValid && ParseResult.bIsStageLayer)
		{
			// Check if can import
			FText ErrorMessage;
			if (FDataLayerImporter::CanImport(Asset, nullptr, ErrorMessage))
			{
				StageAssets.Add(Asset);
			}
			else
			{
				FSkippedDataLayerInfo Skipped;
				Skipped.DataLayerName = Asset->GetName();
				Skipped.SkipReason = ErrorMessage.ToString();
				SkippedDataLayers.Add(Skipped);
			}
		}
	}

	// Second pass: check Act DataLayers
	for (UDataLayerAsset* Asset : DataLayerAssets)
	{
		if (!Asset)
		{
			continue;
		}

		FDataLayerNameParseResult ParseResult = FStageDataLayerNameParser::Parse(Asset->GetName());

		// Skip Stage DataLayers (already processed)
		if (ParseResult.bIsValid && ParseResult.bIsStageLayer)
		{
			continue;
		}

		// Check if this is an Act DataLayer
		if (ParseResult.bIsValid && !ParseResult.bIsStageLayer)
		{
			// Find parent Stage - check if parent Stage is in our selection
			bool bParentStageSelected = false;
			for (UDataLayerAsset* StageAsset : StageAssets)
			{
				FDataLayerNameParseResult StageParseResult = FStageDataLayerNameParser::Parse(StageAsset->GetName());
				if (StageParseResult.StageName == ParseResult.StageName)
				{
					bParentStageSelected = true;
					break;
				}
			}

			if (!bParentStageSelected)
			{
				// Parent Stage not selected - skip this Act
				FSkippedDataLayerInfo Skipped;
				Skipped.DataLayerName = Asset->GetName();
				Skipped.SkipReason = FString::Printf(TEXT("Parent Stage '%s' not selected"), *ParseResult.StageName);
				SkippedDataLayers.Add(Skipped);
			}
			// If parent Stage is selected, this Act will be imported with the Stage (no action needed)
		}
		else if (!ParseResult.bIsValid)
		{
			// Non-standard naming - skip
			FSkippedDataLayerInfo Skipped;
			Skipped.DataLayerName = Asset->GetName();
			Skipped.SkipReason = TEXT("Does not follow naming convention (DL_Stage_* or DL_Act_*)");
			SkippedDataLayers.Add(Skipped);
		}
	}

	// Generate preview for each Stage
	for (UDataLayerAsset* StageAsset : StageAssets)
	{
		TSharedPtr<FBatchImportStageEntry> Entry = MakeShared<FBatchImportStageEntry>();
		GenerateStagePreview(StageAsset, *Entry);
		StageEntries.Add(Entry);
		TotalActCount += Entry->ActItems.Num();
	}

	// Initialize ComboBox array
	DefaultActComboBoxes.SetNum(StageEntries.Num());
}

void SBatchImportPreviewDialog::GenerateStagePreview(UDataLayerAsset* StageAsset, FBatchImportStageEntry& OutEntry)
{
	OutEntry.StageDataLayerAsset = StageAsset;

	// Parse stage name
	FDataLayerNameParseResult StageParseResult = FStageDataLayerNameParser::Parse(StageAsset->GetName());
	OutEntry.StageName = StageParseResult.StageName;

	// Generate full preview to get Act items and warnings
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	FDataLayerImportPreview Preview = FDataLayerImporter::GeneratePreview(StageAsset, World);

	// Collect Act items
	int32 ActIndex = 0;
	for (const FDataLayerImportPreviewItem& Item : Preview.Items)
	{
		if (Item.ItemType == TEXT("Act"))
		{
			OutEntry.ActItems.Add(MakeShared<FDataLayerImportPreviewItem>(Item));

			// Extract act name for ComboBox option
			FString ActName = Item.DisplayName;
			if (ActName.StartsWith(TEXT("Act: ")))
			{
				ActName = ActName.RightChop(5);
			}
			OutEntry.DefaultActOptions.Add(MakeShared<FString>(ActName));
			ActIndex++;
		}
	}

	// Copy naming warnings
	OutEntry.NamingWarnings = Preview.NamingWarnings;

	// Default selection is first Act (index 0)
	OutEntry.SelectedDefaultActIndex = 0;
}

TSharedRef<SWidget> SBatchImportPreviewDialog::BuildStageListContent()
{
	if (StageEntries.Num() == 0)
	{
		return SNullWidget::NullWidget;
	}

	TSharedRef<SScrollBox> ScrollBox = SNew(SScrollBox);

	for (int32 i = 0; i < StageEntries.Num(); ++i)
	{
		ScrollBox->AddSlot()
		.Padding(0, 0, 0, 8)
		[
			BuildStageEntryWidget(i)
		];
	}

	return ScrollBox;
}

TSharedRef<SWidget> SBatchImportPreviewDialog::BuildStageEntryWidget(int32 StageIndex)
{
	if (!StageEntries.IsValidIndex(StageIndex))
	{
		return SNullWidget::NullWidget;
	}

	TSharedPtr<FBatchImportStageEntry> Entry = StageEntries[StageIndex];
	if (!Entry.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	// Build tree prefix
	bool bIsLastStage = (StageIndex == StageEntries.Num() - 1);
	FString StageTreePrefix = bIsLastStage ? TEXT("\u2514\u2500\u2500 ") : TEXT("\u251C\u2500\u2500 ");

	TSharedRef<SVerticalBox> StageBox = SNew(SVerticalBox);

	// Stage header
	StageBox->AddSlot()
	.AutoHeight()
	.Padding(0, 0, 0, 4)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(FText::FromString(StageTreePrefix))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("StageEntry", "Stage: {0}"), FText::FromString(Entry->StageName)))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		]
	];

	// DefaultAct selector (if has Acts)
	if (Entry->DefaultActOptions.Num() > 0)
	{
		FString IndentPrefix = bIsLastStage ? TEXT("    ") : TEXT("\u2502   ");

		StageBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(IndentPrefix + TEXT("\u251C\u2500\u2500 ")))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DefaultActLabel", "DefaultAct:"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SAssignNew(DefaultActComboBoxes[StageIndex], SComboBox<TSharedPtr<FString>>)
				.OptionsSource(&Entry->DefaultActOptions)
				.OnSelectionChanged_Lambda([this, StageIndex](TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo)
				{
					OnDefaultActSelectionChanged(NewSelection, SelectInfo, StageIndex);
				})
				.OnGenerateWidget(this, &SBatchImportPreviewDialog::GenerateDefaultActOptionWidget)
				.InitiallySelectedItem(Entry->DefaultActOptions.IsValidIndex(Entry->SelectedDefaultActIndex) ? Entry->DefaultActOptions[Entry->SelectedDefaultActIndex] : nullptr)
				[
					SNew(STextBlock)
					.Text(this, &SBatchImportPreviewDialog::GetSelectedDefaultActText, StageIndex)
				]
			]
		];

		// Acts list
		for (int32 ActIdx = 0; ActIdx < Entry->ActItems.Num(); ++ActIdx)
		{
			TSharedPtr<FDataLayerImportPreviewItem> ActItem = Entry->ActItems[ActIdx];
			if (!ActItem.IsValid())
			{
				continue;
			}

			FString ActName = ActItem->DisplayName;
			if (ActName.StartsWith(TEXT("Act: ")))
			{
				ActName = ActName.RightChop(5);
			}

			FString DataLayerName = ActItem->DataLayerAsset ? ActItem->DataLayerAsset->GetName() : TEXT("Unknown");

			bool bIsLastAct = (ActIdx == Entry->ActItems.Num() - 1);
			FString ActTreePrefix = bIsLastAct ? TEXT("\u2514\u2500\u2500 ") : TEXT("\u251C\u2500\u2500 ");

			StageBox->AddSlot()
			.AutoHeight()
			.Padding(0, 0, 0, 2)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(FText::FromString(IndentPrefix + ActTreePrefix))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(FText::FromString(ActName))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(8, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text(FText::Format(LOCTEXT("ActSource", "({0})"), FText::FromString(DataLayerName)))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
				]
			];
		}
	}
	else
	{
		// No Acts message
		FString IndentPrefix = bIsLastStage ? TEXT("    ") : TEXT("\u2502   ");

		StageBox->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 2)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(IndentPrefix + TEXT("\u2514\u2500\u2500 ")))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoActs", "(No child Acts - empty DefaultAct will be created)"))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			]
		];
	}

	// Naming warnings for this Stage
	if (Entry->NamingWarnings.Num() > 0)
	{
		FString IndentPrefix = bIsLastStage ? TEXT("    ") : TEXT("\u2502   ");

		StageBox->AddSlot()
		.AutoHeight()
		.Padding(0, 4, 0, 0)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(IndentPrefix))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(TEXT("Icons.Warning")))
				.ColorAndOpacity(FLinearColor(1.0f, 0.8f, 0.0f))
				.DesiredSizeOverride(FVector2D(12, 12))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("NamingWarnings", "{0} naming warning(s)"), FText::AsNumber(Entry->NamingWarnings.Num())))
				.ColorAndOpacity(FLinearColor(1.0f, 0.8f, 0.0f))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			]
		];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(8.0f)
		[
			StageBox
		];
}

TSharedRef<SWidget> SBatchImportPreviewDialog::BuildSkippedWarnings()
{
	if (SkippedDataLayers.Num() == 0)
	{
		return SNullWidget::NullWidget;
	}

	TSharedRef<SVerticalBox> WarningBox = SNew(SVerticalBox);

	// Header
	WarningBox->AddSlot()
	.AutoHeight()
	.Padding(0, 0, 0, 4)
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 4, 0)
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush(TEXT("Icons.Warning")))
			.ColorAndOpacity(FLinearColor(1.0f, 0.8f, 0.0f))
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("SkippedTitle", "Skipped ({0} items)"), FText::AsNumber(SkippedDataLayers.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(FLinearColor(1.0f, 0.8f, 0.0f))
		]
	];

	// Skipped items
	for (const FSkippedDataLayerInfo& Skipped : SkippedDataLayers)
	{
		WarningBox->AddSlot()
		.AutoHeight()
		.Padding(16, 2, 0, 2)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Skipped.DataLayerName))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("SkipReason", "- {0}"), FText::FromString(Skipped.SkipReason)))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			]
		];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(0.8f, 0.6f, 0.2f, 0.3f))
		.Padding(8.0f)
		[
			WarningBox
		];
}

TSharedRef<SWidget> SBatchImportPreviewDialog::GenerateDefaultActOptionWidget(TSharedPtr<FString> Option)
{
	if (!Option.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	return SNew(STextBlock)
		.Text(FText::FromString(*Option));
}

void SBatchImportPreviewDialog::OnDefaultActSelectionChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo, int32 StageIndex)
{
	if (!StageEntries.IsValidIndex(StageIndex) || !NewSelection.IsValid())
	{
		return;
	}

	TSharedPtr<FBatchImportStageEntry> Entry = StageEntries[StageIndex];
	if (!Entry.IsValid())
	{
		return;
	}

	// Find index of selected option
	for (int32 i = 0; i < Entry->DefaultActOptions.Num(); ++i)
	{
		if (Entry->DefaultActOptions[i].IsValid() && *Entry->DefaultActOptions[i] == *NewSelection)
		{
			Entry->SelectedDefaultActIndex = i;
			break;
		}
	}
}

FText SBatchImportPreviewDialog::GetSelectedDefaultActText(int32 StageIndex) const
{
	if (!StageEntries.IsValidIndex(StageIndex))
	{
		return LOCTEXT("NoSelection", "(Select...)");
	}

	TSharedPtr<FBatchImportStageEntry> Entry = StageEntries[StageIndex];
	if (!Entry.IsValid())
	{
		return LOCTEXT("NoSelection", "(Select...)");
	}

	if (Entry->DefaultActOptions.IsValidIndex(Entry->SelectedDefaultActIndex))
	{
		return FText::FromString(*Entry->DefaultActOptions[Entry->SelectedDefaultActIndex]);
	}

	return LOCTEXT("NoSelection", "(Select...)");
}

FReply SBatchImportPreviewDialog::OnImportClicked()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	int32 SuccessCount = 0;
	int32 FailCount = 0;

	for (TSharedPtr<FBatchImportStageEntry> Entry : StageEntries)
	{
		if (!Entry.IsValid() || !Entry->StageDataLayerAsset.IsValid())
		{
			FailCount++;
			continue;
		}

		// Build import params
		FDataLayerImportParams Params;
		Params.StageDataLayerAsset = Entry->StageDataLayerAsset.Get();
		Params.SelectedDefaultActIndex = Entry->SelectedDefaultActIndex;

		// Execute import
		FDataLayerImportResult Result = FDataLayerImporter::ExecuteImport(Params, World);

		if (Result.bSuccess)
		{
			SuccessCount++;
		}
		else
		{
			FailCount++;
			UE_LOG(LogTemp, Warning, TEXT("Failed to import Stage '%s': %s"),
				*Entry->StageName, *Result.ErrorMessage.ToString());
		}
	}

	if (SuccessCount > 0)
	{
		bImportExecuted = true;
		UE_LOG(LogTemp, Log, TEXT("Batch import completed: %d succeeded, %d failed"), SuccessCount, FailCount);
	}

	// Close dialog
	if (TSharedPtr<SWindow> Window = OwnerWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SBatchImportPreviewDialog::OnCancelClicked()
{
	bImportExecuted = false;

	if (TSharedPtr<SWindow> Window = OwnerWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

bool SBatchImportPreviewDialog::IsImportButtonEnabled() const
{
	return StageEntries.Num() > 0;
}

FText SBatchImportPreviewDialog::GetSummaryText() const
{
	return FText::Format(
		LOCTEXT("SummaryFormat", "Total: {0} Stages, {1} Acts"),
		FText::AsNumber(StageEntries.Num()),
		FText::AsNumber(TotalActCount)
	);
}

bool SBatchImportPreviewDialog::ShowDialog(const TArray<UDataLayerAsset*>& DataLayerAssets)
{
	// Create window
	TSharedRef<SWindow> DialogWindow = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Batch Import DataLayers as Stages"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(550, 600))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	// Create dialog widget
	TSharedRef<SBatchImportPreviewDialog> Dialog = SNew(SBatchImportPreviewDialog)
		.DataLayerAssets(DataLayerAssets);

	Dialog->OwnerWindow = DialogWindow;

	DialogWindow->SetContent(Dialog);

	// Show modal
	GEditor->EditorAddModalWindow(DialogWindow);

	return Dialog->bImportExecuted;
}

#undef LOCTEXT_NAMESPACE
