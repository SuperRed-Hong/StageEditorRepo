// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerSync/SDataLayerImportPreviewDialog.h"
#include "DataLayerSync/DataLayerImporter.h"
#include "DataLayerSync/StageDataLayerNameParser.h"
#include "EditorLogic/StageEditorController.h"
#include "EditorUI/StageEditorPanel.h"
#include "StageEditorModule.h"
#include "Actors/Stage.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Framework/Application/SlateApplication.h"
#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "Kismet2/SClassPickerDialog.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "StageEditorDataLayerImport"

//----------------------------------------------------------------
// SDataLayerImportPreviewDialog Implementation
//----------------------------------------------------------------

void SDataLayerImportPreviewDialog::Construct(const FArguments& InArgs)
{
	DataLayerAsset = InArgs._DataLayerAsset;

	// Generate preview
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	Preview = FDataLayerImporter::GeneratePreview(DataLayerAsset, World);

	// Build Act items list and initialize DefaultAct options
	if (Preview.bIsValid)
	{
		InitializeDefaultActOptions();
	}

	// Parse stage name for display
	FDataLayerNameParseResult ParseResult = FStageDataLayerNameParser::Parse(DataLayerAsset ? DataLayerAsset->GetName() : TEXT(""));
	FString StageName = ParseResult.bIsValid ? ParseResult.StageName : (DataLayerAsset ? DataLayerAsset->GetName() : TEXT("Unknown"));

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
				.Text(FText::Format(LOCTEXT("DialogTitle", "Import Preview: {0}"), FText::FromString(StageName)))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
			]

			// Error message (if invalid)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(FLinearColor(0.8f, 0.2f, 0.2f, 0.3f))
				.Padding(8.0f)
				.Visibility(Preview.bIsValid ? EVisibility::Collapsed : EVisibility::Visible)
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
						.Text(Preview.ErrorMessage)
						.ColorAndOpacity(FLinearColor(1.0f, 0.5f, 0.5f))
						.AutoWrapText(true)
					]
				]
			]

			// Stage info section
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(8.0f)
				.Visibility(Preview.bIsValid ? EVisibility::Visible : EVisibility::Collapsed)
				[
					SNew(SVerticalBox)

					// Stage name
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0, 0, 0, 4)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("StageLabel", "Stage:"))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(8, 0, 0, 0)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString(StageName))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(8, 0, 0, 0)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::Format(LOCTEXT("SourceDataLayer", "(from {0})"), FText::FromString(Preview.SourceDataLayerName)))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
							.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
						]
					]
				]
			]

			// DefaultAct selector (only if has Acts)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				BuildDefaultActSelector()
			]

			// Note: Blueprint class creation moved to Import flow
			// User will be prompted to create Blueprint after clicking Import button

			// Naming warnings (if any)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor(FLinearColor(0.8f, 0.6f, 0.2f, 0.3f))
				.Padding(8.0f)
				.Visibility(Preview.HasNamingWarnings() ? EVisibility::Visible : EVisibility::Collapsed)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
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
							.Text(FText::Format(LOCTEXT("NamingWarningTitle", "Naming Convention Warning ({0} items)"),
								FText::AsNumber(Preview.NamingWarnings.Num())))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
							.ColorAndOpacity(FLinearColor(1.0f, 0.8f, 0.0f))
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BuildWarningContent()
					]
				]
			]

			// "Acts to import" section
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 4)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ActsToImportLabel", "Acts to import:"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.Visibility(Preview.bIsValid && Preview.ActCount > 0 ? EVisibility::Visible : EVisibility::Collapsed)
			]

			// Acts list
			+ SVerticalBox::Slot()
			.AutoHeight()
			.MaxHeight(200.0f)
			.Padding(0, 0, 0, 12)
			[
				BuildActsListContent()
			]

			// No Acts message
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
				.Padding(8.0f)
				.Visibility(Preview.bIsValid && Preview.ActCount == 0 ? EVisibility::Visible : EVisibility::Collapsed)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoChildActs", "No child Act DataLayers found. An empty Default Act will be created."))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
				]
			]

			// Separator
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(SSeparator)
				.Visibility(Preview.bIsValid ? EVisibility::Visible : EVisibility::Collapsed)
			]

			// Summary
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(STextBlock)
				.Text(this, &SDataLayerImportPreviewDialog::GetSummaryText)
				.Visibility(Preview.bIsValid ? EVisibility::Visible : EVisibility::Collapsed)
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
					.OnClicked(this, &SDataLayerImportPreviewDialog::OnCancelClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("ImportButton", "Import"))
					.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
					.IsEnabled(this, &SDataLayerImportPreviewDialog::IsImportButtonEnabled)
					.OnClicked(this, &SDataLayerImportPreviewDialog::OnImportClicked)
				]
			]
		]
	];
}

void SDataLayerImportPreviewDialog::InitializeDefaultActOptions()
{
	DefaultActOptions.Empty();
	ActPreviewItems.Empty();

	// Collect Act items from preview
	int32 ActIndex = 0;
	for (const FDataLayerImportPreviewItem& Item : Preview.Items)
	{
		if (Item.ItemType == TEXT("Act"))
		{
			ActPreviewItems.Add(MakeShared<FDataLayerImportPreviewItem>(Item));

			// Extract just the act name from display name (e.g., "Act: Combat" -> "Combat")
			FString ActName = Item.DisplayName;
			if (ActName.StartsWith(TEXT("Act: ")))
			{
				ActName = ActName.RightChop(5);
			}

			DefaultActOptions.Add(MakeShared<FDefaultActOption>(
				ActName,
				ActIndex,
				Item.DataLayerAsset
			));
			ActIndex++;
		}
	}

	// Select the first Act as default
	if (DefaultActOptions.Num() > 0)
	{
		SelectedDefaultActOption = DefaultActOptions[0];
	}
}

TSharedRef<SWidget> SDataLayerImportPreviewDialog::BuildDefaultActSelector()
{
	if (!Preview.bIsValid || Preview.ActCount == 0)
	{
		return SNullWidget::NullWidget;
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(8.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DefaultActLabel", "Default Act:"))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ToolTipText(LOCTEXT("DefaultActTooltip", "Select which Act will be the Default Act (ID=1). The selected Act's DataLayer will be associated with the Default Act."))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(12, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SAssignNew(DefaultActComboBox, SComboBox<TSharedPtr<FDefaultActOption>>)
				.OptionsSource(&DefaultActOptions)
				.OnSelectionChanged(this, &SDataLayerImportPreviewDialog::OnDefaultActSelectionChanged)
				.OnGenerateWidget(this, &SDataLayerImportPreviewDialog::GenerateDefaultActOptionWidget)
				.InitiallySelectedItem(SelectedDefaultActOption)
				[
					SNew(STextBlock)
					.Text(this, &SDataLayerImportPreviewDialog::GetSelectedDefaultActText)
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(8, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DefaultActHint", "(This Act will have ID=1)"))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			]
		];
}

TSharedRef<SWidget> SDataLayerImportPreviewDialog::GenerateDefaultActOptionWidget(TSharedPtr<FDefaultActOption> Option)
{
	if (!Option.IsValid())
	{
		return SNullWidget::NullWidget;
	}

	return SNew(STextBlock)
		.Text(FText::FromString(Option->DisplayName));
}

void SDataLayerImportPreviewDialog::OnDefaultActSelectionChanged(TSharedPtr<FDefaultActOption> NewSelection, ESelectInfo::Type SelectInfo)
{
	SelectedDefaultActOption = NewSelection;
}

FText SDataLayerImportPreviewDialog::GetSelectedDefaultActText() const
{
	if (SelectedDefaultActOption.IsValid())
	{
		return FText::FromString(SelectedDefaultActOption->DisplayName);
	}
	return LOCTEXT("NoSelection", "(Select...)");
}

TSharedRef<SWidget> SDataLayerImportPreviewDialog::BuildActsListContent()
{
	if (!Preview.bIsValid || ActPreviewItems.Num() == 0)
	{
		return SNullWidget::NullWidget;
	}

	TSharedRef<SScrollBox> ScrollBox = SNew(SScrollBox);

	for (int32 i = 0; i < ActPreviewItems.Num(); ++i)
	{
		TSharedPtr<FDataLayerImportPreviewItem> Item = ActPreviewItems[i];
		if (!Item.IsValid())
		{
			continue;
		}

		// Extract act name
		FString ActName = Item->DisplayName;
		if (ActName.StartsWith(TEXT("Act: ")))
		{
			ActName = ActName.RightChop(5);
		}

		// Get DataLayer name
		FString DataLayerName = Item->DataLayerAsset ? Item->DataLayerAsset->GetName() : TEXT("Unknown");

		// Build tree-like prefix
		bool bIsLast = (i == ActPreviewItems.Num() - 1);
		FString TreePrefix = bIsLast ? TEXT("\u2514\u2500\u2500 ") : TEXT("\u251C\u2500\u2500 "); // └── or ├──

		ScrollBox->AddSlot()
		.Padding(8, 2, 8, 2)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TreePrefix))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(ActName))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8, 0, 0, 0)
			[
				SNew(STextBlock)
				.Text(FText::Format(LOCTEXT("ActDataLayerSource", "({0})"), FText::FromString(DataLayerName)))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			]
		];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(4.0f)
		[
			ScrollBox
		];
}

TSharedRef<SWidget> SDataLayerImportPreviewDialog::BuildWarningContent()
{
	if (!Preview.HasNamingWarnings())
	{
		return SNullWidget::NullWidget;
	}

	TSharedRef<SVerticalBox> WarningBox = SNew(SVerticalBox);

	for (const FDataLayerNamingWarning& Warning : Preview.NamingWarnings)
	{
		WarningBox->AddSlot()
			.AutoHeight()
			.Padding(0, 2, 0, 2)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Warning.DataLayerName))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(12, 0, 0, 0)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Warning.WarningReason))
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
				]
			];
	}

	// Add note at the end
	WarningBox->AddSlot()
		.AutoHeight()
		.Padding(0, 8, 0, 0)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("WarningNote", "These DataLayers will still be imported. Consider renaming them after import."))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.6f, 0.6f)))
			.AutoWrapText(true)
		];

	return WarningBox;
}

FReply SDataLayerImportPreviewDialog::OnImportClicked()
{
	// Step 1: Prompt user to create Stage Blueprint
	UE_LOG(LogStageEditor, Log, TEXT("Import: Prompting user to create Stage Blueprint"));

	// Get Controller to access CreateStageBlueprint method
	if (!FStageEditorModule::IsAvailable())
	{
		UE_LOG(LogStageEditor, Error, TEXT("Import failed: StageEditor module not available"));
		return FReply::Handled();
	}

	TSharedPtr<FStageEditorController> Controller = FStageEditorModule::Get().GetController();
	if (!Controller.IsValid())
	{
		UE_LOG(LogStageEditor, Error, TEXT("Import failed: Controller not available"));
		return FReply::Handled();
	}

	// Load default parent class and folder path from FAssetCreationSettings default constructor
	UClass* DefaultParentClass = nullptr;
	FAssetCreationSettings DefaultSettings;

	// Get the folder path for Stage Blueprint creation
	FString StageBlueprintFolderPath = DefaultSettings.StageAssetFolderPath.Path;
	UE_LOG(LogStageEditor, Log, TEXT("Import: Using Stage Blueprint folder path: %s"), *StageBlueprintFolderPath);

	// Check if path is not null (don't use IsValid() as it only returns true for already-loaded assets)
	if (!DefaultSettings.DefaultStageBlueprintParentClass.IsNull())
	{
		DefaultParentClass = DefaultSettings.DefaultStageBlueprintParentClass.LoadSynchronous();
		UE_LOG(LogStageEditor, Log, TEXT("Import: Loaded DefaultStageBlueprintParentClass: %s"),
			DefaultParentClass ? *DefaultParentClass->GetName() : TEXT("nullptr (failed to load)"));
	}
	else
	{
		UE_LOG(LogStageEditor, Warning, TEXT("Import: DefaultStageBlueprintParentClass is null"));
	}

	// Trigger Blueprint creation dialog (with default parent class and folder path from settings)
	UBlueprint* CreatedBlueprint = Controller->CreateStageBlueprint(StageBlueprintFolderPath, DefaultParentClass);

	if (!CreatedBlueprint)
	{
		// User cancelled Blueprint creation - abort import
		UE_LOG(LogStageEditor, Warning, TEXT("Import cancelled: User did not create Stage Blueprint"));
		return FReply::Handled();
	}

	// Step 2: Get the Blueprint's generated class
	TSubclassOf<AStage> NewBlueprintClass = Cast<UClass>(CreatedBlueprint->GeneratedClass);
	if (!NewBlueprintClass)
	{
		UE_LOG(LogStageEditor, Error, TEXT("Import failed: Created Blueprint has no generated class"));
		return FReply::Handled();
	}

	UE_LOG(LogStageEditor, Log, TEXT("Import: Using newly created Blueprint: %s"), *CreatedBlueprint->GetName());

	// Step 3: Build import params with DefaultAct selection and new Blueprint class
	FDataLayerImportParams Params;
	Params.StageDataLayerAsset = DataLayerAsset;
	Params.SelectedDefaultActIndex = SelectedDefaultActOption.IsValid() ? SelectedDefaultActOption->ChildDataLayerIndex : 0;
	Params.StageBlueprintClass = NewBlueprintClass;

	// Step 4: Execute import
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	FDataLayerImportResult Result = FDataLayerImporter::ExecuteImport(Params, World);

	if (Result.bSuccess)
	{
		bImportExecuted = true;

		UE_LOG(LogStageEditor, Log, TEXT("Import successful: Created Stage '%s' with %d Acts and %d Entities"),
			Result.CreatedStage ? *Result.CreatedStage->StageName : TEXT("Unknown"),
			Result.CreatedActCount,
			Result.RegisteredEntityCount);

		// Close dialog
		if (TSharedPtr<SWindow> Window = OwnerWindow.Pin())
		{
			Window->RequestDestroyWindow();
		}
	}
	else
	{
		// Show error (could use notification or message box)
		UE_LOG(LogStageEditor, Error, TEXT("Import failed: %s"), *Result.ErrorMessage.ToString());
	}

	return FReply::Handled();
}

FReply SDataLayerImportPreviewDialog::OnCancelClicked()
{
	bImportExecuted = false;

	if (TSharedPtr<SWindow> Window = OwnerWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}

	return FReply::Handled();
}

bool SDataLayerImportPreviewDialog::IsImportButtonEnabled() const
{
	// Import button enabled only if preview is valid
	// Blueprint class will be created during import flow
	return Preview.bIsValid;
}

FText SDataLayerImportPreviewDialog::GetSummaryText() const
{
	return FText::Format(
		LOCTEXT("SummaryFormat", "Total: {0} Stage, {1} Acts"),
		FText::AsNumber(Preview.StageCount),
		FText::AsNumber(Preview.ActCount)
	);
}

bool SDataLayerImportPreviewDialog::ShowDialog(UDataLayerAsset* DataLayerAsset)
{
	// Create window
	TSharedRef<SWindow> DialogWindow = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Import DataLayer as Stage"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(500, 500))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	// Create dialog widget
	TSharedRef<SDataLayerImportPreviewDialog> Dialog = SNew(SDataLayerImportPreviewDialog)
		.DataLayerAsset(DataLayerAsset);

	Dialog->OwnerWindow = DialogWindow;

	DialogWindow->SetContent(Dialog);

	// Show modal
	GEditor->EditorAddModalWindow(DialogWindow);

	return Dialog->bImportExecuted;
}

#undef LOCTEXT_NAMESPACE
