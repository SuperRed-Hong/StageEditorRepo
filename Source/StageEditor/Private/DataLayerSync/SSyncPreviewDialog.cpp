// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerSync/SSyncPreviewDialog.h"
#include "DataLayerSync/DataLayerSyncStatus.h"
#include "DataLayerSync/DataLayerSyncStatusCache.h"
#include "DataLayerSync/DataLayerSynchronizer.h"
#include "DataLayerSync/DataLayerImporter.h"
#include "DataLayerSync/StageDataLayerNameParser.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "Actors/Stage.h"

#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "StageEditorSyncPreview"

//----------------------------------------------------------------
// SSyncPreviewDialog Implementation
//----------------------------------------------------------------

void SSyncPreviewDialog::Construct(const FArguments& InArgs)
{
	// Collect sync targets
	CollectSyncTargets();

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
				.Text(LOCTEXT("DialogTitle", "Sync Preview"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
			]

			// No sync needed message
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(FLinearColor(0.2f, 0.6f, 0.2f, 0.3f))
				.Padding(8.0f)
				.Visibility((StageLevelEntries.Num() == 0 && ActLevelEntries.Num() == 0) ? EVisibility::Visible : EVisibility::Collapsed)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 8, 0)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(TEXT("Icons.Check")))
						.ColorAndOpacity(FLinearColor(0.3f, 0.8f, 0.3f))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("NoSyncNeeded", "All DataLayers are in sync. No changes needed."))
						.ColorAndOpacity(FLinearColor(0.5f, 0.8f, 0.5f))
						.AutoWrapText(true)
					]
				]
			]

			// Naming warnings section
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				BuildNamingWarnings()
			]

			// Main content
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0, 0, 0, 12)
			[
				SNew(SBox)
				.MinDesiredHeight(200.0f)
				.MaxDesiredHeight(400.0f)
				[
					BuildContent()
				]
			]

			// Separator
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(SSeparator)
				.Visibility((StageLevelEntries.Num() > 0 || ActLevelEntries.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed)
			]

			// Summary
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(STextBlock)
				.Text(this, &SSyncPreviewDialog::GetSummaryText)
				.Visibility((StageLevelEntries.Num() > 0 || ActLevelEntries.Num() > 0) ? EVisibility::Visible : EVisibility::Collapsed)
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
					.OnClicked(this, &SSyncPreviewDialog::OnCancelClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("SyncButton", "Sync All"))
					.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
					.IsEnabled(this, &SSyncPreviewDialog::IsSyncButtonEnabled)
					.OnClicked(this, &SSyncPreviewDialog::OnSyncClicked)
				]
			]
		]
	];
}

void SSyncPreviewDialog::CollectSyncTargets()
{
	StageLevelEntries.Empty();
	ActLevelEntries.Empty();
	EntriesWithWarnings.Empty();
	bHasCriticalWarning = false;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return;
	}

	UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(World);
	if (!Manager)
	{
		return;
	}

	// Refresh all cache first
	FDataLayerSyncStatusCache::Get().InvalidateAll();

	// Iterate all DataLayers
	Manager->ForEachDataLayerInstance([&](UDataLayerInstance* Instance)
	{
		if (!Instance)
		{
			return true;
		}

		UDataLayerAsset* Asset = const_cast<UDataLayerAsset*>(Instance->GetAsset());
		if (!Asset)
		{
			return true;
		}

		// Get sync status
		FDataLayerSyncStatusInfo StatusInfo = FDataLayerSyncStatusCache::Get().GetCachedStatus(Asset);

		// Only process OutOfSync items
		if (StatusInfo.Status != EDataLayerSyncStatus::OutOfSync)
		{
			return true;
		}

		// Create entry
		TSharedPtr<FSyncPreviewEntry> Entry = MakeShared<FSyncPreviewEntry>();
		Entry->DataLayerAsset = Asset;
		Entry->DataLayerName = Asset->GetName();
		Entry->StatusInfo = StatusInfo;

		// Parse naming
		Entry->ParseResult = FStageDataLayerNameParser::Parse(Asset->GetName());

		// Validate naming
		ValidateNaming(*Entry);

		// Determine if Stage or Act level
		if (Entry->ParseResult.bIsValid && Entry->ParseResult.bIsStageLayer)
		{
			Entry->bIsStageLevel = true;
			StageLevelEntries.Add(Entry);
		}
		else
		{
			Entry->bIsStageLevel = false;
			ActLevelEntries.Add(Entry);
		}

		// Track warnings
		if (Entry->bHasNamingWarning)
		{
			EntriesWithWarnings.Add(Entry);
		}

		return true;
	});
}

void SSyncPreviewDialog::ValidateNaming(FSyncPreviewEntry& Entry)
{
	Entry.bHasNamingWarning = false;
	Entry.NamingWarningMessage.Empty();

	// Check if naming is valid
	if (!Entry.ParseResult.bIsValid)
	{
		Entry.bHasNamingWarning = true;
		Entry.NamingWarningMessage = TEXT("Does not follow naming convention (DL_Stage_* or DL_Act_*)");
		return;
	}

	// For Stage level, check if new child DataLayers have valid naming
	if (Entry.bIsStageLevel && Entry.StatusInfo.NewChildDataLayers.Num() > 0)
	{
		TArray<FString> InvalidNames;

		for (const FString& ChildName : Entry.StatusInfo.NewChildDataLayers)
		{
			FDataLayerNameParseResult ChildParse = FStageDataLayerNameParser::Parse(ChildName);

			if (!ChildParse.bIsValid)
			{
				InvalidNames.Add(ChildName);
			}
			else if (ChildParse.bIsStageLayer)
			{
				// Child should be Act level, not Stage level
				InvalidNames.Add(ChildName + TEXT(" (should be DL_Act_*)"));
			}
			else if (ChildParse.StageName != Entry.ParseResult.StageName)
			{
				// Act's stage name doesn't match parent
				InvalidNames.Add(ChildName + TEXT(" (stage name mismatch)"));
			}
		}

		if (InvalidNames.Num() > 0)
		{
			Entry.bHasNamingWarning = true;
			Entry.NamingWarningMessage = FString::Printf(
				TEXT("New child DataLayers have naming issues: %s"),
				*FString::Join(InvalidNames, TEXT(", ")));
		}
	}
}

TSharedRef<SWidget> SSyncPreviewDialog::BuildContent()
{
	if (StageLevelEntries.Num() == 0 && ActLevelEntries.Num() == 0)
	{
		return SNullWidget::NullWidget;
	}

	TSharedRef<SScrollBox> ScrollBox = SNew(SScrollBox);

	// Stage level section
	if (StageLevelEntries.Num() > 0)
	{
		ScrollBox->AddSlot()
		.Padding(0, 0, 0, 8)
		[
			BuildStageSyncItems()
		];
	}

	// Act level section
	if (ActLevelEntries.Num() > 0)
	{
		ScrollBox->AddSlot()
		.Padding(0, 0, 0, 8)
		[
			BuildActSyncItems()
		];
	}

	return ScrollBox;
}

TSharedRef<SWidget> SSyncPreviewDialog::BuildStageSyncItems()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// Section header
	Box->AddSlot()
	.AutoHeight()
	.Padding(0, 0, 0, 8)
	[
		SNew(STextBlock)
		.Text(FText::Format(LOCTEXT("StageLevelSection", "Stage Level Changes ({0})"), FText::AsNumber(StageLevelEntries.Num())))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
	];

	// Entries
	for (int32 i = 0; i < StageLevelEntries.Num(); ++i)
	{
		Box->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			BuildEntryWidget(*StageLevelEntries[i], i == StageLevelEntries.Num() - 1)
		];
	}

	return Box;
}

TSharedRef<SWidget> SSyncPreviewDialog::BuildActSyncItems()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// Section header
	Box->AddSlot()
	.AutoHeight()
	.Padding(0, 0, 0, 8)
	[
		SNew(STextBlock)
		.Text(FText::Format(LOCTEXT("ActLevelSection", "Act Level Changes ({0})"), FText::AsNumber(ActLevelEntries.Num())))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
	];

	// Entries
	for (int32 i = 0; i < ActLevelEntries.Num(); ++i)
	{
		Box->AddSlot()
		.AutoHeight()
		.Padding(0, 0, 0, 4)
		[
			BuildEntryWidget(*ActLevelEntries[i], i == ActLevelEntries.Num() - 1)
		];
	}

	return Box;
}

TSharedRef<SWidget> SSyncPreviewDialog::BuildEntryWidget(const FSyncPreviewEntry& Entry, bool bIsLast)
{
	FString TreePrefix = bIsLast ? TEXT("\u2514\u2500\u2500 ") : TEXT("\u251C\u2500\u2500 ");

	TSharedRef<SVerticalBox> EntryBox = SNew(SVerticalBox);

	// Entry header
	EntryBox->AddSlot()
	.AutoHeight()
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TreePrefix))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
		]

		// Warning icon if needed
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 4, 0)
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush(TEXT("Icons.Warning")))
			.ColorAndOpacity(FLinearColor(1.0f, 0.8f, 0.0f))
			.DesiredSizeOverride(FVector2D(12, 12))
			.Visibility(Entry.bHasNamingWarning ? EVisibility::Visible : EVisibility::Collapsed)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(FText::FromString(Entry.DataLayerName))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(8, 0, 0, 0)
		[
			SNew(STextBlock)
			.Text(Entry.bIsStageLevel ? LOCTEXT("StageTag", "[Stage]") : LOCTEXT("ActTag", "[Act]"))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.7f, 1.0f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
		]
	];

	// Change details
	FString IndentPrefix = bIsLast ? TEXT("    ") : TEXT("\u2502   ");

	EntryBox->AddSlot()
	.AutoHeight()
	.Padding(0, 2, 0, 0)
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
		.FillWidth(1.0f)
		[
			SNew(STextBlock)
			.Text(GetChangeDetailsText(Entry.StatusInfo))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.AutoWrapText(true)
		]
	];

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(8.0f)
		[
			EntryBox
		];
}

FText SSyncPreviewDialog::GetChangeDetailsText(const FDataLayerSyncStatusInfo& StatusInfo) const
{
	TArray<FString> Details;

	// New children
	if (StatusInfo.NewChildDataLayers.Num() > 0)
	{
		Details.Add(FString::Printf(TEXT("+%d new child(s): %s"),
			StatusInfo.NewChildDataLayers.Num(),
			*FString::Join(StatusInfo.NewChildDataLayers, TEXT(", "))));
	}

	// Removed children
	if (StatusInfo.RemovedChildDataLayers.Num() > 0)
	{
		Details.Add(FString::Printf(TEXT("-%d removed child(s): %s"),
			StatusInfo.RemovedChildDataLayers.Num(),
			*FString::Join(StatusInfo.RemovedChildDataLayers, TEXT(", "))));
	}

	// NotImported children
	if (StatusInfo.NotImportedChildDataLayers.Num() > 0)
	{
		Details.Add(FString::Printf(TEXT("%d unimported child(s): %s"),
			StatusInfo.NotImportedChildDataLayers.Num(),
			*FString::Join(StatusInfo.NotImportedChildDataLayers, TEXT(", "))));
	}

	// Actor changes
	if (StatusInfo.AddedActorCount > 0)
	{
		Details.Add(FString::Printf(TEXT("+%d new actor(s)"), StatusInfo.AddedActorCount));
	}

	if (StatusInfo.RemovedActorCount > 0)
	{
		Details.Add(FString::Printf(TEXT("-%d removed actor(s)"), StatusInfo.RemovedActorCount));
	}

	if (Details.Num() == 0)
	{
		return LOCTEXT("NoDetails", "Changes detected");
	}

	return FText::FromString(FString::Join(Details, TEXT(" | ")));
}

TSharedRef<SWidget> SSyncPreviewDialog::BuildNamingWarnings()
{
	if (EntriesWithWarnings.Num() == 0)
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
			.Text(FText::Format(LOCTEXT("NamingWarningsTitle", "Naming Warnings ({0})"), FText::AsNumber(EntriesWithWarnings.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(FLinearColor(1.0f, 0.8f, 0.0f))
		]
	];

	// Warning items
	for (const TSharedPtr<FSyncPreviewEntry>& Entry : EntriesWithWarnings)
	{
		if (!Entry.IsValid())
		{
			continue;
		}

		WarningBox->AddSlot()
		.AutoHeight()
		.Padding(16, 2, 0, 2)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Entry->DataLayerName))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8, 2, 0, 0)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Entry->NamingWarningMessage))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.6f, 0.4f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
				.AutoWrapText(true)
			]
		];
	}

	// Note
	WarningBox->AddSlot()
	.AutoHeight()
	.Padding(0, 8, 0, 0)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("NamingWarningNote", "DataLayers with naming issues can still be synced, but may not work correctly with StageEditor features."))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
		.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
		.AutoWrapText(true)
	];

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(FLinearColor(0.8f, 0.6f, 0.2f, 0.3f))
		.Padding(8.0f)
		[
			WarningBox
		];
}

FReply SSyncPreviewDialog::OnSyncClicked()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;

	// Execute sync
	FDataLayerBatchSyncResult Result = FDataLayerSynchronizer::SyncAllOutOfSync(World);

	if (Result.SyncedCount > 0 || Result.FailedCount > 0)
	{
		bSyncExecuted = true;
		UE_LOG(LogTemp, Log, TEXT("Sync completed: %d synced, %d failed, %d skipped. Changes: %d Acts, %d Entities"),
			Result.SyncedCount, Result.FailedCount, Result.SkippedCount,
			Result.TotalActChanges, Result.TotalEntityChanges);
	}

	// Close dialog
	if (TSharedPtr<SWindow> Window = OwnerWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

FReply SSyncPreviewDialog::OnCancelClicked()
{
	bSyncExecuted = false;

	if (TSharedPtr<SWindow> Window = OwnerWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

bool SSyncPreviewDialog::IsSyncButtonEnabled() const
{
	// Can sync if there are any items and no critical warnings
	return (StageLevelEntries.Num() > 0 || ActLevelEntries.Num() > 0) && !bHasCriticalWarning;
}

FText SSyncPreviewDialog::GetSummaryText() const
{
	int32 TotalNewChildren = 0;
	int32 TotalRemovedChildren = 0;
	int32 TotalActorChanges = 0;

	for (const auto& Entry : StageLevelEntries)
	{
		if (Entry.IsValid())
		{
			TotalNewChildren += Entry->StatusInfo.NewChildDataLayers.Num();
			TotalRemovedChildren += Entry->StatusInfo.RemovedChildDataLayers.Num();
		}
	}

	for (const auto& Entry : ActLevelEntries)
	{
		if (Entry.IsValid())
		{
			TotalActorChanges += Entry->StatusInfo.AddedActorCount + Entry->StatusInfo.RemovedActorCount;
		}
	}

	return FText::Format(
		LOCTEXT("SummaryFormat", "Total: {0} Stage(s), {1} Act(s) | Changes: +{2}/-{3} child DataLayers, {4} actor changes"),
		FText::AsNumber(StageLevelEntries.Num()),
		FText::AsNumber(ActLevelEntries.Num()),
		FText::AsNumber(TotalNewChildren),
		FText::AsNumber(TotalRemovedChildren),
		FText::AsNumber(TotalActorChanges)
	);
}

bool SSyncPreviewDialog::ShowDialog()
{
	// Create window
	TSharedRef<SWindow> DialogWindow = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Sync DataLayers Preview"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(600, 500))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	// Create dialog widget
	TSharedRef<SSyncPreviewDialog> Dialog = SNew(SSyncPreviewDialog);

	Dialog->OwnerWindow = DialogWindow;

	DialogWindow->SetContent(Dialog);

	// Show modal
	GEditor->EditorAddModalWindow(DialogWindow);

	return Dialog->bSyncExecuted;
}

#undef LOCTEXT_NAMESPACE
