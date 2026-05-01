// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayerSync/DataLayerImporter.h"
#include "DataLayerSync/DataLayerSyncUtils.h"
#include "DataLayerSync/StageDataLayerNameParser.h"
#include "DataLayerSync/DataLayerSyncStatusCache.h"
#include "EditorLogic/StageEditorController.h"
#include "StageEditorModule.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "Subsystems/StageEditorSubsystem.h"
#include "Actors/Stage.h"
#include "Core/StageCoreTypes.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "StageEditorDataLayerImport"

// Use shared utilities
using namespace StageDataLayerSyncUtils;

//----------------------------------------------------------------
// FDataLayerImporter Implementation
//----------------------------------------------------------------

bool FDataLayerImporter::CanImport(UDataLayerAsset* DataLayerAsset, UWorld* World, FText& OutErrorMessage)
{
	if (!DataLayerAsset)
	{
		OutErrorMessage = LOCTEXT("ErrorNullAsset", "DataLayerAsset is null");
		return false;
	}

	if (!World)
	{
		World = GetEditorWorld();
	}
	if (!World)
	{
		OutErrorMessage = LOCTEXT("ErrorNoWorld", "No valid World found");
		return false;
	}

	// Check naming convention
	FDataLayerNameParseResult ParseResult = FStageDataLayerNameParser::Parse(DataLayerAsset->GetName());
	if (!ParseResult.bIsValid)
	{
		OutErrorMessage = LOCTEXT("ErrorInvalidNaming", "DataLayer name does not follow naming convention (DL_Stage_*)");
		return false;
	}

	if (!ParseResult.bIsStageLayer)
	{
		OutErrorMessage = LOCTEXT("ErrorNotStageLayer", "Only Stage-level DataLayers (DL_Stage_*) can be imported. Select the parent Stage DataLayer.");
		return false;
	}

	// Check if already imported
	UStageManagerSubsystem* Subsystem = GetStageManagerSubsystem(World);
	if (Subsystem && Subsystem->IsDataLayerImported(DataLayerAsset))
	{
		OutErrorMessage = LOCTEXT("ErrorAlreadyImported", "This DataLayer has already been imported");
		return false;
	}

	return true;
}

TArray<UDataLayerInstance*> FDataLayerImporter::GetChildDataLayers(UDataLayerAsset* ParentAsset, UWorld* World)
{
	TArray<UDataLayerInstance*> Result;

	if (!ParentAsset || !World)
	{
		return Result;
	}

	UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(World);
	if (!Manager)
	{
		return Result;
	}

	Manager->ForEachDataLayerInstance([&](UDataLayerInstance* Instance)
	{
		if (Instance && Instance->GetParent())
		{
			if (Instance->GetParent()->GetAsset() == ParentAsset)
			{
				Result.Add(Instance);
			}
		}
		return true; // Continue iteration
	});

	return Result;
}

TArray<AActor*> FDataLayerImporter::GetActorsInDataLayer(UDataLayerAsset* Asset, UWorld* World)
{
	TArray<AActor*> Result;

	if (!Asset || !World)
	{
		return Result;
	}

	UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(World);
	if (!Manager)
	{
		return Result;
	}

	const UDataLayerInstance* Instance = Manager->GetDataLayerInstance(Asset);
	if (!Instance)
	{
		return Result;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->HasDataLayers())
		{
			TArray<const UDataLayerInstance*> ActorDataLayers = Actor->GetDataLayerInstances();
			if (ActorDataLayers.Contains(Instance))
			{
				Result.Add(Actor);
			}
		}
	}

	return Result;
}

FDataLayerImportPreview FDataLayerImporter::GeneratePreview(UDataLayerAsset* StageDataLayerAsset, UWorld* World)
{
	FDataLayerImportPreview Preview;

	// Validate
	FText ErrorMessage;
	if (!CanImport(StageDataLayerAsset, World, ErrorMessage))
	{
		Preview.bIsValid = false;
		Preview.ErrorMessage = ErrorMessage;
		return Preview;
	}

	if (!World)
	{
		World = GetEditorWorld();
	}

	// Parse Stage name
	FDataLayerNameParseResult StageParseResult = FStageDataLayerNameParser::Parse(StageDataLayerAsset->GetName());
	Preview.SourceDataLayerName = StageDataLayerAsset->GetName();

	// Add Stage item (Depth 0)
	FDataLayerImportPreviewItem StageItem;
	StageItem.DisplayName = FString::Printf(TEXT("Stage: %s"), *StageParseResult.StageName);
	StageItem.SUIDDisplay = TEXT("S:?"); // Will be assigned on import
	StageItem.ItemType = TEXT("Stage");
	StageItem.DataLayerAsset = StageDataLayerAsset;
	StageItem.Depth = 0;
	Preview.Items.Add(StageItem);
	Preview.StageCount = 1;

	// Get child DataLayers (Acts) - collect ALL children regardless of naming
	TArray<UDataLayerInstance*> ChildInstances = GetChildDataLayers(StageDataLayerAsset, World);

	for (UDataLayerInstance* ChildInstance : ChildInstances)
	{
		if (!ChildInstance)
		{
			continue;
		}

		const UDataLayerAsset* ChildAsset = ChildInstance->GetAsset();
		if (!ChildAsset)
		{
			continue;
		}

		// Check naming convention
		FDataLayerNameParseResult ActParseResult = FStageDataLayerNameParser::Parse(ChildAsset->GetName());

		FString ActDisplayName;
		bool bHasNamingWarning = false;
		FString WarningReason;

		if (!ActParseResult.bIsValid)
		{
			// Non-standard naming - still import but warn
			bHasNamingWarning = true;
			WarningReason = TEXT("Does not follow naming convention (expected: DL_Act_<StageName>_<ActName>)");
			// Use the DataLayer name as display name
			ActDisplayName = ChildAsset->GetName();
		}
		else if (ActParseResult.bIsStageLayer)
		{
			// Child is also a Stage DataLayer - unusual but import anyway
			bHasNamingWarning = true;
			WarningReason = TEXT("Named as Stage DataLayer (DL_Stage_*) but is a child - treating as Act");
			ActDisplayName = ActParseResult.StageName;
		}
		else if (ActParseResult.StageName != StageParseResult.StageName)
		{
			// StageName in Act does not match parent Stage - warn but import
			bHasNamingWarning = true;
			WarningReason = FString::Printf(TEXT("Stage name mismatch: Act has '%s' but parent Stage is '%s'"),
				*ActParseResult.StageName, *StageParseResult.StageName);
			ActDisplayName = ActParseResult.ActName;
		}
		else
		{
			// Valid Act naming: DL_Act_<StageName>_<ActName>
			ActDisplayName = ActParseResult.ActName;
		}

		// Record warning if any
		if (bHasNamingWarning)
		{
			FDataLayerNamingWarning Warning;
			Warning.DataLayerName = ChildAsset->GetName();
			Warning.DataLayerAsset = const_cast<UDataLayerAsset*>(ChildAsset);
			Warning.WarningReason = WarningReason;
			Preview.NamingWarnings.Add(Warning);
		}

		// Add Act item (Depth 1) - always include child DataLayers
		FDataLayerImportPreviewItem ActItem;
		ActItem.DisplayName = FString::Printf(TEXT("Act: %s"), *ActDisplayName);
		ActItem.SUIDDisplay = FString::Printf(TEXT("A:?.%d"), Preview.ActCount + 1);
		ActItem.ItemType = TEXT("Act");
		ActItem.DataLayerAsset = const_cast<UDataLayerAsset*>(ChildAsset);
		ActItem.Depth = 1;
		Preview.Items.Add(ActItem);
		Preview.ActCount++;

		// Get Actors in this Act
		TArray<AActor*> ActorsInAct = GetActorsInDataLayer(const_cast<UDataLayerAsset*>(ChildAsset), World);
		if (ActorsInAct.Num() > 0)
		{
			// Add Entities summary item (Depth 2)
			FDataLayerImportPreviewItem EntitiesItem;
			EntitiesItem.DisplayName = FString::Printf(TEXT("Entities: %d actors"), ActorsInAct.Num());
			EntitiesItem.ItemType = TEXT("Entities");
			EntitiesItem.Depth = 2;
			EntitiesItem.ActorCount = ActorsInAct.Num();
			Preview.Items.Add(EntitiesItem);
			Preview.EntityCount += ActorsInAct.Num();
		}
	}

	Preview.bIsValid = true;
	return Preview;
}

AStage* FDataLayerImporter::CreateStageActor(const FString& StageName, TSubclassOf<AStage> StageBlueprintClass, UWorld* World)
{
	if (!World)
	{
		UE_LOG(LogStageEditor, Error, TEXT("Cannot create Stage '%s': World is null"), *StageName);
		return nullptr;
	}

	// Validate Blueprint class
	if (!StageBlueprintClass)
	{
		UE_LOG(LogStageEditor, Error,
			TEXT("Cannot create Stage '%s' without a Blueprint class. Please select a Stage Blueprint class in the Import dialog."),
			*StageName);
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Use user-provided Blueprint class instead of hardcoded AStage::StaticClass()
	AStage* NewStage = World->SpawnActor<AStage>(
		StageBlueprintClass,  // User's Blueprint class
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		SpawnParams);

	if (NewStage)
	{
		NewStage->StageName = StageName;
		NewStage->SetActorLabel(StageName);
	}

	return NewStage;
}

void FDataLayerImporter::AssignSUID(AStage* Stage)
{
	if (!Stage)
	{
		return;
	}

	UWorld* World = Stage->GetWorld();
	if (!World)
	{
		return;
	}

	UStageEditorSubsystem* EditorSub = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (EditorSub)
	{
		EditorSub->RegisterStage(Stage);
	}
}

FDataLayerImportResult FDataLayerImporter::ExecuteImport(UDataLayerAsset* StageDataLayerAsset, UWorld* World)
{
	// Simple version: use default DefaultAct selection (first child)
	FDataLayerImportParams Params;
	Params.StageDataLayerAsset = StageDataLayerAsset;
	Params.SelectedDefaultActIndex = 0;
	return ExecuteImport(Params, World);
}

FDataLayerImportResult FDataLayerImporter::ExecuteImport(const FDataLayerImportParams& Params, UWorld* World)
{
	FDataLayerImportResult Result;

	// Validate Blueprint class
	if (!Params.StageBlueprintClass)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = LOCTEXT("ErrorNoBlueprintClass",
			"Stage Blueprint Class is required for import. Please select a Blueprint class in the Import dialog.");
		return Result;
	}

	// Delegate to Controller for unified import logic
	if (!FStageEditorModule::IsAvailable())
	{
		Result.bSuccess = false;
		Result.ErrorMessage = LOCTEXT("ErrorModuleNotLoaded", "StageEditor module is not loaded");
		return Result;
	}

	TSharedPtr<FStageEditorController> Controller = FStageEditorModule::Get().GetController();
	if (!Controller.IsValid())
	{
		Result.bSuccess = false;
		Result.ErrorMessage = LOCTEXT("ErrorNoController", "Failed to get StageEditorController");
		return Result;
	}

	// Use Controller's unified import API with DefaultAct selection and Blueprint class
	AStage* NewStage = Controller->ImportStageFromDataLayerWithDefaultAct(
		Params.StageDataLayerAsset,
		Params.SelectedDefaultActIndex,
		Params.StageBlueprintClass,  // Pass user-selected Blueprint class
		World);

	if (!NewStage)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = LOCTEXT("ErrorImportFailed", "Import failed - see log for details");
		return Result;
	}

	// Fill result struct
	Result.bSuccess = true;
	Result.CreatedStage = NewStage;
	Result.CreatedActCount = NewStage->Acts.Num() - 1; // -1 for default act

	// Count registered entities
	for (const FAct& Act : NewStage->Acts)
	{
		Result.RegisteredEntityCount += Act.EntityStateOverrides.Num();
	}

	// Broadcast stage data changed to notify StageEditorPanel to refresh
	if (UStageManagerSubsystem* Subsystem = GetStageManagerSubsystem(World))
	{
		Subsystem->BroadcastStageDataChanged(NewStage);
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
