// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorUI/StageEditorActionHandlers.h"
#include "EditorUI/StageEditorPanel.h"
#include "DataModels/StageTreeItem.h"
#include "EditorLogic/StageEditorController.h"
#include "Actors/Stage.h"
#include "Data/StageRegistryAsset.h"
#include "Subsystems/StageEditorSubsystem.h"
#include "DataLayerSync/StageMigrationAnalyzer.h"
#include "DataLayerSync/SStageMigrationDialog.h"
#include "DataLayerSync/SRegistryCreationDialog.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Editor.h"
#include "Selection.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "DebugHeader.h"
#include "Logging/StageEditorLog.h"

#define LOCTEXT_NAMESPACE "StageEditorActionHandlers"

//----------------------------------------------------------------
// Constructor / Destructor
//----------------------------------------------------------------

FStageEditorActionHandlers::FStageEditorActionHandlers(
	TWeakPtr<FStageEditorController> InController,
	TWeakPtr<SStageEditorPanel> InPanel,
	TSharedPtr<FStructOnScope> InCreationSettings)
	: WeakController(InController)
	, WeakPanel(InPanel)
	, CreationSettings(InCreationSettings)
{
}

FStageEditorActionHandlers::~FStageEditorActionHandlers()
{
}

//----------------------------------------------------------------
// Button Actions
//----------------------------------------------------------------

FReply FStageEditorActionHandlers::OnCreateActClicked()
{
	if (TSharedPtr<FStageEditorController> Controller = WeakController.Pin())
	{
		Controller->CreateNewAct();
	}
	return FReply::Handled();
}

FReply FStageEditorActionHandlers::OnRegisterSelectedEntitiesClicked()
{
	if (TSharedPtr<FStageEditorController> Controller = WeakController.Pin())
	{
		if (GEditor)
		{
			TArray<AActor*> SelectedActors;
			GEditor->GetSelectedActors()->GetSelectedObjects<AActor>(SelectedActors);
			Controller->RegisterEntities(SelectedActors);
		}
	}
	return FReply::Handled();
}

FReply FStageEditorActionHandlers::OnCreateStageBPClicked()
{
	if (TSharedPtr<FStageEditorController> Controller = WeakController.Pin())
	{
		FAssetCreationSettings* Settings = GetSettings();
		if (!Settings)
		{
			return FReply::Handled();
		}

		FString Path;
		if (Settings->bIsCustomStageAssetFolderPath)
		{
			// Convert physical path to virtual path
			FString PhysicalPath = Settings->StageAssetFolderPath.Path;
			FString ProjectContentDir = FPaths::ProjectContentDir();

			if (PhysicalPath.StartsWith(ProjectContentDir))
			{
				FString RelativePath = PhysicalPath.RightChop(ProjectContentDir.Len());
				Path = TEXT("/Game/") + RelativePath;
			}
			else
			{
				// Assume it's already a virtual path
				Path = PhysicalPath;
			}
		}
		else
		{
			Path = TEXT("/StageEditor/StagesBP");
		}

		// Load default parent class from settings
		UClass* DefaultParentClass = nullptr;

		// Check if path is not null
		if (!Settings->DefaultStageBlueprintParentClass.IsNull())
		{
			// Try LoadSynchronous first
			DefaultParentClass = Settings->DefaultStageBlueprintParentClass.LoadSynchronous();

			// If LoadSynchronous fails (asset not yet loaded), try StaticLoadClass as fallback
			if (!DefaultParentClass)
			{
				FString ClassPath = Settings->DefaultStageBlueprintParentClass.ToString();
				DefaultParentClass = StaticLoadClass(AStage::StaticClass(), nullptr, *ClassPath);
			}
		}

		// Call with default parent class and name prefix
		Controller->CreateStageBlueprint(Path, DefaultParentClass, TEXT("BP_Stage_"));
	}
	return FReply::Handled();
}

FReply FStageEditorActionHandlers::OnCreateEntityActorBPClicked()
{
	if (TSharedPtr<FStageEditorController> Controller = WeakController.Pin())
	{
		FAssetCreationSettings* Settings = GetSettings();
		if (!Settings)
		{
			return FReply::Handled();
		}

		FString Path;
		if (Settings->bIsCustomEntityActorAssetPath)
		{
			// Convert physical path to virtual path
			FString PhysicalPath = Settings->EntityActorAssetFolderPath.Path;
			FString ProjectContentDir = FPaths::ProjectContentDir();

			if (PhysicalPath.StartsWith(ProjectContentDir))
			{
				FString RelativePath = PhysicalPath.RightChop(ProjectContentDir.Len());
				Path = TEXT("/Game/") + RelativePath;
			}
			else
			{
				// Assume it's already a virtual path
				Path = PhysicalPath;
			}
		}
		else
		{
			Path = TEXT("/StageEditor/EntitiesBP");
		}

		// Load default parent class from settings
		UClass* DefaultParentClass = nullptr;
		if (Settings->DefaultEntityActorBlueprintParentClass.IsValid())
		{
			// Try LoadSynchronous first
			DefaultParentClass = Settings->DefaultEntityActorBlueprintParentClass.LoadSynchronous();

			// If LoadSynchronous fails (asset not yet loaded), try StaticLoadClass as fallback
			if (!DefaultParentClass)
			{
				FString ClassPath = Settings->DefaultEntityActorBlueprintParentClass.ToString();
				DefaultParentClass = StaticLoadClass(AStageEntity::StaticClass(), nullptr, *ClassPath);
			}
		}

		Controller->CreateEntityActorBlueprint(Path, DefaultParentClass);
	}
	return FReply::Handled();
}

FReply FStageEditorActionHandlers::OnCreateEntityComponentBPClicked()
{
	if (TSharedPtr<FStageEditorController> Controller = WeakController.Pin())
	{
		FAssetCreationSettings* Settings = GetSettings();
		if (!Settings)
		{
			return FReply::Handled();
		}

		FString Path;
		if (Settings->bIsCustomEntityComponentAssetPath)
		{
			// Convert physical path to virtual path
			FString PhysicalPath = Settings->EntityComponentAssetFolderPath.Path;
			FString ProjectContentDir = FPaths::ProjectContentDir();

			if (PhysicalPath.StartsWith(ProjectContentDir))
			{
				FString RelativePath = PhysicalPath.RightChop(ProjectContentDir.Len());
				Path = TEXT("/Game/") + RelativePath;
			}
			else
			{
				// Assume it's already a virtual path
				Path = PhysicalPath;
			}
		}
		else
		{
			Path = TEXT("/StageEditor/EntitiesBP");
		}

		// Load default parent class from settings
		UClass* DefaultParentClass = nullptr;
		if (Settings->DefaultEntityComponentBlueprintParentClass.IsValid())
		{
			// Try LoadSynchronous first
			DefaultParentClass = Settings->DefaultEntityComponentBlueprintParentClass.LoadSynchronous();

			// If LoadSynchronous fails (asset not yet loaded), try StaticLoadClass as fallback
			if (!DefaultParentClass)
			{
				FString ClassPath = Settings->DefaultEntityComponentBlueprintParentClass.ToString();
				DefaultParentClass = StaticLoadClass(UStageEntityComponent::StaticClass(), nullptr, *ClassPath);
			}
		}

		Controller->CreateEntityComponentBlueprint(Path, DefaultParentClass);
	}
	return FReply::Handled();
}

FReply FStageEditorActionHandlers::OnCleanOrphanedEntitiesClicked()
{
	if (TSharedPtr<FStageEditorController> Controller = WeakController.Pin())
	{
		int32 CleanedCount = Controller->CleanOrphanedEntities();

		// Show feedback message
		FText Message = FText::Format(
			LOCTEXT("OrphanedEntitiesCleaned", "Cleaned {0} orphaned Entity(ies)."),
			FText::AsNumber(CleanedCount)
		);

		FMessageDialog::Open(
			EAppMsgType::Ok,
			Message,
			LOCTEXT("CleanOrphanedEntitiesTitle", "Clean Orphaned Entities")
		);
	}
	return FReply::Handled();
}

FReply FStageEditorActionHandlers::OnCreateRegistryClicked()
{
	// Get current World
	if (!GEditor)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoEditorError", "Cannot create Registry: GEditor is null."));
		return FReply::Handled();
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoWorldError", "Cannot create Registry: No active world."));
		return FReply::Handled();
	}

	// Get EditorSubsystem
	UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (!EditorSubsystem)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoSubsystemError", "Cannot create Registry: StageEditorSubsystem not found."));
		return FReply::Handled();
	}

	// Check if Registry already exists
	if (TSharedPtr<SStageEditorPanel> Panel = WeakPanel.Pin())
	{
		if (Panel->HasRegistryAsset())
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("RegistryExistsError", "Registry already exists for this level."));
			return FReply::Handled();
		}
	}

	// Show dialog to select collaboration mode
	ECollaborationMode Mode = ECollaborationMode::Solo;
	bool bUserConfirmed = SRegistryCreationDialog::ShowDialog(Mode);

	if (!bUserConfirmed)
	{
		// User cancelled
		return FReply::Handled();
	}

	// Create Registry
	UStageRegistryAsset* Registry = EditorSubsystem->CreateRegistryAsset(World, Mode);
	if (!Registry)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("CreateRegistryError", "Failed to create StageRegistryAsset."));
		return FReply::Handled();
	}

	// Analyze existing Stages and show migration dialog if needed
	FMigrationAnalysisResult Analysis = FStageMigrationAnalyzer::AnalyzeStages(World);

	// Debug logging
	UE_LOG(LogStageEditor, Log, TEXT("OnCreateRegistryClicked: Analysis complete - Total: %d, Valid: %d, Uninitialized: %d, Conflicts: %d, HasIssues: %s"),
		Analysis.GetTotalStageCount(),
		Analysis.ValidStageCount,
		Analysis.UninitializedStageCount,
		Analysis.ConflictStageCount,
		Analysis.HasIssues() ? TEXT("TRUE") : TEXT("FALSE"));

	if (Analysis.HasIssues())
	{
		UE_LOG(LogStageEditor, Log, TEXT("OnCreateRegistryClicked: Showing Migration Dialog..."));
		// Show migration dialog
		bool bMigrationExecuted = SStageMigrationDialog::ShowDialog(World, Analysis, Registry);

		if (bMigrationExecuted)
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("RegistryCreatedWithMigration", "Registry created successfully! Stage IDs have been migrated."));
		}
		else
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("RegistryCreatedNoMigration", "Registry created, but migration was cancelled. Some Stages may have invalid IDs."));
		}
	}
	else
	{
		UE_LOG(LogStageEditor, Log, TEXT("OnCreateRegistryClicked: No issues found, registering all Stages directly"));
		// No issues, just register all Stages
		for (const FStageMigrationAnalysis& StageAnalysis : Analysis.StageAnalyses)
		{
			if (AStage* Stage = StageAnalysis.Stage)
			{
				Registry->AllocateAndRegister(Stage);
			}
		}

		Registry->MarkPackageDirty();

		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("RegistryCreatedSuccess", "Registry created successfully! All Stages have been registered."));
	}

	RefreshUI();
	return FReply::Handled();
}

FReply FStageEditorActionHandlers::OnSelectExistingRegistryClicked()
{
	if (!GEditor)
	{
		return FReply::Handled();
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		DebugHeader::ShowNotifyInfo(TEXT("No active World"));
		return FReply::Handled();
	}

	// Open Asset Picker dialog for StageRegistryAsset
	FAssetPickerConfig PickerConfig;
	PickerConfig.Filter.ClassPaths.Add(UStageRegistryAsset::StaticClass()->GetClassPathName());
	PickerConfig.SelectionMode = ESelectionMode::Single;
	PickerConfig.bAllowNullSelection = false;
	PickerConfig.bFocusSearchBoxWhenOpened = true;
	PickerConfig.InitialAssetViewType = EAssetViewType::List;

	// Capture World and Panel for lambda
	TWeakPtr<SStageEditorPanel> WeakPanelCopy = WeakPanel;

	// Callback when asset is selected
	PickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda(
		[World, WeakPanelCopy](const FAssetData& AssetData)
		{
			if (AssetData.IsValid())
			{
				FString RegistryPath = AssetData.GetSoftObjectPath().ToString();

				// Get Subsystem and set manual association
				UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
				if (EditorSubsystem)
				{
					EditorSubsystem->SetManualRegistryAssociation(World, RegistryPath);

					DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Registry associated: %s"), *RegistryPath));

					// Refresh UI
					if (TSharedPtr<SStageEditorPanel> Panel = WeakPanelCopy.Pin())
					{
						Panel->RefreshUI();
					}
				}
			}

			// Close the picker menu
			FSlateApplication::Get().DismissAllMenus();
		});

	// Create and show the asset picker in a menu
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TSharedRef<SWidget> AssetPicker = ContentBrowserModule.Get().CreateAssetPicker(PickerConfig);

	// Get the panel widget for menu anchoring
	TSharedPtr<SStageEditorPanel> Panel = WeakPanel.Pin();
	if (!Panel.IsValid())
	{
		return FReply::Handled();
	}

	// Show as popup menu
	FSlateApplication::Get().PushMenu(
		Panel.ToSharedRef(),
		FWidgetPath(),
		SNew(SBox)
		.WidthOverride(400)
		.HeightOverride(500)
		[
			AssetPicker
		],
		FSlateApplication::Get().GetCursorPos(),
		FPopupTransitionEffect(FPopupTransitionEffect::TypeInPopup)
	);

	return FReply::Handled();
}

FReply FStageEditorActionHandlers::OnSyncRegistryClicked()
{
	if (TSharedPtr<FStageEditorController> Controller = WeakController.Pin())
	{
		bool bSuccess = Controller->SyncRegistry();

		if (bSuccess)
		{
			RefreshUI();
		}
	}

	return FReply::Handled();
}

FReply FStageEditorActionHandlers::OnViewChangelistClicked()
{
	if (!GEditor)
	{
		return FReply::Handled();
	}

	UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (EditorSubsystem)
	{
		EditorSubsystem->OpenChangelistPanel();
	}

	return FReply::Handled();
}

FReply FStageEditorActionHandlers::OnConvertToWorldPartitionClicked()
{
	if (TSharedPtr<FStageEditorController> Controller = WeakController.Pin())
	{
		// Show confirmation dialog
		const FText Title = LOCTEXT("ConvertConfirmTitle", "Convert to World Partition?");
		const FText Message = LOCTEXT("ConvertConfirmMessage",
			"This will convert the current level to World Partition format.\n\n"
			"Important:\n"
			"- This operation cannot be undone\n"
			"- The level will be saved and reloaded\n"
			"- Make sure you have saved all your work\n\n"
			"Do you want to continue?");

		if (ShowConfirmDialog(Title, Message))
		{
			// Call conversion (this opens UE's native conversion dialog)
			Controller->ConvertToWorldPartition();

			// Check if conversion succeeded (user completed the dialog)
			const bool bNowIsWorldPartition = IsWorldPartitionLevel();

			if (bNowIsWorldPartition)
			{
				// Conversion succeeded - show success message
				DebugHeader::ShowMsgDialog(
					EAppMsgType::Ok,
					TEXT("World Partition conversion completed successfully!\n\n"
						"The level is now using World Partition.\n"
						"Stage Editor features are now available."),
					false  // Not a warning, it's a success
				);

				RefreshUI();
			}
			else
			{
				// Conversion was cancelled or failed
				DebugHeader::ShowMsgDialog(
					EAppMsgType::Ok,
					TEXT("World Partition conversion was cancelled or failed.\n\n"
						"The level is still not using World Partition.\n"
						"Please try again or convert manually via the Level menu."),
					true  // Show as warning
				);
			}
		}
	}

	return FReply::Handled();
}

//----------------------------------------------------------------
// Private Helpers
//----------------------------------------------------------------

FAssetCreationSettings* FStageEditorActionHandlers::GetSettings() const
{
	if (!CreationSettings.IsValid())
	{
		return nullptr;
	}

	return (FAssetCreationSettings*)CreationSettings->GetStructMemory();
}

bool FStageEditorActionHandlers::ShowConfirmDialog(const FText& Title, const FText& Message) const
{
	return FMessageDialog::Open(EAppMsgType::YesNo, Message, Title) == EAppReturnType::Yes;
}

void FStageEditorActionHandlers::RefreshUI()
{
	if (TSharedPtr<SStageEditorPanel> Panel = WeakPanel.Pin())
	{
		Panel->RefreshUI();
	}
}

bool FStageEditorActionHandlers::IsWorldPartitionLevel() const
{
	if (TSharedPtr<SStageEditorPanel> Panel = WeakPanel.Pin())
	{
		return Panel->IsWorldPartitionLevel();
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
