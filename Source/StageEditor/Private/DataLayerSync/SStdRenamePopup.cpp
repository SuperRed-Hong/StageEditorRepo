// Copyright Stage Editor Plugin. All Rights Reserved.

#include "DataLayerSync/SStdRenamePopup.h"
#include "DataLayerSync/StageDataLayerNameParser.h"
#include "EditorLogic/StageEditorController.h"
#include "StageEditorModule.h"
#include "Actors/Stage.h"

#include "AssetToolsModule.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"

#define LOCTEXT_NAMESPACE "SStdRenamePopup"

bool SStdRenamePopup::IsStageDataLayer(const UDataLayerInstance* Instance)
{
	if (!Instance)
	{
		return true; // Default to Stage if no instance
	}

	// No parent = Stage, Has parent = Act
	return Instance->GetParent() == nullptr;
}

void SStdRenamePopup::Construct(const FArguments& InArgs)
{
	DataLayerAsset = InArgs._DataLayerAsset;
	DataLayerInstance = InArgs._DataLayerInstance;

	// Determine if Stage or Act based on hierarchy
	bIsStage = IsStageDataLayer(DataLayerInstance);

	// For Act, try to get the parent Stage name from the DataLayer hierarchy
	FString ParentStageName;
	if (!bIsStage && DataLayerInstance)
	{
		// Get parent Stage DataLayer's name
		if (const UDataLayerInstance* ParentInstance = DataLayerInstance->GetParent())
		{
			if (const UDataLayerAsset* ParentAsset = ParentInstance->GetAsset())
			{
				FDataLayerNameParseResult ParentParseResult = FStageDataLayerNameParser::Parse(ParentAsset->GetName());
				if (ParentParseResult.bIsValid && ParentParseResult.bIsStageLayer)
				{
					ParentStageName = ParentParseResult.StageName;
				}
			}
		}
	}

	// Set the appropriate prefix
	// For Stage: DL_Stage_
	// For Act: DL_Act_<StageName>_ (include Stage name in prefix)
	if (bIsStage)
	{
		NamePrefix = TEXT("DL_Stage_");
	}
	else
	{
		if (!ParentStageName.IsEmpty())
		{
			NamePrefix = FString::Printf(TEXT("DL_Act_%s_"), *ParentStageName);
		}
		else
		{
			// Fallback: try to extract StageName from current DataLayer name
			if (DataLayerAsset)
			{
				FDataLayerNameParseResult ParseResult = FStageDataLayerNameParser::Parse(DataLayerAsset->GetName());
				if (ParseResult.bIsValid && !ParseResult.bIsStageLayer)
				{
					NamePrefix = FString::Printf(TEXT("DL_Act_%s_"), *ParseResult.StageName);
				}
				else
				{
					NamePrefix = TEXT("DL_Act_");
				}
			}
			else
			{
				NamePrefix = TEXT("DL_Act_");
			}
		}
	}

	// Extract current custom name part if already following convention
	FString CurrentSuffix;
	if (DataLayerAsset)
	{
		FDataLayerNameParseResult ParseResult = FStageDataLayerNameParser::Parse(DataLayerAsset->GetName());
		if (ParseResult.bIsValid)
		{
			if (ParseResult.bIsStageLayer)
			{
				CurrentSuffix = ParseResult.StageName;
			}
			else
			{
				// For Act, only show the ActName part (StageName is now in prefix)
				CurrentSuffix = ParseResult.ActName;
			}
		}
	}

	// Build the UI
	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(12.f))
		[
			SNew(SVerticalBox)

			// Title row
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.Text(bIsStage
					? LOCTEXT("TitleStage", "Rename as Stage DataLayer")
					: LOCTEXT("TitleAct", "Rename as Act DataLayer"))
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
			]

			// Input row: Prefix label + editable text
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 12)
			[
				SNew(SHorizontalBox)

				// Fixed prefix display
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(NamePrefix))
					.Font(FAppStyle::GetFontStyle("NormalFont"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]

				// Editable suffix
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SAssignNew(NameInputBox, SEditableTextBox)
					.Text(FText::FromString(CurrentSuffix))
					.SelectAllTextWhenFocused(true)
					.OnTextCommitted(this, &SStdRenamePopup::OnTextCommitted)
				]
			]

			// Button row
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
					.Text(LOCTEXT("OK", "OK"))
					.OnClicked(this, &SStdRenamePopup::OnOKClicked)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.OnClicked(this, &SStdRenamePopup::OnCancelClicked)
				]
			]
		]
	];
}

void SStdRenamePopup::ShowPopup(UDataLayerAsset* DataLayerAsset, UDataLayerInstance* DataLayerInstance)
{
	if (!DataLayerAsset)
	{
		return;
	}

	bool bIsStage = IsStageDataLayer(DataLayerInstance);

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(bIsStage
			? LOCTEXT("WindowTitleStage", "Std Rename - Stage")
			: LOCTEXT("WindowTitleAct", "Std Rename - Act"))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TSharedRef<SStdRenamePopup> Popup = SNew(SStdRenamePopup)
		.DataLayerAsset(DataLayerAsset)
		.DataLayerInstance(DataLayerInstance);

	Popup->WeakParentWindow = Window;

	Window->SetContent(Popup);

	// Set keyboard focus to the input box after showing
	FSlateApplication::Get().AddModalWindow(Window, FGlobalTabmanager::Get()->GetRootWindow());
}

void SStdRenamePopup::OnTextCommitted(const FText& Text, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		if (ExecuteRename())
		{
			ClosePopup();
		}
	}
}

FReply SStdRenamePopup::OnOKClicked()
{
	if (ExecuteRename())
	{
		ClosePopup();
	}
	return FReply::Handled();
}

FReply SStdRenamePopup::OnCancelClicked()
{
	ClosePopup();
	return FReply::Handled();
}

bool SStdRenamePopup::ExecuteRename()
{
	if (!DataLayerAsset || !NameInputBox.IsValid())
	{
		return false;
	}

	FString UserInput = NameInputBox->GetText().ToString().TrimStartAndEnd();
	if (UserInput.IsEmpty())
	{
		return false;
	}

	// Construct full name: Prefix + UserInput
	FString NewName = NamePrefix + UserInput;

	// Check if this DataLayer is imported into StageEditor
	// If so, use Controller for unified rename logic
	if (FStageEditorModule::IsAvailable())
	{
		TSharedPtr<FStageEditorController> Controller = FStageEditorModule::Get().GetController();
		if (Controller.IsValid())
		{
			AStage* OwnerStage = Controller->FindStageByDataLayer(DataLayerAsset);
			if (OwnerStage)
			{
				// DataLayer is imported - use Controller API
				if (OwnerStage->StageDataLayerAsset == DataLayerAsset)
				{
					// It's the Stage's root DataLayer
					bool bResult = Controller->RenameStageDataLayer(OwnerStage, NewName);
					if (bResult)
					{
						UE_LOG(LogTemp, Log, TEXT("Std Rename (via Controller): Renamed Stage DataLayer to: %s"), *NewName);
					}
					return bResult;
				}
				else
				{
					// It's an Act DataLayer
					int32 ActID = Controller->FindActIDByDataLayer(OwnerStage, DataLayerAsset);
					if (ActID >= 0)
					{
						bool bResult = Controller->RenameActDataLayer(OwnerStage, ActID, NewName);
						if (bResult)
						{
							UE_LOG(LogTemp, Log, TEXT("Std Rename (via Controller): Renamed Act DataLayer to: %s"), *NewName);
						}
						return bResult;
					}
				}
			}
		}
	}

	// DataLayer is not imported - directly rename Asset
	FString PackagePath = FPackageName::GetLongPackagePath(DataLayerAsset->GetPackage()->GetName());

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	TArray<FAssetRenameData> RenameData;
	RenameData.Add(FAssetRenameData(DataLayerAsset, PackagePath, NewName));

	bool bResult = AssetToolsModule.Get().RenameAssets(RenameData);

	if (bResult)
	{
		UE_LOG(LogTemp, Log, TEXT("Std Rename (direct): Renamed DataLayer to: %s"), *NewName);

		// Notify DataLayer subsystem to trigger refresh
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World)
		{
			if (AWorldDataLayers* WorldDataLayers = World->GetWorldDataLayers())
			{
				WorldDataLayers->ForEachDataLayerInstance([this](UDataLayerInstance* Instance) -> bool
				{
					if (Instance && Instance->GetAsset() == DataLayerAsset)
					{
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
		UE_LOG(LogTemp, Warning, TEXT("Std Rename: Failed to rename DataLayer to: %s"), *NewName);
	}

	return bResult;
}

void SStdRenamePopup::ClosePopup()
{
	if (TSharedPtr<SWindow> Window = WeakParentWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}
}

#undef LOCTEXT_NAMESPACE
