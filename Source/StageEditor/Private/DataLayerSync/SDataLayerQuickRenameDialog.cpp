// Copyright Stage Editor Plugin. All Rights Reserved.

#include "DataLayerSync/SDataLayerQuickRenameDialog.h"
#include "DataLayerSync/StageDataLayerNameParser.h"

#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "IContentBrowserSingleton.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"

#define LOCTEXT_NAMESPACE "SDataLayerQuickRenameDialog"

void SDataLayerQuickRenameDialog::Construct(const FArguments& InArgs)
{
	DataLayerAsset = InArgs._DataLayerAsset;

	// Parse current name to extract display name
	FString CurrentDisplayName;
	if (DataLayerAsset)
	{
		FDataLayerNameParseResult ParseResult = FStageDataLayerNameParser::Parse(DataLayerAsset->GetName());
		if (ParseResult.bIsValid)
		{
			CurrentDisplayName = ParseResult.bIsStageLayer ? ParseResult.StageName : ParseResult.ActName;
		}
		else
		{
			// Use full name if not parseable
			CurrentDisplayName = DataLayerAsset->GetName();
		}
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(16.f))
		[
			SNew(SVerticalBox)

			// Title
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DialogTitle", "Quick Rename DataLayer"))
				.Font(FAppStyle::GetFontStyle("HeadingExtraSmall"))
			]

			// Current name display
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CurrentNameLabel", "Current: "))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(DataLayerAsset ? DataLayerAsset->GetName() : TEXT("")))
					.ColorAndOpacity(FLinearColor::Gray)
				]
			]

			// Input label
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 8, 0, 4)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InputLabel", "Enter new name:"))
			]

			// Name input
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 16)
			[
				SAssignNew(NameInputBox, SEditableTextBox)
				.Text(FText::FromString(CurrentDisplayName))
				.SelectAllTextWhenFocused(true)
				.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type CommitType)
				{
					if (CommitType == ETextCommit::OnEnter)
					{
						// Default to Stage rename on Enter
						OnRenameAsStageClicked();
					}
				})
			]

			// Rename buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(FMargin(4.f, 0.f))

				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("RenameAsStage", "Rename as Stage"))
					.ToolTipText(LOCTEXT("RenameAsStageTooltip", "Rename to DL_Stage_{Name}"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SDataLayerQuickRenameDialog::OnRenameAsStageClicked)
				]

				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("RenameAsAct", "Rename as Act"))
					.ToolTipText(LOCTEXT("RenameAsActTooltip", "Rename to DL_Act_{ParentStageName}_{Name}"))
					.HAlign(HAlign_Center)
					.OnClicked(this, &SDataLayerQuickRenameDialog::OnRenameAsActClicked)
				]
			]

			// Cancel button
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(SButton)
				.Text(LOCTEXT("Cancel", "Cancel"))
				.OnClicked(this, &SDataLayerQuickRenameDialog::OnCancelClicked)
			]
		]
	];
}

void SDataLayerQuickRenameDialog::ShowDialog(UDataLayerAsset* DataLayerAsset)
{
	if (!DataLayerAsset)
	{
		return;
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Quick Rename DataLayer"))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<SDataLayerQuickRenameDialog> Dialog = SNew(SDataLayerQuickRenameDialog)
		.DataLayerAsset(DataLayerAsset);

	Dialog->WeakParentWindow = Window;

	Window->SetContent(Dialog);

	FSlateApplication::Get().AddModalWindow(Window, FGlobalTabmanager::Get()->GetRootWindow());
}

FReply SDataLayerQuickRenameDialog::OnRenameAsStageClicked()
{
	if (!NameInputBox.IsValid())
	{
		return FReply::Handled();
	}

	FString UserInput = NameInputBox->GetText().ToString().TrimStartAndEnd();
	if (UserInput.IsEmpty())
	{
		return FReply::Handled();
	}

	// Generate Stage name: DL_Stage_{UserInput}
	FString NewName = FString::Printf(TEXT("DL_Stage_%s"), *UserInput);

	if (ExecuteRename(NewName))
	{
		// Close the dialog
		if (TSharedPtr<SWindow> Window = WeakParentWindow.Pin())
		{
			Window->RequestDestroyWindow();
		}
	}

	return FReply::Handled();
}

FReply SDataLayerQuickRenameDialog::OnRenameAsActClicked()
{
	if (!NameInputBox.IsValid())
	{
		return FReply::Handled();
	}

	FString UserInput = NameInputBox->GetText().ToString().TrimStartAndEnd();
	if (UserInput.IsEmpty())
	{
		return FReply::Handled();
	}

	// Get parent Stage name
	FString ParentStageName = GetParentStageName();
	if (ParentStageName.IsEmpty())
	{
		// No parent Stage found, show warning
		UE_LOG(LogTemp, Warning, TEXT("Cannot rename as Act: No parent Stage DataLayer found"));
		return FReply::Handled();
	}

	// Generate Act name: DL_Act_{ParentStageName}_{UserInput}
	FString NewName = FString::Printf(TEXT("DL_Act_%s_%s"), *ParentStageName, *UserInput);

	if (ExecuteRename(NewName))
	{
		// Close the dialog
		if (TSharedPtr<SWindow> Window = WeakParentWindow.Pin())
		{
			Window->RequestDestroyWindow();
		}
	}

	return FReply::Handled();
}

FReply SDataLayerQuickRenameDialog::OnCancelClicked()
{
	if (TSharedPtr<SWindow> Window = WeakParentWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}
	return FReply::Handled();
}

bool SDataLayerQuickRenameDialog::ExecuteRename(const FString& NewName)
{
	if (!DataLayerAsset)
	{
		return false;
	}

	// Get the asset path
	FString PackagePath = FPackageName::GetLongPackagePath(DataLayerAsset->GetPackage()->GetName());

	// Use AssetTools to rename
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	TArray<FAssetRenameData> RenameData;
	RenameData.Add(FAssetRenameData(DataLayerAsset, PackagePath, NewName));

	bool bResult = AssetToolsModule.Get().RenameAssets(RenameData);

	if (bResult)
	{
		UE_LOG(LogTemp, Log, TEXT("Renamed DataLayer to: %s"), *NewName);

		// Notify DataLayer subsystem to trigger refresh in any listening UI
		// Find the DataLayerInstance for this asset and broadcast change
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World)
		{
			if (AWorldDataLayers* WorldDataLayers = World->GetWorldDataLayers())
			{
				WorldDataLayers->ForEachDataLayerInstance([this](UDataLayerInstance* Instance) -> bool
				{
					if (Instance && Instance->GetAsset() == DataLayerAsset)
					{
						// Broadcast DataLayer changed event to trigger UI refresh
						if (UDataLayerEditorSubsystem* Subsystem = UDataLayerEditorSubsystem::Get())
						{
							Subsystem->OnDataLayerChanged().Broadcast(EDataLayerAction::Rename, Instance, NAME_None);
						}
						return false; // Stop iteration
					}
					return true; // Continue
				});
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to rename DataLayer to: %s"), *NewName);
	}

	return bResult;
}

FString SDataLayerQuickRenameDialog::GetParentStageName() const
{
	if (!DataLayerAsset)
	{
		return FString();
	}

	// Get the world and find the DataLayerInstance
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FString();
	}

	// Find the DataLayerInstance for this asset
	UDataLayerInstance* DataLayerInstance = nullptr;

	// Iterate through all WorldDataLayers to find the instance
	if (AWorldDataLayers* WorldDataLayers = World->GetWorldDataLayers())
	{
		WorldDataLayers->ForEachDataLayerInstance([&](UDataLayerInstance* Instance) -> bool
		{
			if (Instance && Instance->GetAsset() == DataLayerAsset)
			{
				DataLayerInstance = Instance;
				return false; // Stop iteration
			}
			return true; // Continue
		});
	}

	if (!DataLayerInstance)
	{
		return FString();
	}

	// Get parent DataLayer
	UDataLayerInstance* ParentInstance = DataLayerInstance->GetParent();
	if (!ParentInstance)
	{
		return FString();
	}

	const UDataLayerAsset* ParentAsset = ParentInstance->GetAsset();
	if (!ParentAsset)
	{
		return FString();
	}

	// Parse parent name to get Stage name
	FDataLayerNameParseResult ParseResult = FStageDataLayerNameParser::Parse(ParentAsset->GetName());
	if (ParseResult.bIsValid && ParseResult.bIsStageLayer)
	{
		return ParseResult.StageName;
	}

	// Fallback: return parent name without prefix if not parseable
	return ParentAsset->GetName();
}

#undef LOCTEXT_NAMESPACE
