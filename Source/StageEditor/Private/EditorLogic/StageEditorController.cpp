#include "EditorLogic/StageEditorController.h"
#include "Logging/StageEditorLog.h"
#include "Actors/Stage.h"
#include "Actors/StageEntity.h"
#include "DebugHeader.h"
#include "ScopedTransaction.h"
#include "Misc/MessageDialog.h"
#include "AssetToolsModule.h"
#include "Factories/BlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Components/StageEntityComponent.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/DataLayer/WorldDataLayers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "WorldPartitionEditorModule.h"
#include "Kismet2/SClassPickerDialog.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"
#include "DataLayerSync/StageDataLayerNameParser.h"
#include "DataLayerSync/DataLayerSyncStatusCache.h"
#include "Subsystems/StageEditorSubsystem.h"
#include "Data/StageRegistryAsset.h"
#include "ObjectTools.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "FStageEditorController"

// Static member initialization
int32 FStageEditorController::ImportBypassCounter = 0;

FStageEditorController::FStageEditorController()
{
}

void FStageEditorController::Initialize()
{
	BindEditorDelegates();
	FindStageInWorld();
}

void FStageEditorController::FindStageInWorld()
{
	if (!GEditor) return;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return;

	FoundStages.Empty();
	ActiveStage.Reset();

	for (TActorIterator<AStage> It(World); It; ++It)
	{
		AStage* FoundStage = *It;
		if (FoundStage)
		{
			FoundStages.Add(FoundStage);
			UE_LOG(LogStageEditor, Log, TEXT("StageEditor: Found Stage: %s"), *FoundStage->GetActorLabel());
		}
	}
	UE_LOG(LogStageEditor, Log, TEXT("StageEditor: Total Stages Found: %d"), FoundStages.Num());

	if (FoundStages.Num() > 0)
	{
		// Default to the first one as active
		SetActiveStage(FoundStages[0].Get());
		// Note: UI Panel already displays Stage list, notification removed to avoid redundancy
	}
	else
	{
		OnModelChanged.Broadcast(); // Clear UI if no stages found
	}
}

FStageEditorController::~FStageEditorController()
{
	UnbindEditorDelegates();
}

void FStageEditorController::SetActiveStage(AStage* NewStage)
{
	ActiveStage = NewStage;
	OnModelChanged.Broadcast();
}

AStage* FStageEditorController::GetActiveStage() const
{
	return ActiveStage.Get();
}

int32 FStageEditorController::CreateNewAct()
{
	AStage* Stage = GetActiveStage();
	if (!Stage) return -1;

	const FScopedTransaction Transaction(LOCTEXT("CreateNewAct", "Create New Act"));
	Stage->Modify();

	// Mock ID Generation: Find max Act ID + 1
	int32 NewActID = 1;
	for (const FAct& Act : Stage->Acts)
	{
		if (Act.SUID.ActID >= NewActID)
		{
			NewActID = Act.SUID.ActID + 1;
		}
	}

	FAct NewAct;
	NewAct.SUID.StageID = Stage->GetStageID();
	NewAct.SUID.ActID = NewActID;
	NewAct.DisplayName = FString::Printf(TEXT("Act_%d"), NewActID);

	Stage->Acts.Add(NewAct);

	// Auto-create DataLayer for new Act if World Partition is active
	if (IsWorldPartitionActive())
	{
		CreateDataLayerForAct(NewActID);
	}

	// Invalidate sync status cache for Stage's DataLayer
	if (Stage->StageDataLayerAsset)
	{
		FDataLayerSyncStatusCache::Get().InvalidateCache(Stage->StageDataLayerAsset);
	}

	OnModelChanged.Broadcast();
	DebugHeader::ShowNotifyInfo(TEXT("Created new Act: ") + NewAct.DisplayName);
	return NewActID;
}

bool FStageEditorController::RegisterEntities(const TArray<AActor*>& ActorsToRegister, AStage* TargetStage)
{
	AStage* Stage = TargetStage ? TargetStage : GetActiveStage();
	if (!Stage) return false;
	if (ActorsToRegister.Num() == 0) return false;

	const FScopedTransaction Transaction(LOCTEXT("RegisterEntities", "Register Entitys"));
	Stage->Modify();

	bool bAnyRegistered = false;
	for (AActor* Actor : ActorsToRegister)
	{
		if (!Actor) continue;

		// === Prevent registering Stage actors as Entitys (nested Stage not allowed) ===
		if (Actor->IsA<AStage>())
		{
			UE_LOG(LogStageEditor, Error,
				TEXT("Cannot register Stage actor '%s' as a Entity! Stage actors cannot be nested."),
				*Actor->GetActorLabel());

			DebugHeader::ShowMsgDialog(
				EAppMsgType::Ok,
				FString::Printf(TEXT("Cannot register Stage as a Entity!\n\n"
					"Stage: %s\n\n"
					"Stage actors cannot be registered as Entitys.\n"
					"This is a dangerous nested operation and is not allowed."),
					*Actor->GetActorLabel()),
				true);
			continue;
		}

		// === Check if Entity is already registered to another Stage ===
		AStage* OtherStage = nullptr;
		if (IsEntityRegisteredToOtherStage(Actor, Stage, OtherStage))
		{
			FText Message = FText::Format(
				LOCTEXT("EntityAlreadyRegisteredMessage",
					"Entity '{0}' is already registered to Stage '{1}'.\n\n"
					"An Entity can only be registered to one Stage at a time.\n\n"
					"Do you want to move it to Stage '{2}'?"),
				FText::FromString(Actor->GetActorLabel()),
				FText::FromString(OtherStage->GetActorLabel()),
				FText::FromString(Stage->GetActorLabel())
			);

			EAppReturnType::Type Response = FMessageDialog::Open(
				EAppMsgType::YesNo,
				Message,
				LOCTEXT("EntityConflictTitle", "Entity Already Registered")
			);

			if (Response == EAppReturnType::Yes)
			{
				// Unregister from old Stage
				UStageEntityComponent* EntityComp = Actor->FindComponentByClass<UStageEntityComponent>();
				int32 OldEntityID = EntityComp->GetEntityID();

				OtherStage->Modify();
				OtherStage->UnregisterEntity(OldEntityID);

				UE_LOG(LogStageEditor, Log,
					TEXT("Moved Entity '%s' from Stage '%s' to Stage '%s'"),
					*Actor->GetActorLabel(),
					*OtherStage->GetActorLabel(),
					*Stage->GetActorLabel());
			}
			else
			{
				// User chose to skip this Entity
				continue;
			}
		}

		// Check if Actor has UStageEntityComponent
		UStageEntityComponent* EntityComp = Actor->FindComponentByClass<UStageEntityComponent>();

		// Auto-add component if missing
		if (!EntityComp)
		{
			Actor->Modify();
			EntityComp = NewObject<UStageEntityComponent>(Actor, UStageEntityComponent::StaticClass(), TEXT("StageEntityComponent"));
			if (EntityComp)
			{
				Actor->AddInstanceComponent(EntityComp);
				EntityComp->RegisterComponent();
				Actor->RerunConstructionScripts();
			}
		}

		if (EntityComp)
		{
			// Check if already registered (O(N) check for prototype)
			bool bAlreadyRegistered = false;
			for (const auto& Pair : Stage->EntityRegistry)
			{
				if (Pair.Value.Get() == Actor)
				{
					bAlreadyRegistered = true;
					break;
				}
			}

			if (!bAlreadyRegistered)
			{
				int32 NewEntityID = Stage->RegisterEntity(Actor);
				bAnyRegistered = true;

				// NOTE: Entitys are now registered to Stage only, NOT automatically added to any Act.
				// User can manually add Entitys to Acts via:
				// 1. Right-click context menu in RegisteredEntitys folder
				// 2. Drag & drop Entitys onto Acts
				// This gives users full control over which Entitys belong to which Acts.
			}
		}
	}

	if (bAnyRegistered)
	{
		OnModelChanged.Broadcast();
		DebugHeader::ShowNotifyInfo(TEXT("Registered ") + FString::FromInt(ActorsToRegister.Num()) + TEXT(" Entitys"));
	}

	return bAnyRegistered;
}

/** Class filter that only allows AStage and its subclasses */
class FStageClassFilter : public IClassViewerFilter
{
public:
	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return InClass && InClass->IsChildOf(AStage::StaticClass());
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
	{
		return InUnloadedClassData->IsChildOf(AStage::StaticClass());
	}
};

UBlueprint* FStageEditorController::CreateStageBlueprint(const FString& DefaultPath, UClass* DefaultParentClass, const FString& DefaultName)
{
	// Configure class picker to only show Stage and its subclasses
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.DisplayMode = EClassViewerDisplayMode::TreeView;
	Options.bShowUnloadedBlueprints = true;
	Options.bShowNoneOption = false;
	Options.bExpandRootNodes = true;  // Auto-expand root nodes
	Options.bExpandAllNodes = true;   // Auto-expand all nodes to show nested blueprints
	Options.ClassFilters.Add(MakeShared<FStageClassFilter>());

	// Determine the initial class (default selection in the picker)
	UClass* InitialClass = DefaultParentClass;
	if (!InitialClass)
	{
		// Try to load BP_BaseStage as default (fallback if settings not configured)
		const FString FallbackPath = TEXT("/StageEditor/StagesBP/StageBaseBP/BP_BaseStage.BP_BaseStage_C");
		InitialClass = StaticLoadClass(AStage::StaticClass(), nullptr, *FallbackPath);
	}
	if (!InitialClass)
	{
		// Fallback to C++ AStage
		InitialClass = AStage::StaticClass();
	}
	Options.InitiallySelectedClass = InitialClass;  // Set initially selected class

	// Show the class picker dialog
	UClass* SelectedClass = nullptr;
	const bool bPressedOk = SClassPickerDialog::PickClass(
		LOCTEXT("PickStageParentClass", "Pick Parent Class for Stage Blueprint"),
		Options,
		SelectedClass,
		InitialClass  // Use the default parent class as initial selection
	);

	if (bPressedOk && SelectedClass)
	{
		// Use provided default name or fallback
		FString BlueprintName = DefaultName.IsEmpty() ? TEXT("BP_Stage_") : DefaultName;
		return CreateBlueprintAsset(SelectedClass, BlueprintName, DefaultPath);
	}

	return nullptr;  // User cancelled or no class selected
}

void FStageEditorController::CreateEntityActorBlueprint(const FString& DefaultPath, UClass* DefaultParentClass)
{
	// Configure class picker to only show AStageEntity and its subclasses
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.DisplayMode = EClassViewerDisplayMode::TreeView;
	Options.bShowUnloadedBlueprints = true;
	Options.bShowNoneOption = false;
	Options.bExpandRootNodes = true;  // Auto-expand root nodes
	Options.bExpandAllNodes = true;   // Auto-expand all nodes to show nested blueprints

	// Filter to only show AStageEntity and subclasses
	class FEntityClassFilter : public IClassViewerFilter
	{
	public:
		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return InClass && InClass->IsChildOf(AStageEntity::StaticClass());
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return InUnloadedClassData->IsChildOf(AStageEntity::StaticClass());
		}
	};
	Options.ClassFilters.Add(MakeShared<FEntityClassFilter>());

	// Determine the initial class (default selection in the picker)
	UClass* InitialClass = DefaultParentClass;
	if (!InitialClass)
	{
		// Try to load BP_BaseStageEntityActor as default (fallback if settings not configured)
		const FString FallbackPath = TEXT("/StageEditor/EntitiesBP/EntityBaseBP/BP_BaseStageEntityActor.BP_BaseStageEntityActor_C");
		InitialClass = StaticLoadClass(AStageEntity::StaticClass(), nullptr, *FallbackPath);
	}
	if (!InitialClass)
	{
		// Fallback to C++ AStageEntity
		InitialClass = AStageEntity::StaticClass();
	}
	Options.InitiallySelectedClass = InitialClass;  // Set initially selected class

	// Show the class picker dialog
	UClass* SelectedClass = nullptr;
	const bool bPressedOk = SClassPickerDialog::PickClass(
		LOCTEXT("PickEntityActorParentClass", "Pick Parent Class for Entity Actor Blueprint"),
		Options,
		SelectedClass,
		InitialClass  // Use the default parent class as initial selection
	);

	if (bPressedOk && SelectedClass)
	{
		CreateBlueprintAsset(SelectedClass, TEXT("BP_EntityActor_"), DefaultPath);
	}
}

void FStageEditorController::CreateEntityComponentBlueprint(const FString& DefaultPath, UClass* DefaultParentClass)
{
	// Configure class picker to only show UStageEntityComponent and its subclasses
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.DisplayMode = EClassViewerDisplayMode::TreeView;
	Options.bShowUnloadedBlueprints = true;
	Options.bShowNoneOption = false;
	Options.bExpandRootNodes = true;  // Auto-expand root nodes
	Options.bExpandAllNodes = true;   // Auto-expand all nodes to show nested blueprints

	// Filter to only show UStageEntityComponent and subclasses
	class FEntityComponentClassFilter : public IClassViewerFilter
	{
	public:
		virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return InClass && InClass->IsChildOf(UStageEntityComponent::StaticClass());
		}

		virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef<const IUnloadedBlueprintData> InUnloadedClassData, TSharedRef<FClassViewerFilterFuncs> InFilterFuncs) override
		{
			return InUnloadedClassData->IsChildOf(UStageEntityComponent::StaticClass());
		}
	};
	Options.ClassFilters.Add(MakeShared<FEntityComponentClassFilter>());

	// Determine the initial class (default selection in the picker)
	UClass* InitialClass = DefaultParentClass;
	if (!InitialClass)
	{
		// Try to load BPC_BaseStageEntityComponent as default (fallback if settings not configured)
		const FString FallbackPath = TEXT("/StageEditor/EntitiesBP/EntityBaseBP/BPC_BaseStageEntityComponent.BPC_BaseStageEntityComponent_C");
		InitialClass = StaticLoadClass(UStageEntityComponent::StaticClass(), nullptr, *FallbackPath);
	}
	if (!InitialClass)
	{
		// Fallback to C++ UStageEntityComponent
		InitialClass = UStageEntityComponent::StaticClass();
	}
	Options.InitiallySelectedClass = InitialClass;  // Set initially selected class

	// Show the class picker dialog
	UClass* SelectedClass = nullptr;
	const bool bPressedOk = SClassPickerDialog::PickClass(
		LOCTEXT("PickEntityComponentParentClass", "Pick Parent Class for Entity Component Blueprint"),
		Options,
		SelectedClass,
		InitialClass  // Use the default parent class as initial selection
	);

	if (bPressedOk && SelectedClass)
	{
		CreateBlueprintAsset(SelectedClass, TEXT("BPC_EntityComponent_"), DefaultPath);
	}
}


void FStageEditorController::PreviewAct(int32 ActID)
{
	if (AStage* Stage = GetActiveStage())
	{
		// Just call the runtime ActivateAct, which now triggers editor updates via EntityComponent
		Stage->ActivateAct(ActID);

		// Data Layer Logic: Activate target Act's Data Layer, deactivate others
		if (UDataLayerEditorSubsystem* DataLayerSubsystem = UDataLayerEditorSubsystem::Get())
		{
			UWorld* World = Stage->GetWorld();
			
			// Find the target Act's Data Layer Asset
			UDataLayerAsset* TargetDataLayerAsset = nullptr;
			for (const FAct& Act : Stage->Acts)
			{
				if (Act.SUID.ActID == ActID)
				{
					TargetDataLayerAsset = Act.AssociatedDataLayer;
					break;
				}
			}

			// Iterate all Data Layer Instances
			for (UDataLayerInstance* Instance : DataLayerSubsystem->GetAllDataLayers())
			{
				if (!Instance) continue;

				// Check if this instance belongs to ANY Act in the Stage
				bool bIsStageDataLayer = false;
				for (const FAct& Act : Stage->Acts)
				{
					if (Act.AssociatedDataLayer == Instance->GetAsset())
					{
						bIsStageDataLayer = true;
						break;
					}
				}

				if (bIsStageDataLayer)
				{
					// If it's the target, activate it. Otherwise, deactivate it.
					bool bShouldBeActive = (Instance->GetAsset() == TargetDataLayerAsset);
					DataLayerSubsystem->SetDataLayerVisibility(Instance, bShouldBeActive);
				}
			}
		}
	}
}

//----------------------------------------------------------------
// Entity Management
//----------------------------------------------------------------

bool FStageEditorController::SetEntityStateInAct(int32 EntityID, int32 ActID, int32 NewState)
{
	AStage* Stage = GetActiveStage();
	if (!Stage) return false;

	// Find the Act
	FAct* TargetAct = Stage->Acts.FindByPredicate([ActID](const FAct& Act) {
		return Act.SUID.ActID == ActID;
	});

	if (!TargetAct)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Error: Act not found"));
		return false;
	}

	// Check if Entity exists in the Act, if not, add it
	bool bIsNewEntity = !TargetAct->EntityStateOverrides.Contains(EntityID);
	
	const FScopedTransaction Transaction(LOCTEXT("SetEntityState", "Set Entity State"));
	Stage->Modify();

	// Update the state
	TargetAct->EntityStateOverrides.Add(EntityID, NewState);

	// Sync with Data Layer
	SyncEntityToDataLayer(EntityID, ActID);

	OnModelChanged.Broadcast();
	if (bIsNewEntity)
	{
		DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Added Entity %d to Act with State %d"), EntityID, NewState));
	}
	else
	{
		DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Set Entity %d state to %d"), EntityID, NewState));
	}
	return true;
}

bool FStageEditorController::RemoveEntityFromAct(int32 EntityID, int32 ActID)
{
	AStage* Stage = GetActiveStage();
	if (!Stage) return false;

	const FScopedTransaction Transaction(LOCTEXT("RemoveEntityFromAct", "Remove Entity from Act"));
	Stage->Modify();

	// Remove from Data Layer if applicable
	FAct* TargetAct = Stage->Acts.FindByPredicate([ActID](const FAct& Act) {
		return Act.SUID.ActID == ActID;
	});

	if (TargetAct && TargetAct->AssociatedDataLayer)
	{
		if (const TSoftObjectPtr<AActor>* EntityActorPtr = Stage->EntityRegistry.Find(EntityID))
		{
			if (AActor* EntityActor = EntityActorPtr->Get())
			{
				if (UDataLayerEditorSubsystem* DataLayerSubsystem = UDataLayerEditorSubsystem::Get())
				{
					// Find Instance
					UDataLayerInstance* DataLayerInstance = nullptr;
					for (UDataLayerInstance* Instance : DataLayerSubsystem->GetAllDataLayers())
					{
						if (Instance->GetAsset() == TargetAct->AssociatedDataLayer)
						{
							DataLayerInstance = Instance;
							break;
						}
					}

					if (DataLayerInstance)
					{
						TArray<AActor*> ActorsToRemove;
						ActorsToRemove.Add(EntityActor);
						DataLayerSubsystem->RemoveActorsFromDataLayer(ActorsToRemove, DataLayerInstance);
					}
				}
			}
		}
	}

	Stage->RemoveEntityFromAct(EntityID, ActID);

	OnModelChanged.Broadcast();
	DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Removed Entity %d from Act %d"), EntityID, ActID));
	return true;
}

bool FStageEditorController::RemoveAllEntitiesFromAct(int32 ActID)
{
	AStage* Stage = GetActiveStage();
	if (!Stage) return false;

	// Find the Act
	FAct* TargetAct = Stage->Acts.FindByPredicate([ActID](const FAct& Act) {
		return Act.SUID.ActID == ActID;
	});

	if (!TargetAct)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Error: Act not found"));
		return false;
}

	int32 EntityCount = TargetAct->EntityStateOverrides.Num();
	if (EntityCount == 0)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Act has no Entitys to remove"));
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("RemoveAllEntitiesFromAct", "Remove All Entitys from Act"));
	Stage->Modify();

	// Remove from Data Layer if applicable
	if (TargetAct->AssociatedDataLayer)
	{
		if (UDataLayerEditorSubsystem* DataLayerSubsystem = UDataLayerEditorSubsystem::Get())
		{
			// Find Instance
			UDataLayerInstance* DataLayerInstance = nullptr;
			for (UDataLayerInstance* Instance : DataLayerSubsystem->GetAllDataLayers())
			{
				if (Instance->GetAsset() == TargetAct->AssociatedDataLayer)
				{
					DataLayerInstance = Instance;
					break;
				}
			}

			if (DataLayerInstance)
			{
				TArray<AActor*> ActorsToRemove;
				for (const auto& Pair : TargetAct->EntityStateOverrides)
				{
					int32 EntityID = Pair.Key;
					if (const TSoftObjectPtr<AActor>* EntityActorPtr = Stage->EntityRegistry.Find(EntityID))
					{
						if (AActor* EntityActor = EntityActorPtr->Get())
						{
							ActorsToRemove.Add(EntityActor);
						}
					}
				}

				if (ActorsToRemove.Num() > 0)
				{
					DataLayerSubsystem->RemoveActorsFromDataLayer(ActorsToRemove, DataLayerInstance);
				}
			}
		}
	}

	TargetAct->EntityStateOverrides.Empty();

	OnModelChanged.Broadcast();
	DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Removed %d Entitys from Act '%s'"), EntityCount, *TargetAct->DisplayName));
	return true;
}

bool FStageEditorController::UnregisterAllEntities(int32 EntityID)
{
	AStage* Stage = GetActiveStage();
	if (!Stage) return false;

	// Verify Entity exists
	if (!Stage->EntityRegistry.Contains(EntityID))
	{
		DebugHeader::ShowNotifyInfo(TEXT("Error: Entity not registered to Stage"));
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("UnregisterEntity", "Unregister Entity"));
	Stage->Modify();

	// Remove from DataLayers before unregistering
	if (IsWorldPartitionActive())
	{
		// Remove from Stage DataLayer
		RemoveEntityFromStageDataLayer(EntityID);

		// Remove from all Act DataLayers
		for (const FAct& Act : Stage->Acts)
		{
			if (Act.EntityStateOverrides.Contains(EntityID) && Act.AssociatedDataLayer)
			{
				RemoveEntityFromActDataLayer(EntityID, Act.SUID.ActID);
			}
		}
	}

	// Clear component registration state before removing from Stage
	if (TSoftObjectPtr<AActor>* SoftPtr = Stage->EntityRegistry.Find(EntityID))
	{
		if (AActor* EntityActor = SoftPtr->Get())
		{
			if (UStageEntityComponent* Comp = EntityActor->FindComponentByClass<UStageEntityComponent>())
			{
				Comp->ClearRegistrationState();
			}
		}
	}

	Stage->UnregisterEntity(EntityID);

	OnModelChanged.Broadcast();
	DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Unregistered Entity %d from Stage"), EntityID));
	return true;
}

bool FStageEditorController::UnregisterAllEntities()
{
	AStage* Stage = GetActiveStage();
	if (!Stage) return false;

	int32 EntityCount = Stage->EntityRegistry.Num();
	if (EntityCount == 0)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Stage has no registered Entitys"));
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("UnregisterAllEntities", "Unregister All Entitys"));
	Stage->Modify();

	// Remove all Entitys from DataLayers before clearing
	if (IsWorldPartitionActive())
	{
		for (const auto& Pair : Stage->EntityRegistry)
		{
			int32 EntityID = Pair.Key;
			// Remove from Stage DataLayer
			RemoveEntityFromStageDataLayer(EntityID);

			// Remove from all Act DataLayers
			for (const FAct& Act : Stage->Acts)
			{
				if (Act.EntityStateOverrides.Contains(EntityID) && Act.AssociatedDataLayer)
				{
					RemoveEntityFromActDataLayer(EntityID, Act.SUID.ActID);
				}
			}
		}
	}

	// Clear component registration state for all Entities
	for (const auto& Pair : Stage->EntityRegistry)
	{
		if (AActor* EntityActor = Pair.Value.Get())
		{
			if (UStageEntityComponent* Comp = EntityActor->FindComponentByClass<UStageEntityComponent>())
			{
				Comp->ClearRegistrationState();
			}
		}
	}

	// Clear EntityRegistry
	Stage->EntityRegistry.Empty();

	// Clear all Entitys from all Acts
	for (FAct& Act : Stage->Acts)
	{
		Act.EntityStateOverrides.Empty();
	}

	OnModelChanged.Broadcast();
	DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Unregistered %d Entitys from Stage"), EntityCount));
	return true;
}

//----------------------------------------------------------------
// Act Management
//----------------------------------------------------------------

bool FStageEditorController::DeleteAct(int32 ActID)
{
	AStage* Stage = GetActiveStage();
	if (!Stage) return false;

	// Prevent deleting Default Act (ID 0)
	if (ActID == 0)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Cannot delete Default Act"));
		return false;
	}

	// Find the Act to get its name for notification
	FAct* TargetAct = Stage->Acts.FindByPredicate([ActID](const FAct& Act) {
		return Act.SUID.ActID == ActID;
	});

	if (!TargetAct)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Error: Act not found"));
		return false;
	}

	FString ActName = TargetAct->DisplayName;

	const FScopedTransaction Transaction(LOCTEXT("DeleteAct", "Delete Act"));
	Stage->Modify();

	// Delete Act's DataLayer before removing the Act
	if (IsWorldPartitionActive() && TargetAct->AssociatedDataLayer)
	{
		DeleteDataLayerForAct(ActID);
	}

	Stage->RemoveAct(ActID);

	// Invalidate sync status cache for Stage's DataLayer
	if (Stage->StageDataLayerAsset)
	{
		FDataLayerSyncStatusCache::Get().InvalidateCache(Stage->StageDataLayerAsset);
	}

	OnModelChanged.Broadcast();
	DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Deleted Act '%s'"), *ActName));
	return true;
}


UBlueprint* FStageEditorController::CreateBlueprintAsset(UClass* ParentClass, const FString& BaseName, const FString& DefaultPath)
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// Create the factory with proper configuration
	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentClass;
	Factory->BlueprintType = BPTYPE_Normal;
	Factory->bSkipClassPicker = true;  // Skip the Parent Class selection dialog (class already selected)

	// Open the "Save Asset As" dialog
	UObject* NewAsset = AssetTools.CreateAssetWithDialog(BaseName, DefaultPath, UBlueprint::StaticClass(), Factory);

	UBlueprint* NewBlueprint = Cast<UBlueprint>(NewAsset);
	if (NewBlueprint)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Created new Blueprint: ") + NewBlueprint->GetName());
		// Open the editor for the new asset
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewBlueprint);
	}

	return NewBlueprint;  // Returns nullptr if user cancelled or creation failed
}

void FStageEditorController::BindEditorDelegates()
{
	if (GEngine)
	{
		GEngine->OnLevelActorAdded().AddRaw(this, &FStageEditorController::OnLevelActorAdded);
		GEngine->OnLevelActorDeleted().AddRaw(this, &FStageEditorController::OnLevelActorDeleted);
	}

	// Subscribe to DataLayer changes (for external rename detection)
	if (UDataLayerEditorSubsystem* DataLayerSubsystem = UDataLayerEditorSubsystem::Get())
	{
		DataLayerChangedHandle = DataLayerSubsystem->OnDataLayerChanged().AddRaw(
			this, &FStageEditorController::OnExternalDataLayerChanged);
	}
}

void FStageEditorController::UnbindEditorDelegates()
{
	if (GEngine)
	{
		GEngine->OnLevelActorAdded().RemoveAll(this);
		GEngine->OnLevelActorDeleted().RemoveAll(this);
	}

	// Unsubscribe from DataLayer changes
	if (DataLayerChangedHandle.IsValid())
	{
		if (UDataLayerEditorSubsystem* DataLayerSubsystem = UDataLayerEditorSubsystem::Get())
		{
			DataLayerSubsystem->OnDataLayerChanged().Remove(DataLayerChangedHandle);
		}
		DataLayerChangedHandle.Reset();
	}
}

void FStageEditorController::OnLevelActorAdded(AActor* InActor)
{
	if (InActor && InActor->IsA<AStage>())
	{
		// Filter out temporary actors during drag-and-drop preview
		// Only refresh when actor is fully placed and committed
		if (InActor->HasAnyFlags(RF_Transient) ||
			InActor->IsActorBeingDestroyed() ||
			!InActor->GetWorld() ||
			InActor->GetWorld()->WorldType != EWorldType::Editor)
		{
			return;
		}

		// CRITICAL: Skip if we're in the middle of Blueprint reconstruction
		// When a BP is recompiled, OnLevelActorAdded is called for the new instance,
		// but we should NOT treat it as a new Stage (it inherits data from old instance)
		if (GIsReconstructingBlueprintInstances)
		{
			UE_LOG(LogStageEditor, Log, TEXT("StageEditorController: Skipping OnLevelActorAdded during BP reconstruction for '%s'"),
				*InActor->GetActorLabel());
			return;
		}

		// Additional check: ensure actor has a valid label (not default temporary name)
		FString ActorLabel = InActor->GetActorLabel();
		if (ActorLabel.IsEmpty() || ActorLabel.Contains(TEXT("PLACEHOLDER")))
		{
			return;
		}

		AStage* NewStage = Cast<AStage>(InActor);
		if (NewStage)
		{
			// === Singleton Check (Per Blueprint Class) ===
			// Ensures only one Stage of the SAME Blueprint class exists per level.
			// Different Stage Blueprint classes can coexist (e.g., BP_Stage_Forest + BP_Stage_Castle).
			// Safe to run here because GIsReconstructingBlueprintInstances check above
			// prevents this from triggering during BP recompilation.
			UWorld* World = NewStage->GetWorld();
			UClass* NewStageClass = NewStage->GetClass();
			AStage* ExistingStage = nullptr;

			for (TActorIterator<AStage> It(World); It; ++It)
			{
				AStage* Other = *It;
				if (Other != NewStage && IsValid(Other) && !Other->IsPendingKillPending())
				{
					// Only conflict if same Blueprint class
					if (Other->GetClass() == NewStageClass)
					{
						ExistingStage = Other;
						break;
					}
				}
			}

			if (ExistingStage)
			{
				// Log the error
				UE_LOG(LogStageEditor, Error,
					TEXT("Stage Singleton Violation: Attempted to place a second instance of '%s' in level. "
					     "Existing instance: '%s'. Only one instance per Stage Blueprint class is allowed."),
					*NewStageClass->GetName(), *ExistingStage->GetActorLabel());

				// Show modal dialog to user
				DebugHeader::ShowMsgDialog(
					EAppMsgType::Ok,
					FString::Printf(TEXT("Only one instance of this Stage Blueprint is allowed per level!\n\n"
						"Blueprint: %s\n"
						"Existing instance: %s\n\n"
						"This Stage will be destroyed."),
						*NewStageClass->GetName(),
						*ExistingStage->GetActorLabel()),
					true);

				// Destroy the duplicate Stage
				World->DestroyActor(NewStage);
				return;
			}

			// Register Stage with Subsystem to get unique ID
			// PHASE 13 INTEGRATION: Use StageRegistryAsset for ID allocation when Registry exists
			UWorld* StageWorld = NewStage->GetWorld();
			UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();

			if (EditorSubsystem)
			{
				UStageRegistryAsset* Registry = EditorSubsystem->GetOrLoadRegistryAsset(StageWorld);

				if (!Registry) // No Registry → Skip auto-assignment, let Migration handle it
				{
					UE_LOG(LogStageEditor, Log, TEXT("StageEditorController: OnLevelActorAdded - No Registry exists, skipping auto-ID assignment for '%s' (StageID will remain 0)"),
						*NewStage->GetActorLabel());
					// Refresh UI to reflect the new Stage
					OnModelChanged.Broadcast();
					return;
				}

				// Registry exists → Use Registry for ID allocation (Phase 13 approach)
				// Check if this Stage is already registered
				if (NewStage->GetStageID() > 0 && Registry->IsStageIDRegistered(NewStage->GetStageID()))
				{
					// Already registered in Registry, skip
					UE_LOG(LogStageEditor, Log, TEXT("StageEditorController: Stage '%s' already registered with ID %d"),
						*NewStage->GetActorLabel(), NewStage->GetStageID());
				}
				else
				{
					// Phase 13.9: Three-branch logic based on SC availability and Registry lock status
					NewStage->Modify();

					// Determine which mode to use
					bool bShouldUseOfflineMode = false;
					FString OfflineReason;

					if (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
					{
						// Check SC availability first
						if (!EditorSubsystem->IsSourceControlEnabled())
						{
							bShouldUseOfflineMode = true;
							OfflineReason = TEXT("Source Control offline");
						}
						else
						{
							// SC is available, try to CheckOut Registry
							FString ErrorMessage;
							if (!EditorSubsystem->CheckOutToChangelist(Registry, ErrorMessage))
							{
								bShouldUseOfflineMode = true;
								OfflineReason = FString::Printf(TEXT("Registry locked (%s)"), *ErrorMessage);
							}
						}
					}

					if (bShouldUseOfflineMode)
					{
						// ======================== OFFLINE MODE ========================
						// Allocate temporary negative ID
						int32 TempID = AllocateTemporaryID();
						NewStage->SUID.StageID = TempID;
						NewStage->bPendingRegistration = true;

						UE_LOG(LogStageEditor, Warning,
							TEXT("StageEditorController: %s. Allocated temporary ID %d for Stage '%s'. Use [Reconcile Stages] when back online."),
							*OfflineReason, TempID, *NewStage->GetActorLabel());

						// Show user-friendly notification
						DebugHeader::ShowNotifyInfo(FString::Printf(
							TEXT("⚠ %s\nStage '%s' created with temporary ID %d.\nUse [Reconcile Stages] button when Source Control is back online."),
							*OfflineReason, *NewStage->GetActorLabel(), TempID));

						// Broadcast model change to update UI (show pending status)
						OnModelChanged.Broadcast();
					}
					else
					{
						// ======================== ONLINE MODE ========================
						// Allocate real positive ID via Registry
						int32 NewID = Registry->AllocateAndRegister(NewStage);
						if (NewID > 0)
						{
							NewStage->SUID.StageID = NewID;
							NewStage->bPendingRegistration = false; // Ensure flag is false for successfully registered Stages

							UE_LOG(LogStageEditor, Log, TEXT("StageEditorController: Registered Stage '%s' with new ID %d via Registry"),
								*NewStage->GetActorLabel(), NewID);

							// MULTI-USER MODE: Save Registry immediately and open Changelist panel
							if (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
							{
								// Append change info to Changelist description
								EditorSubsystem->AppendStageChangeToChangelist(NewID, NewStage->GetActorLabel(), true);

								if (EditorSubsystem->SaveRegistryToDisk(Registry))
								{
									UE_LOG(LogStageEditor, Log, TEXT("StageEditorController: Saved Registry after Stage registration (Multi-user mode)"));

									// Open Changelist panel for user review
									EditorSubsystem->OpenChangelistPanel();
								}
								else
								{
									UE_LOG(LogStageEditor, Error, TEXT("StageEditorController: Failed to save Registry after Stage registration"));
								}
							}
							else
							{
								// Solo mode: Just mark dirty, user saves manually
								Registry->MarkPackageDirty();
							}

							// Broadcast model change to update UI
							OnModelChanged.Broadcast();
						}
						else
						{
							UE_LOG(LogStageEditor, Error, TEXT("StageEditorController: Failed to allocate ID for Stage '%s'"),
								*NewStage->GetActorLabel());
						}
					}
				}

				// Also register with runtime Subsystem for in-memory tracking
				if (UStageManagerSubsystem* Subsystem = GetSubsystem())
				{
					if (NewStage->GetStageID() > 0 && !Subsystem->GetStage(NewStage->GetStageID()))
					{
						Subsystem->RegisterStage(NewStage);
					}
				}
			}

			// Auto-create DataLayers if World Partition is active
			// IMPORTANT: Skip if in import bypass mode (DataLayerImporter will assign existing DataLayers)
			if (IsWorldPartitionActive() && !IsImportBypassActive())
			{
				// Create Stage's root DataLayer
				if (!NewStage->StageDataLayerAsset && NewStage->GetStageID() > 0)
				{
					CreateDataLayerForStage(NewStage);
					UE_LOG(LogStageEditor, Log, TEXT("StageEditorController: Created DataLayer for Stage '%s'"),
						*NewStage->GetActorLabel());
				}

				// Create DataLayer for Default Act (ActID == 1, as defined in Stage constructor)
				for (FAct& Act : NewStage->Acts)
				{
					if (Act.SUID.ActID == 1 && !Act.AssociatedDataLayer)
					{
						// Temporarily set this as active stage to use CreateDataLayerForAct
						TWeakObjectPtr<AStage> PrevActiveStage = ActiveStage;
						ActiveStage = NewStage;
						CreateDataLayerForAct(1);
						ActiveStage = PrevActiveStage;
						UE_LOG(LogStageEditor, Log, TEXT("StageEditorController: Created DataLayer for Default Act (ID=1)"));
						break;
					}
				}

				// Phase 13.8: Update metadata cache after DataLayer creation
				// Note: Metadata needs to be updated AFTER DataLayer is created
				// Note: Reuse World variable declared earlier at line 894 to avoid C4456 warning
				if (UStageEditorSubsystem* EditorSub = GEditor->GetEditorSubsystem<UStageEditorSubsystem>())
				{
					if (World) // World already declared at line 894
					{
						if (UStageManagerSubsystem* RuntimeSub = World->GetSubsystem<UStageManagerSubsystem>())
						{
							FStageMetadata Metadata;
							Metadata.StageID = NewStage->GetStageID();
							Metadata.StageName = NewStage->GetStageName();
							Metadata.StageDataLayerAsset = TSoftObjectPtr<UDataLayerAsset>(NewStage->StageDataLayerAsset);
							Metadata.OwnerLevel = TSoftObjectPtr<UWorld>(World);

							// Add Act DataLayers to metadata (only the Default Act at this point)
							for (const FAct& Act : NewStage->Acts)
							{
								if (Act.AssociatedDataLayer)
								{
									Metadata.ActDataLayers.Add(Act.SUID.ActID, TSoftObjectPtr<UDataLayerAsset>(Act.AssociatedDataLayer));
								}
							}

							RuntimeSub->UpdateStageMetadata(Metadata.StageID, Metadata);

							UE_LOG(LogStageEditor, Log, TEXT("StageEditorController: Updated metadata cache for Stage '%s' (ID:%d) with %d Act DataLayers"),
								*Metadata.StageName, Metadata.StageID, Metadata.ActDataLayers.Num());
						}
					}
				}
			}
			else if (IsImportBypassActive())
			{
				UE_LOG(LogStageEditor, Log, TEXT("StageEditorController: Skipping auto DataLayer creation (import bypass active) for Stage '%s'"),
					*NewStage->GetActorLabel());
			}
		}

		FindStageInWorld();
	}
}

void FStageEditorController::OnLevelActorDeleted(AActor* InActor)
{
	if (!InActor) return;

	// Check if it's a Stage actor
	if (InActor->IsA<AStage>())
	{
		// Filter out temporary actors (similar to OnLevelActorAdded)
		if (InActor->HasAnyFlags(RF_Transient) ||
			!InActor->GetWorld() ||
			InActor->GetWorld()->WorldType != EWorldType::Editor)
		{
			return;
		}

		// CRITICAL: Skip if we're in the middle of Blueprint reconstruction
		// When a BP is recompiled, OnLevelActorDeleted is called for the old instance
		if (GIsReconstructingBlueprintInstances)
		{
			UE_LOG(LogStageEditor, Log, TEXT("Skipping OnLevelActorDeleted during BP reconstruction for '%s'"),
				*InActor->GetActorLabel());
			return;
		}

		// NOTE: Stage DataLayer deletion is now handled explicitly via UI Delete button
		// When user directly deletes Stage Actor in scene, only the Actor is deleted,
		// DataLayers and registered Entities are kept (Entities become orphaned).
		// Use "Clean Orphaned Entities" button to clean up orphaned Entities.
		// Use UI Delete button for Stage to get confirmation dialog and delete DataLayers.

		AStage* Stage = Cast<AStage>(InActor);
		int32 StageID = Stage ? Stage->GetStageID() : -1;
		FString StageName = InActor->GetActorLabel();

		// Auto-unregister from Registry so orphaned entries don't accumulate
		if (StageID > 0)
		{
			if (UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>())
			{
				UWorld* StageWorld = InActor->GetWorld();
				if (UStageRegistryAsset* Registry = EditorSubsystem->GetOrLoadRegistryAsset(StageWorld))
				{
					bool bCanModify = true;
					if (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
					{
						FString ErrorMessage;
						if (!EditorSubsystem->CheckOutToChangelist(Registry, ErrorMessage))
						{
							UE_LOG(LogStageEditor, Warning,
								TEXT("Cannot checkout Registry to unregister deleted Stage '%s': %s"),
								*StageName, *ErrorMessage);
							bCanModify = false;
						}
					}

					if (bCanModify && Registry->Unregister(StageID))
					{
						UE_LOG(LogStageEditor, Log,
							TEXT("Auto-unregistered Stage '%s' (ID:%d) from Registry on level deletion"),
							*StageName, StageID);

						if (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
						{
							EditorSubsystem->AppendStageChangeToChangelist(StageID, StageName, false);
							EditorSubsystem->SaveRegistryToDisk(Registry);
						}
						else
						{
							Registry->MarkPackageDirty();
						}
					}
				}
			}
		}

		UE_LOG(LogStageEditor, Log, TEXT("Stage '%s' deleted (DataLayers and Entities kept). Use 'Clean Orphaned' to clean up."),
			*StageName);

		// Refresh UI to reflect Stage removal
		FindStageInWorld();
		return;
	}

	// Check if it's a Entity actor (has StageEntityComponent)
	if (UStageEntityComponent* EntityComp = InActor->FindComponentByClass<UStageEntityComponent>())
	{
		// Filter out temporary actors
		if (InActor->HasAnyFlags(RF_Transient) ||
			!InActor->GetWorld() ||
			InActor->GetWorld()->WorldType != EWorldType::Editor)
		{
			return;
		}

		// CRITICAL: Skip if we're in the middle of Blueprint reconstruction
		// Same reason as Stage - avoid unregistering during BP recompile
		if (GIsReconstructingBlueprintInstances)
		{
			UE_LOG(LogStageEditor, Log, TEXT("Skipping Entity auto-unregister during BP reconstruction for '%s'"),
				*InActor->GetActorLabel());
			return;
		}

		// Auto-unregister this Entity from all Stages
		int32 EntityID = EntityComp->GetEntityID();
		AStage* OwnerStage = EntityComp->OwnerStage.Get();

		if (OwnerStage && EntityID >= 0)
		{
			const FScopedTransaction Transaction(LOCTEXT("AutoUnregisterEntity", "Auto Unregister Entity"));
			OwnerStage->Modify();
			OwnerStage->UnregisterEntity(EntityID);
			
			// Refresh UI if this is the active stage
			if (OwnerStage == GetActiveStage())
			{
				OnModelChanged.Broadcast();
			}
			
			UE_LOG(LogStageEditor, Log, TEXT("StageEditor: Auto-unregistered Entity '%s' (ID:%d) from Stage '%s' due to level deletion"), 
				*InActor->GetName(), EntityID, *OwnerStage->GetName());
		}
	}
}

//----------------------------------------------------------------
// Data Layer Integration
//----------------------------------------------------------------

bool FStageEditorController::CreateDataLayerForAct(int32 ActID)
{
	AStage* Stage = GetActiveStage();
	if (!Stage) return false;

	FAct* TargetAct = nullptr;
	for (FAct& Act : Stage->Acts)
	{
		if (Act.SUID.ActID == ActID)
		{
			TargetAct = &Act;
			break;
		}
	}

	if (!TargetAct) return false;

	UDataLayerEditorSubsystem* DataLayerSubsystem = UDataLayerEditorSubsystem::Get();
	if (!DataLayerSubsystem) return false;

	const FScopedTransaction Transaction(LOCTEXT("CreateDataLayer", "Create Data Layer for Act"));
	Stage->Modify();

	// Create a new DataLayer asset using our helper function
	FString DataLayerName = FString::Printf(TEXT("DL_Act_%d_%d"), Stage->GetStageID(), ActID);
	UDataLayerAsset* NewDataLayerAsset = CreateDataLayerAsset(DataLayerName, DataLayerAssetFolderPath);

	if (NewDataLayerAsset)
	{
		// Create instance for the asset
		UDataLayerInstance* NewDataLayer = GetOrCreateInstanceForAsset(NewDataLayerAsset);
		if (NewDataLayer)
		{
			TargetAct->AssociatedDataLayer = NewDataLayerAsset;

			// Set Act DataLayer as child of Stage DataLayer
			UDataLayerInstance* StageDataLayerInstance = FindStageDataLayerInstance(Stage);
			if (StageDataLayerInstance)
			{
				DataLayerSubsystem->SetParentDataLayer(NewDataLayer, StageDataLayerInstance);
				UE_LOG(LogStageEditor, Log, TEXT("Set Act DataLayer '%s' as child of Stage DataLayer '%s'"),
					*DataLayerName, *Stage->StageDataLayerAsset->GetName());
			}

			// Invalidate sync status cache for both Stage and new Act DataLayers
			if (Stage->StageDataLayerAsset)
			{
				FDataLayerSyncStatusCache::Get().InvalidateCache(Stage->StageDataLayerAsset);
			}
			FDataLayerSyncStatusCache::Get().InvalidateCache(NewDataLayerAsset);

			OnModelChanged.Broadcast();
			DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Created Data Layer '%s' for Act"), *DataLayerName));
			return true;
		}
	}

	return false;
}

bool FStageEditorController::AssignDataLayerToAct(int32 ActID, UDataLayerAsset* DataLayer)
{
	AStage* Stage = GetActiveStage();
	if (!Stage) return false;

	FAct* TargetAct = nullptr;
	for (FAct& Act : Stage->Acts)
	{
		if (Act.SUID.ActID == ActID)
		{
			TargetAct = &Act;
			break;
		}
	}

	if (!TargetAct) return false;

	const FScopedTransaction Transaction(LOCTEXT("AssignDataLayer", "Assign Data Layer to Act"));
	Stage->Modify();

	TargetAct->AssociatedDataLayer = DataLayer;

	// Invalidate sync status cache for both Stage and Act DataLayers
	if (Stage->StageDataLayerAsset)
	{
		FDataLayerSyncStatusCache::Get().InvalidateCache(Stage->StageDataLayerAsset);
	}
	if (DataLayer)
	{
		FDataLayerSyncStatusCache::Get().InvalidateCache(DataLayer);
	}

	OnModelChanged.Broadcast();

	return true;
}

bool FStageEditorController::SyncEntityToDataLayer(int32 EntityID, int32 ActID)
{
	AStage* Stage = GetActiveStage();
	if (!Stage) return false;

	// Find Act
	FAct* TargetAct = nullptr;
	for (FAct& Act : Stage->Acts)
	{
		if (Act.SUID.ActID == ActID)
		{
			TargetAct = &Act;
			break;
		}
	}

	if (!TargetAct || !TargetAct->AssociatedDataLayer) return false;

	// Find Entity Actor
	AActor* EntityActor = nullptr;
	if (const TSoftObjectPtr<AActor>* EntityActorPtr = Stage->EntityRegistry.Find(EntityID))
	{
		EntityActor = EntityActorPtr->Get();
	}

	if (!EntityActor) return false;

	UDataLayerEditorSubsystem* DataLayerSubsystem = UDataLayerEditorSubsystem::Get();
	if (!DataLayerSubsystem) return false;

	// Add actor to data layer
	TArray<AActor*> ActorsToAdd;
	ActorsToAdd.Add(EntityActor);
	
	// We need the UDataLayerInstance, not just the Asset
	UDataLayerInstance* DataLayerInstance = nullptr;
	for (UDataLayerInstance* Instance : DataLayerSubsystem->GetAllDataLayers())
	{
		if (Instance->GetAsset() == TargetAct->AssociatedDataLayer)
		{
			DataLayerInstance = Instance;
			break;
		}
	}

	if (DataLayerInstance)
	{
		return DataLayerSubsystem->AddActorsToDataLayer(ActorsToAdd, DataLayerInstance);
	}

	return false;
}

//----------------------------------------------------------------
// Helper Functions
//----------------------------------------------------------------

UStageManagerSubsystem* FStageEditorController::GetSubsystem() const
{
	if (!GEditor) return nullptr;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	return World ? World->GetSubsystem<UStageManagerSubsystem>() : nullptr;
}

//----------------------------------------------------------------
// DataLayer Asset Management
//----------------------------------------------------------------

UDataLayerAsset* FStageEditorController::CreateDataLayerAsset(const FString& AssetName, const FString& FolderPath)
{
	FString PackagePath = FolderPath / AssetName;
	FString PackageName = FPackageName::ObjectPathToPackageName(PackagePath);

	// Check if asset already exists — prompt user for conflict resolution
	if (UDataLayerAsset* ExistingAsset = LoadObject<UDataLayerAsset>(nullptr, *PackagePath))
	{
		const FText Title = LOCTEXT("DataLayerNameConflict", "DataLayer Name Conflict");
		const FText Message = FText::Format(
			LOCTEXT("DataLayerNameConflictMsg", "DataLayer '{0}' already exists.\n\nYes = Reuse existing asset\nNo = Enter a new name\nCancel = Abort"),
			FText::FromString(AssetName));

		const EAppReturnType::Type Choice = FMessageDialog::Open(EAppMsgType::YesNoCancel, Message, Title);

		if (Choice == EAppReturnType::Yes)
		{
			UE_LOG(LogStageEditor, Log, TEXT("Reusing existing DataLayerAsset: %s"), *PackagePath);
			return ExistingAsset;
		}
		if (Choice == EAppReturnType::No)
		{
			const FString NewName = PromptForNewDataLayerName(AssetName);
			if (!NewName.IsEmpty() && NewName != AssetName)
			{
				return CreateDataLayerAsset(NewName, FolderPath);
			}
		}
		return nullptr;
	}

	// Create the package
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) return nullptr;

	// Create the DataLayerAsset
	UDataLayerAsset* NewAsset = NewObject<UDataLayerAsset>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!NewAsset) return nullptr;

#if WITH_EDITOR
	NewAsset->OnCreated();
#endif

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewAsset);

	// Save the package
	FString FilePath = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;

	if (UPackage::SavePackage(Package, NewAsset, *FilePath, SaveArgs))
	{
		// Mark for P4 add (UPackage::SavePackage bypasses UEditorEngine::Save auto-add pipeline)
		FText FailReason;
		SourceControlHelpers::CheckoutOrMarkForAdd(FilePath, FText(), nullptr, FailReason);

		UE_LOG(LogStageEditor, Log, TEXT("Created DataLayerAsset: %s"), *PackagePath);
		return NewAsset;
	}

	return nullptr;
}

FString FStageEditorController::PromptForNewDataLayerName(const FString& CurrentName)
{
	TSharedPtr<FString> ResultName = MakeShared<FString>(CurrentName);
	TSharedPtr<bool> bConfirmed = MakeShared<bool>(false);

	TSharedRef<SEditableTextBox> NameInput = SNew(SEditableTextBox)
		.Text(FText::FromString(CurrentName))
		.SelectAllTextWhenFocused(true)
		.OnTextCommitted_Lambda([ResultName](const FText& Text, ETextCommit::Type CommitType)
		{
			*ResultName = Text.ToString();
		});

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("PromptForNewDataLayerNameTitle", "Enter New DataLayer Name"))
		.ClientSize(FVector2D(420, 120))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.SizingRule(ESizingRule::Autosized);

	Window->SetContent(
		SNew(SBox)
		.Padding(10)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PromptForNewDataLayerNameLabel", "This name already exists. Enter a new name:"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				NameInput
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("OK", "OK"))
					.OnClicked_Lambda([Window, ResultName, bConfirmed]()
					{
						*ResultName = ResultName->TrimStartAndEnd();
						if (!ResultName->IsEmpty())
						{
							*bConfirmed = true;
							Window->RequestDestroyWindow();
						}
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.OnClicked_Lambda([Window]()
					{
						Window->RequestDestroyWindow();
						return FReply::Handled();
					})
				]
			]
		]);

	FSlateApplication::Get().AddModalWindow(Window, nullptr);

	return *bConfirmed ? *ResultName : FString();
}

UDataLayerInstance* FStageEditorController::GetOrCreateInstanceForAsset(UDataLayerAsset* Asset)
{
	if (!Asset) return nullptr;

	UDataLayerEditorSubsystem* DataLayerSubsystem = UDataLayerEditorSubsystem::Get();
	if (!DataLayerSubsystem) return nullptr;

	// Check if instance already exists
	const TArray<UDataLayerInstance*> AllInstances = DataLayerSubsystem->GetAllDataLayers();
	for (UDataLayerInstance* Instance : AllInstances)
	{
		if (Instance && Instance->GetAsset() == Asset)
		{
			return Instance;
		}
	}

	// Create new instance
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) return nullptr;

	// Check if level supports External Objects (required for DataLayer creation)
	ULevel* Level = World->PersistentLevel;
	if (!Level || !Level->IsUsingExternalObjects())
	{
		UE_LOG(LogStageEditor, Error, TEXT("Cannot create DataLayerInstance: Level does not support External Objects."));
		UE_LOG(LogStageEditor, Error, TEXT("   Please enable 'Use External Actors' in World Settings -> World Partition."));
		DebugHeader::ShowNotifyInfo(TEXT("Error: Level must have 'Use External Actors' enabled for DataLayer creation."));
		return nullptr;
	}

	AWorldDataLayers* WorldDataLayers = World->GetWorldDataLayers();
	if (!WorldDataLayers) return nullptr;

	FDataLayerCreationParameters Params;
	Params.DataLayerAsset = Asset;
	Params.WorldDataLayers = WorldDataLayers;

	UDataLayerInstance* NewInstance = DataLayerSubsystem->CreateDataLayerInstance(Params);
	if (NewInstance)
	{
		UE_LOG(LogStageEditor, Log, TEXT("Created DataLayerInstance for Asset: %s"), *Asset->GetName());
	}
	return NewInstance;
}

UDataLayerInstance* FStageEditorController::FindStageDataLayerInstance(AStage* Stage)
{
	if (!Stage || !Stage->StageDataLayerAsset) return nullptr;

	UDataLayerEditorSubsystem* DataLayerSubsystem = UDataLayerEditorSubsystem::Get();
	if (!DataLayerSubsystem) return nullptr;

	const TArray<UDataLayerInstance*> AllInstances = DataLayerSubsystem->GetAllDataLayers();
	for (UDataLayerInstance* Instance : AllInstances)
	{
		if (Instance && Instance->GetAsset() == Stage->StageDataLayerAsset)
		{
			return Instance;
		}
	}
	return nullptr;
}

bool FStageEditorController::CreateDataLayerForStage(AStage* Stage)
{
	if (!Stage || Stage->GetStageID() <= 0) return false;
	if (Stage->StageDataLayerAsset) return true; // Already has one

	FString AssetName = FString::Printf(TEXT("DL_Stage_%d"), Stage->GetStageID());
	UDataLayerAsset* StageDataLayerAsset = CreateDataLayerAsset(AssetName, DataLayerAssetFolderPath);
	if (!StageDataLayerAsset) return false;

	UDataLayerInstance* StageDataLayerInstance = GetOrCreateInstanceForAsset(StageDataLayerAsset);
	if (!StageDataLayerInstance) return false;

	const FScopedTransaction Transaction(LOCTEXT("CreateStageDataLayer", "Create Stage DataLayer"));
	Stage->Modify();
	Stage->StageDataLayerAsset = StageDataLayerAsset;
	Stage->StageDataLayerName = AssetName;

	UE_LOG(LogStageEditor, Log, TEXT("Created Stage DataLayer: %s"), *AssetName);
	return true;
}

bool FStageEditorController::DeleteDataLayerForAct(int32 ActID)
{
	AStage* Stage = GetActiveStage();
	if (!Stage) return false;

	FAct* TargetAct = nullptr;
	for (FAct& Act : Stage->Acts)
	{
		if (Act.SUID.ActID == ActID)
		{
			TargetAct = &Act;
			break;
		}
	}

	if (!TargetAct || !TargetAct->AssociatedDataLayer) return false;

	UDataLayerEditorSubsystem* DataLayerSubsystem = UDataLayerEditorSubsystem::Get();
	if (!DataLayerSubsystem) return false;

	// IMPORTANT: Create transaction BEFORE modifying anything for proper Undo/Redo support
	const FScopedTransaction Transaction(LOCTEXT("DeleteDataLayerForAct", "Delete DataLayer for Act"));
	Stage->Modify();

	// Find and delete the DataLayer Instance
	const TArray<UDataLayerInstance*> AllInstances = DataLayerSubsystem->GetAllDataLayers();
	for (UDataLayerInstance* Instance : AllInstances)
	{
		if (Instance && Instance->GetAsset() == TargetAct->AssociatedDataLayer)
		{
			DataLayerSubsystem->DeleteDataLayer(Instance);
			break;
		}
	}

	// Delete the DataLayer asset file from disk
	if (UDataLayerAsset* AssetToDelete = TargetAct->AssociatedDataLayer)
	{
		TArray<UObject*> ObjectsToDelete;
		ObjectsToDelete.Add(AssetToDelete);
		ObjectTools::ForceDeleteObjects(ObjectsToDelete, false);
	}

	TargetAct->AssociatedDataLayer = nullptr;

	return true;
}

bool FStageEditorController::AssignEntityToStageDataLayer(int32 EntityID)
{
	AStage* Stage = GetActiveStage();
	if (!Stage || !Stage->StageDataLayerAsset) return false;

	const TSoftObjectPtr<AActor>* EntityActorPtr = Stage->EntityRegistry.Find(EntityID);
	if (!EntityActorPtr || !EntityActorPtr->Get()) return false;

	AActor* EntityActor = EntityActorPtr->Get();

	UDataLayerEditorSubsystem* DataLayerSubsystem = UDataLayerEditorSubsystem::Get();
	if (!DataLayerSubsystem) return false;

	UDataLayerInstance* StageDataLayerInstance = FindStageDataLayerInstance(Stage);
	if (!StageDataLayerInstance) return false;

	TArray<AActor*> ActorsToAdd;
	ActorsToAdd.Add(EntityActor);
	return DataLayerSubsystem->AddActorsToDataLayer(ActorsToAdd, StageDataLayerInstance);
}

bool FStageEditorController::RemoveEntityFromStageDataLayer(int32 EntityID)
{
	AStage* Stage = GetActiveStage();
	if (!Stage || !Stage->StageDataLayerAsset) return false;

	const TSoftObjectPtr<AActor>* EntityActorPtr = Stage->EntityRegistry.Find(EntityID);
	if (!EntityActorPtr || !EntityActorPtr->Get()) return false;

	AActor* EntityActor = EntityActorPtr->Get();

	UDataLayerEditorSubsystem* DataLayerSubsystem = UDataLayerEditorSubsystem::Get();
	if (!DataLayerSubsystem) return false;

	UDataLayerInstance* StageDataLayerInstance = FindStageDataLayerInstance(Stage);
	if (!StageDataLayerInstance) return false;

	TArray<AActor*> ActorsToRemove;
	ActorsToRemove.Add(EntityActor);
	return DataLayerSubsystem->RemoveActorsFromDataLayer(ActorsToRemove, StageDataLayerInstance);
}

bool FStageEditorController::AssignEntityToActDataLayer(int32 EntityID, int32 ActID)
{
	return SyncEntityToDataLayer(EntityID, ActID);
}

bool FStageEditorController::RemoveEntityFromActDataLayer(int32 EntityID, int32 ActID)
{
	AStage* Stage = GetActiveStage();
	if (!Stage) return false;

	FAct* TargetAct = nullptr;
	for (FAct& Act : Stage->Acts)
	{
		if (Act.SUID.ActID == ActID)
		{
			TargetAct = &Act;
			break;
		}
	}

	if (!TargetAct || !TargetAct->AssociatedDataLayer) return false;

	const TSoftObjectPtr<AActor>* EntityActorPtr = Stage->EntityRegistry.Find(EntityID);
	if (!EntityActorPtr || !EntityActorPtr->Get()) return false;

	AActor* EntityActor = EntityActorPtr->Get();

	UDataLayerEditorSubsystem* DataLayerSubsystem = UDataLayerEditorSubsystem::Get();
	if (!DataLayerSubsystem) return false;

	UDataLayerInstance* DataLayerInstance = nullptr;
	const TArray<UDataLayerInstance*> AllInstances = DataLayerSubsystem->GetAllDataLayers();
	for (UDataLayerInstance* Instance : AllInstances)
	{
		if (Instance && Instance->GetAsset() == TargetAct->AssociatedDataLayer)
		{
			DataLayerInstance = Instance;
			break;
		}
	}

	if (!DataLayerInstance) return false;

	TArray<AActor*> ActorsToRemove;
	ActorsToRemove.Add(EntityActor);
	return DataLayerSubsystem->RemoveActorsFromDataLayer(ActorsToRemove, DataLayerInstance);
}

//----------------------------------------------------------------
// World Partition Support
//----------------------------------------------------------------

bool FStageEditorController::IsWorldPartitionActive() const
{
	if (UWorld* World = GEditor->GetEditorWorldContext().World())
	{
		return World->GetWorldPartition() != nullptr;
	}
	return false;
}

void FStageEditorController::ConvertToWorldPartition()
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Error: No active world found"));
		return;
	}

	// Check if already World Partition
	if (World->GetWorldPartition())
	{
		DebugHeader::ShowNotifyInfo(TEXT("Level is already a World Partition level"));
		return;
	}

	// Use UE native World Partition conversion
	FWorldPartitionEditorModule& WorldPartitionEditorModule =
		FModuleManager::LoadModuleChecked<FWorldPartitionEditorModule>("WorldPartitionEditor");

	FString LongPackageName = World->GetPackage()->GetName();

	// Call native conversion function (opens dialog)
	WorldPartitionEditorModule.ConvertMap(LongPackageName);
}

//----------------------------------------------------------------
// Phase 13.5: Multi-User Registry Sync
//----------------------------------------------------------------

FRegistrySyncStatus FStageEditorController::CalculateSyncStatus(UWorld* World, UStageRegistryAsset* Registry)
{
	FRegistrySyncStatus Status;

	if (!World || !Registry)
	{
		return Status;
	}

	// Collect all Stage IDs from Level
	TSet<int32> LevelStageIDs;
	TArray<AStage*> AllStages;

	for (TActorIterator<AStage> It(World); It; ++It)
	{
		AStage* Stage = *It;
		if (Stage && IsValid(Stage) && !Stage->IsPendingKillPending())
		{
			AllStages.Add(Stage);
			int32 StageID = Stage->GetStageID();
			if (StageID > 0)
			{
				LevelStageIDs.Add(StageID);
			}
			else if (StageID < 0)
			{
				// Phase 13.9: Temporary negative ID means offline-created, pending reconciliation
				Status.PendingReconciliation.Add(Stage);
			}
			else // StageID == 0
			{
				// ID=0 means pending assignment
				Status.PendingAssignment.Add(Stage);
			}
		}
	}

	// Find Registry entries without corresponding Level Actor (pending removal)
	const TMap<int32, FStageRegistryEntry>& RegisteredStages = Registry->GetAllStages();
	for (const auto& Pair : RegisteredStages)
	{
		int32 RegisteredID = Pair.Key;
		if (!LevelStageIDs.Contains(RegisteredID))
		{
			Status.PendingRemoval.Add(RegisteredID);
		}
	}

	// Phase 13.5: Check if Registry file is out of date with P4 (Multi-user mode only)
	if (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
	{
#if WITH_EDITOR
		ISourceControlModule& SCModule = FModuleManager::LoadModuleChecked<ISourceControlModule>("SourceControl");
		if (SCModule.IsEnabled() && SCModule.GetProvider().IsAvailable())
		{
			// Get Registry file path
			FString PackageFileName = FPackageName::LongPackageNameToFilename(
				Registry->GetPackage()->GetName(),
				FPackageName::GetAssetPackageExtension());

			ISourceControlProvider& Provider = SCModule.GetProvider();

			// Query file state from P4 (force update to get latest info)
			FSourceControlStatePtr FileState = Provider.GetState(PackageFileName, EStateCacheUsage::ForceUpdate);

			if (FileState.IsValid())
			{
				// Check if file is current (up-to-date with server)
				// IsCurrent() returns false if server has newer revision
				if (!FileState->IsCurrent())
				{
					// Registry file is out of date
					Status.bRegistryFileOutOfDate = true;
					Status.RegistryFileStatusMessage = TEXT("Server has newer version - Need to sync from P4!");

					UE_LOG(LogStageEditor, Warning,
						TEXT("CalculateSyncStatus: Registry file is OUT OF DATE! %s"),
						*Status.RegistryFileStatusMessage);
				}
				else
				{
					// File is up to date
					UE_LOG(LogStageEditor, Verbose,
						TEXT("CalculateSyncStatus: Registry file is up-to-date with P4"));
				}
			}
			else
			{
				UE_LOG(LogStageEditor, Warning,
					TEXT("CalculateSyncStatus: Failed to get P4 file state for Registry"));
			}
		}
		else
		{
			// SC is offline
			UE_LOG(LogStageEditor, Verbose,
				TEXT("CalculateSyncStatus: Source Control is offline, skipping file version check"));
		}
#endif
	}

	// Update cache
	CachedSyncStatus = Status;
	LastSyncStatusCalcTime = FPlatformTime::Seconds();

	return Status;
}

FRegistrySyncStatus FStageEditorController::GetCachedSyncStatus(UWorld* World, UStageRegistryAsset* Registry, bool bForceRefresh)
{
	if (!World || !Registry)
	{
		return FRegistrySyncStatus();
	}

	const double CurrentTime = FPlatformTime::Seconds();
	const bool bCacheExpired = (CurrentTime - LastSyncStatusCalcTime) >= SyncStatusCacheInterval;

	// Return cached result if still valid and not forcing refresh
	if (!bForceRefresh && !bCacheExpired)
	{
		UE_LOG(LogStageEditor, Verbose, TEXT("GetCachedSyncStatus: Using cached result (%.1fs old)"),
			CurrentTime - LastSyncStatusCalcTime);
		return CachedSyncStatus;
	}

	// Cache expired or force refresh - recalculate
	UE_LOG(LogStageEditor, Verbose, TEXT("GetCachedSyncStatus: Recalculating (cache %s)"),
		bForceRefresh ? TEXT("force refresh") : TEXT("expired"));

	return CalculateSyncStatus(World, Registry);
}

bool FStageEditorController::SyncRegistry()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Sync failed: Cannot get current World"));
		return false;
	}

	UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (!EditorSubsystem)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Sync failed: EditorSubsystem unavailable"));
		return false;
	}

	UStageRegistryAsset* Registry = EditorSubsystem->GetOrLoadRegistryAsset(World);
	if (!Registry)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Sync failed: Registry not found"));
		return false;
	}

	// Multi-user mode: Try to CheckOut
	if (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
	{
		FString ErrorMessage;
		if (!EditorSubsystem->CheckOutToChangelist(Registry, ErrorMessage))
		{
			DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Sync failed: Registry locked (%s)"), *ErrorMessage));
			return false;
		}
	}

	// Calculate what needs to be synced
	FRegistrySyncStatus SyncStatus = CalculateSyncStatus(World, Registry);

	if (SyncStatus.IsSynced())
	{
		DebugHeader::ShowNotifyInfo(TEXT("Registry already synced, no action needed"));
		return true;
	}

	int32 AssignedCount = 0;
	int32 RemovedCount = 0;

	// Assign IDs to pending Stages
	for (TWeakObjectPtr<AStage> StagePtr : SyncStatus.PendingAssignment)
	{
		if (AStage* Stage = StagePtr.Get())
		{
			Stage->Modify();
			int32 NewID = Registry->AllocateAndRegister(Stage);
			if (NewID > 0)
			{
				Stage->SUID.StageID = NewID;
				AssignedCount++;
				UE_LOG(LogStageEditor, Log, TEXT("SyncRegistry: Assigned ID %d to Stage '%s'"),
					NewID, *Stage->GetActorLabel());

				// Append change info to Changelist description (Multi-user mode)
				if (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
				{
					EditorSubsystem->AppendStageChangeToChangelist(NewID, Stage->GetActorLabel(), true);
				}

				// Also register with runtime Subsystem
				if (UStageManagerSubsystem* Subsystem = GetSubsystem())
				{
					if (!Subsystem->GetStage(NewID))
					{
						Subsystem->RegisterStage(Stage);
					}
				}
			}
		}
	}

	// Remove orphaned entries from Registry
	for (int32 OrphanedID : SyncStatus.PendingRemoval)
	{
		// Get Stage name from Registry before removal (for logging)
		FString StageName = FString::Printf(TEXT("Orphaned_%d"), OrphanedID);
		if (const FStageRegistryEntry* Entry = Registry->GetStageEntry(OrphanedID))
		{
			StageName = Entry->StageName;
		}

		if (Registry->Unregister(OrphanedID))
		{
			RemovedCount++;
			UE_LOG(LogStageEditor, Log, TEXT("SyncRegistry: Removed orphaned entry ID %d from Registry"), OrphanedID);

			// Append change info to Changelist description (Multi-user mode)
			if (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
			{
				EditorSubsystem->AppendStageChangeToChangelist(OrphanedID, StageName, false);
			}
		}
	}

	// Save Registry
	if (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
	{
		if (EditorSubsystem->SaveRegistryToDisk(Registry))
		{
			EditorSubsystem->OpenChangelistPanel();
		}
	}
	else
	{
		Registry->MarkPackageDirty();
	}

	// Show result
	DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Sync completed: Assigned %d IDs, removed %d orphaned entries"), AssignedCount, RemovedCount));

	// Refresh UI
	OnModelChanged.Broadcast();

	return true;
}

//----------------------------------------------------------------
// DataLayer Import Integration
//----------------------------------------------------------------

bool FStageEditorController::CanImportDataLayer(UDataLayerAsset* DataLayerAsset, FText& OutReason)
{
	if (!DataLayerAsset)
	{
		OutReason = LOCTEXT("ErrorNullAsset", "DataLayerAsset is null");
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		OutReason = LOCTEXT("ErrorNoWorld", "No valid World found");
		return false;
	}

	// Check naming convention
	FDataLayerNameParseResult ParseResult = FStageDataLayerNameParser::Parse(DataLayerAsset->GetName());
	if (!ParseResult.bIsValid)
	{
		OutReason = LOCTEXT("ErrorInvalidNaming", "DataLayer name does not follow naming convention (DL_Stage_*)");
		return false;
	}

	if (!ParseResult.bIsStageLayer)
	{
		OutReason = LOCTEXT("ErrorNotStageLayer", "Only Stage-level DataLayers (DL_Stage_*) can be imported. Select the parent Stage DataLayer.");
		return false;
	}

	// Check if already imported
	if (UStageManagerSubsystem* Subsystem = GetSubsystem())
	{
		if (Subsystem->IsDataLayerImported(DataLayerAsset))
		{
			OutReason = LOCTEXT("ErrorAlreadyImported", "This DataLayer has already been imported");
			return false;
		}
	}

	return true;
}

AStage* FStageEditorController::ImportStageFromDataLayer(UDataLayerAsset* StageDataLayerAsset, UWorld* World)
{
	// Default behavior: use first child DataLayer as DefaultAct (index 0)
	// Note: This legacy method uses AStage::StaticClass() for backward compatibility
	return ImportStageFromDataLayerWithDefaultAct(StageDataLayerAsset, 0, AStage::StaticClass(), World);
}

AStage* FStageEditorController::ImportStageFromDataLayerWithDefaultAct(UDataLayerAsset* StageDataLayerAsset, int32 SelectedDefaultActIndex, TSubclassOf<AStage> StageBlueprintClass, UWorld* World)
{
	// Validate
	FText ErrorMessage;
	if (!CanImportDataLayer(StageDataLayerAsset, ErrorMessage))
	{
		DebugHeader::ShowNotifyInfo(ErrorMessage.ToString());
		return nullptr;
	}

	if (!World)
	{
		World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}
	if (!World)
	{
		return nullptr;
	}

	// Parse Stage name
	FDataLayerNameParseResult StageParseResult = FStageDataLayerNameParser::Parse(StageDataLayerAsset->GetName());

	// Begin transaction
	FScopedTransaction Transaction(LOCTEXT("ImportDataLayerAsStage", "Import DataLayer as Stage"));

	// CRITICAL: Suppress automatic DataLayer creation in OnLevelActorAdded
	FScopedImportBypass ImportBypass;

	// Validate Blueprint class
	if (!StageBlueprintClass)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Error: Stage Blueprint Class is required for import"));
		return nullptr;
	}

	// 1. Create Stage Actor using user-provided Blueprint class (constructor creates empty DefaultAct at Acts[0])
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStage* NewStage = World->SpawnActor<AStage>(StageBlueprintClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!NewStage)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Failed to create Stage actor"));
		return nullptr;
	}

	NewStage->Modify();
	NewStage->StageName = StageParseResult.StageName;
	NewStage->SetActorLabel(StageParseResult.StageName);

	// 2. Assign SUID via Subsystem
	if (UStageManagerSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->RegisterStage(NewStage);
	}

	// 3. Associate Stage with DataLayer
	NewStage->StageDataLayerAsset = StageDataLayerAsset;
	NewStage->StageDataLayerName = StageDataLayerAsset->GetName();

	// 4. Collect all child DataLayers first
	struct FChildDataLayerInfo
	{
		UDataLayerAsset* Asset;
		FString DisplayName;
	};
	TArray<FChildDataLayerInfo> ChildDataLayers;

	UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(World);
	if (Manager)
	{
		Manager->ForEachDataLayerInstance([&](UDataLayerInstance* Instance)
		{
			if (!Instance || !Instance->GetParent())
			{
				return true; // Continue
			}

			if (Instance->GetParent()->GetAsset() != StageDataLayerAsset)
			{
				return true; // Not a child of this Stage
			}

			const UDataLayerAsset* ChildAsset = Instance->GetAsset();
			if (!ChildAsset)
			{
				return true;
			}

			// Parse naming to determine display name
			FDataLayerNameParseResult ActParseResult = FStageDataLayerNameParser::Parse(ChildAsset->GetName());

			FChildDataLayerInfo Info;
			Info.Asset = const_cast<UDataLayerAsset*>(ChildAsset);

			if (!ActParseResult.bIsValid)
			{
				Info.DisplayName = ChildAsset->GetName();
			}
			else if (ActParseResult.bIsStageLayer)
			{
				Info.DisplayName = ActParseResult.StageName;
			}
			else
			{
				Info.DisplayName = ActParseResult.ActName;
			}

			ChildDataLayers.Add(Info);
			return true; // Continue iteration
		});
	}

	// 5. Process DefaultAct and other Acts
	// DefaultAct (ID=1) is already at Acts[0] from constructor
	// We need to either update it with selected DataLayer or keep it empty

	if (ChildDataLayers.Num() > 0 && SelectedDefaultActIndex >= 0)
	{
		// Clamp index to valid range
		SelectedDefaultActIndex = FMath::Clamp(SelectedDefaultActIndex, 0, ChildDataLayers.Num() - 1);

		// Update DefaultAct (Acts[0]) with selected child DataLayer
		FChildDataLayerInfo& SelectedChild = ChildDataLayers[SelectedDefaultActIndex];
		NewStage->Acts[0].DisplayName = SelectedChild.DisplayName;
		NewStage->Acts[0].AssociatedDataLayer = SelectedChild.Asset;
		NewStage->Acts[0].SUID = FSUID::MakeActID(NewStage->SUID.StageID, 1); // ActID=1 for DefaultAct

		// Register Entitys for DefaultAct
		if (Manager)
		{
			const UDataLayerInstance* DefaultActInstance = Manager->GetDataLayerInstance(SelectedChild.Asset);
			if (DefaultActInstance)
			{
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					AActor* Actor = *It;
					if (Actor && Actor != NewStage && Actor->HasDataLayers())
					{
						TArray<const UDataLayerInstance*> ActorDataLayers = Actor->GetDataLayerInstances();
						if (ActorDataLayers.Contains(DefaultActInstance))
						{
							int32 EntityID = NewStage->RegisterEntity(Actor);
							if (EntityID >= 0)
							{
								NewStage->Acts[0].EntityStateOverrides.Add(EntityID, 0);
							}
						}
					}
				}
			}
		}

		// Add other child DataLayers as Acts (starting from ActID=2)
		int32 NextActID = 2;
		for (int32 i = 0; i < ChildDataLayers.Num(); ++i)
		{
			if (i == SelectedDefaultActIndex)
			{
				continue; // Skip the one we used for DefaultAct
			}

			FChildDataLayerInfo& ChildInfo = ChildDataLayers[i];

			FAct NewAct;
			NewAct.SUID = FSUID::MakeActID(NewStage->SUID.StageID, NextActID);
			NewAct.DisplayName = ChildInfo.DisplayName;
			NewAct.AssociatedDataLayer = ChildInfo.Asset;

			int32 NewActIndex = NewStage->Acts.Add(NewAct);

			// Register Entitys for this Act
			if (Manager)
			{
				const UDataLayerInstance* ActInstance = Manager->GetDataLayerInstance(ChildInfo.Asset);
				if (ActInstance)
				{
					for (TActorIterator<AActor> It(World); It; ++It)
					{
						AActor* Actor = *It;
						if (Actor && Actor != NewStage && Actor->HasDataLayers())
						{
							TArray<const UDataLayerInstance*> ActorDataLayers = Actor->GetDataLayerInstances();
							if (ActorDataLayers.Contains(ActInstance))
							{
								int32 EntityID = NewStage->RegisterEntity(Actor);
								if (EntityID >= 0)
								{
									NewStage->Acts[NewActIndex].EntityStateOverrides.Add(EntityID, 0);
								}
							}
						}
					}
				}
			}

			NextActID++;
		}
	}
	else if (ChildDataLayers.Num() > 0)
	{
		// SelectedDefaultActIndex == -1: Keep empty DefaultAct, add all children starting from ActID=2
		int32 NextActID = 2;
		for (FChildDataLayerInfo& ChildInfo : ChildDataLayers)
		{
			FAct NewAct;
			NewAct.SUID = FSUID::MakeActID(NewStage->SUID.StageID, NextActID);
			NewAct.DisplayName = ChildInfo.DisplayName;
			NewAct.AssociatedDataLayer = ChildInfo.Asset;

			int32 NewActIndex = NewStage->Acts.Add(NewAct);

			// Register Entitys for this Act
			if (Manager)
			{
				const UDataLayerInstance* ActInstance = Manager->GetDataLayerInstance(ChildInfo.Asset);
				if (ActInstance)
				{
					for (TActorIterator<AActor> It(World); It; ++It)
					{
						AActor* Actor = *It;
						if (Actor && Actor != NewStage && Actor->HasDataLayers())
						{
							TArray<const UDataLayerInstance*> ActorDataLayers = Actor->GetDataLayerInstances();
							if (ActorDataLayers.Contains(ActInstance))
							{
								int32 EntityID = NewStage->RegisterEntity(Actor);
								if (EntityID >= 0)
								{
									NewStage->Acts[NewActIndex].EntityStateOverrides.Add(EntityID, 0);
								}
							}
						}
					}
				}
			}

			NextActID++;
		}
	}
	// else: No child DataLayers - keep empty DefaultAct from constructor

	// Invalidate sync status cache
	FDataLayerSyncStatusCache::Get().InvalidateCache(StageDataLayerAsset);
	if (Manager)
	{
		Manager->ForEachDataLayerInstance([&](UDataLayerInstance* Instance)
		{
			if (Instance && Instance->GetAsset())
			{
				FDataLayerSyncStatusCache::Get().InvalidateCache(Instance->GetAsset());
			}
			return true;
		});
	}

	// Refresh controller state
	FindStageInWorld();
	OnModelChanged.Broadcast();

	DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Imported Stage '%s' with %d Acts"),
		*StageParseResult.StageName, NewStage->Acts.Num() - 1)); // -1 because Acts[0] is DefaultAct

	return NewStage;
}

//----------------------------------------------------------------
// DataLayer Rename
//----------------------------------------------------------------

bool FStageEditorController::RenameStageDataLayer(AStage* Stage, const FString& NewAssetName)
{
	if (!Stage || !Stage->StageDataLayerAsset)
	{
		return false;
	}

	UDataLayerAsset* Asset = Stage->StageDataLayerAsset;
	FString OldName = Asset->GetName();

	if (OldName == NewAssetName)
	{
		return true; // No change needed
	}

	// Parse old and new Stage names for cascade update
	FDataLayerNameParseResult OldParseResult = FStageDataLayerNameParser::Parse(OldName);
	FDataLayerNameParseResult NewParseResult = FStageDataLayerNameParser::Parse(NewAssetName);

	FString OldStageName = OldParseResult.bIsValid ? OldParseResult.StageName : TEXT("");
	FString NewStageName = NewParseResult.bIsValid ? NewParseResult.StageName : TEXT("");

	// Perform asset rename for Stage DataLayer
	TArray<FAssetRenameData> RenameData;
	FString PackagePath = FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName());
	RenameData.Add(FAssetRenameData(Asset, PackagePath, NewAssetName));

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	bool bSuccess = AssetToolsModule.Get().RenameAssets(RenameData);

	if (bSuccess)
	{
		// Update Stage's cached name
		const FScopedTransaction Transaction(LOCTEXT("RenameStageDataLayer", "Rename Stage DataLayer"));
		Stage->Modify();
		Stage->StageDataLayerName = NewAssetName;

		// Invalidate cache
		FDataLayerSyncStatusCache::Get().InvalidateCache(Asset);

		// Broadcast
		OnDataLayerRenamed.Broadcast(Asset, NewAssetName);

		UE_LOG(LogStageEditor, Log, TEXT("Renamed Stage DataLayer from '%s' to '%s'"), *OldName, *NewAssetName);

		// Cascade update: rename all child Act DataLayers if StageName changed
		if (!OldStageName.IsEmpty() && !NewStageName.IsEmpty() && OldStageName != NewStageName)
		{
			int32 CascadeCount = 0;
			for (FAct& Act : Stage->Acts)
			{
				if (!Act.AssociatedDataLayer)
				{
					continue;
				}

				UDataLayerAsset* ActAsset = Act.AssociatedDataLayer;
				FString ActOldName = ActAsset->GetName();

				// Parse Act DataLayer name
				FDataLayerNameParseResult ActParseResult = FStageDataLayerNameParser::Parse(ActOldName);
				if (!ActParseResult.bIsValid || ActParseResult.bIsStageLayer)
				{
					continue; // Skip if not a valid Act DataLayer
				}

				// Check if this Act's StageName matches the old Stage name
				if (ActParseResult.StageName != OldStageName)
				{
					continue; // Skip if StageName doesn't match (shouldn't happen normally)
				}

				// Build new Act DataLayer name: DL_Act_<NewStageName>_<ActName>
				FString ActNewName = FString::Printf(TEXT("DL_Act_%s_%s"), *NewStageName, *ActParseResult.ActName);

				// Rename Act DataLayer
				TArray<FAssetRenameData> ActRenameData;
				FString ActPackagePath = FPackageName::GetLongPackagePath(ActAsset->GetOutermost()->GetName());
				ActRenameData.Add(FAssetRenameData(ActAsset, ActPackagePath, ActNewName));

				if (AssetToolsModule.Get().RenameAssets(ActRenameData))
				{
					FDataLayerSyncStatusCache::Get().InvalidateCache(ActAsset);
					OnDataLayerRenamed.Broadcast(ActAsset, ActNewName);
					CascadeCount++;

					UE_LOG(LogStageEditor, Log, TEXT("  Cascade renamed Act DataLayer from '%s' to '%s'"), *ActOldName, *ActNewName);
				}
				else
				{
					UE_LOG(LogStageEditor, Warning, TEXT("  Failed to cascade rename Act DataLayer '%s'"), *ActOldName);
				}
			}

			if (CascadeCount > 0)
			{
				UE_LOG(LogStageEditor, Log, TEXT("Cascade renamed %d Act DataLayer(s) to use new StageName '%s'"), CascadeCount, *NewStageName);
			}
		}

		OnModelChanged.Broadcast();
	}

	return bSuccess;
}

bool FStageEditorController::RenameActDataLayer(AStage* Stage, int32 ActID, const FString& NewAssetName)
{
	if (!Stage)
	{
		return false;
	}

	// Find the Act
	FAct* TargetAct = nullptr;
	for (FAct& Act : Stage->Acts)
	{
		if (Act.SUID.ActID == ActID)
		{
			TargetAct = &Act;
			break;
		}
	}

	if (!TargetAct || !TargetAct->AssociatedDataLayer)
	{
		return false;
	}

	UDataLayerAsset* Asset = TargetAct->AssociatedDataLayer;
	FString OldName = Asset->GetName();

	if (OldName == NewAssetName)
	{
		return true; // No change needed
	}

	// Perform asset rename
	TArray<FAssetRenameData> RenameData;
	FString PackagePath = FPackageName::GetLongPackagePath(Asset->GetOutermost()->GetName());
	RenameData.Add(FAssetRenameData(Asset, PackagePath, NewAssetName));

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	bool bSuccess = AssetToolsModule.Get().RenameAssets(RenameData);

	if (bSuccess)
	{
		// Invalidate cache
		FDataLayerSyncStatusCache::Get().InvalidateCache(Asset);

		// Broadcast
		OnDataLayerRenamed.Broadcast(Asset, NewAssetName);
		OnModelChanged.Broadcast();

		UE_LOG(LogStageEditor, Log, TEXT("Renamed Act DataLayer from '%s' to '%s'"), *OldName, *NewAssetName);
	}

	return bSuccess;
}

AStage* FStageEditorController::FindStageByDataLayer(UDataLayerAsset* DataLayerAsset)
{
	if (!DataLayerAsset)
	{
		return nullptr;
	}

	// Check all found stages
	for (const TWeakObjectPtr<AStage>& StagePtr : FoundStages)
	{
		AStage* Stage = StagePtr.Get();
		if (!Stage)
		{
			continue;
		}

		// Check Stage's root DataLayer
		if (Stage->StageDataLayerAsset == DataLayerAsset)
		{
			return Stage;
		}

		// Check Act DataLayers
		for (const FAct& Act : Stage->Acts)
		{
			if (Act.AssociatedDataLayer == DataLayerAsset)
			{
				return Stage;
			}
		}
	}

	return nullptr;
}

int32 FStageEditorController::FindActIDByDataLayer(AStage* Stage, UDataLayerAsset* DataLayerAsset)
{
	if (!Stage || !DataLayerAsset)
	{
		return -1;
	}

	for (const FAct& Act : Stage->Acts)
	{
		if (Act.AssociatedDataLayer == DataLayerAsset)
		{
			return Act.SUID.ActID;
		}
	}

	return -1;
}

//----------------------------------------------------------------
// External DataLayer Change Handler (Phase 9.3)
//----------------------------------------------------------------

void FStageEditorController::OnExternalDataLayerChanged(
	const EDataLayerAction Action,
	const TWeakObjectPtr<const UDataLayerInstance>& ChangedDataLayer,
	const FName& ChangedProperty)
{
	// Only handle rename actions
	if (Action != EDataLayerAction::Rename)
	{
		return;
	}

	const UDataLayerInstance* Instance = ChangedDataLayer.Get();
	if (!Instance)
	{
		return;
	}

	UDataLayerAsset* Asset = const_cast<UDataLayerAsset*>(Instance->GetAsset());
	if (!Asset)
	{
		return;
	}

	// Check if this DataLayer belongs to any of our Stages
	AStage* OwnerStage = FindStageByDataLayer(Asset);
	if (!OwnerStage)
	{
		// Not an imported DataLayer, nothing to sync
		return;
	}

	FString NewName = Asset->GetName();

	// Check if it's the Stage's root DataLayer
	if (OwnerStage->StageDataLayerAsset == Asset)
	{
		// Sync StageDataLayerName
		if (OwnerStage->StageDataLayerName != NewName)
		{
			const FScopedTransaction Transaction(LOCTEXT("SyncStageDataLayerName", "Sync Stage DataLayer Name"));
			OwnerStage->Modify();
			OwnerStage->StageDataLayerName = NewName;

			UE_LOG(LogStageEditor, Log, TEXT("External rename detected: Synced Stage '%s' DataLayer name to '%s'"),
				*OwnerStage->GetActorLabel(), *NewName);
		}
	}

	// Invalidate cache for this DataLayer
	FDataLayerSyncStatusCache::Get().InvalidateCache(Asset);

	// Broadcast changes
	OnDataLayerRenamed.Broadcast(Asset, NewName);
	OnModelChanged.Broadcast();
}

//----------------------------------------------------------------
// Orphaned Entity Management
//----------------------------------------------------------------

int32 FStageEditorController::CleanOrphanedEntities(UWorld* World)
{
	if (!World)
	{
		World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}
	if (!World) return 0;

	const FScopedTransaction Transaction(LOCTEXT("CleanOrphanedEntities", "Clean Orphaned Entities"));

	int32 CleanedCount = 0;
	TArray<FString> CleanedEntityNames;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsTemplate()) continue;

		UStageEntityComponent* EntityComp = Actor->FindComponentByClass<UStageEntityComponent>();
		if (EntityComp && EntityComp->IsOrphaned())
		{
			EntityComp->ClearOrphanedState();
			CleanedCount++;
			CleanedEntityNames.Add(Actor->GetActorLabel());
		}
	}

	if (CleanedCount > 0)
	{
		UE_LOG(LogStageEditor, Log, TEXT("Cleaned %d orphaned Entities: %s"),
			CleanedCount, *FString::Join(CleanedEntityNames, TEXT(", ")));

		DebugHeader::ShowNotifyInfo(
			FString::Printf(TEXT("Cleaned %d orphaned Entity/Entities"), CleanedCount));
	}
	else
	{
		DebugHeader::ShowNotifyInfo(TEXT("No orphaned Entities found"));
	}

	return CleanedCount;
}

bool FStageEditorController::IsEntityRegisteredToOtherStage(AActor* Actor, AStage* CurrentStage, AStage*& OutOwnerStage)
{
	if (!Actor) return false;

	UStageEntityComponent* EntityComp = Actor->FindComponentByClass<UStageEntityComponent>();
	if (!EntityComp) return false;

	AStage* OwnerStage = EntityComp->OwnerStage.Get();
	if (OwnerStage && OwnerStage != CurrentStage)
	{
		OutOwnerStage = OwnerStage;
		return true;
	}

	return false;
}

//----------------------------------------------------------------
// Stage Deletion
//----------------------------------------------------------------

bool FStageEditorController::DeleteStageWithConfirmation(AStage* Stage)
{
	if (!Stage) return false;

	// Count associated DataLayers
	int32 DataLayerCount = Stage->StageDataLayerAsset ? 1 : 0;
	int32 RegisteredEntityCount = Stage->EntityRegistry.Num();

	for (const FAct& Act : Stage->Acts)
	{
		if (Act.AssociatedDataLayer)
		{
			DataLayerCount++;
		}
	}

	// Build dialog message
	FText DialogMessage = FText::Format(
		LOCTEXT("DeleteStageDetailedMessage",
			"Stage '{0}' contains:\n"
			"  • {1} DataLayer(s)\n"
			"  • {2} registered Entity/Entities\n\n"
			"What would you like to do?\n\n"
			"Yes - Delete Stage + DataLayers:\n"
			"  Deletes the Stage Actor and all associated DataLayers.\n"
			"  Entities remain in scene (will become orphaned).\n\n"
			"No - Delete Stage Only:\n"
			"  Deletes only the Stage Actor.\n"
			"  Keeps all DataLayers and Entities (Entities become orphaned).\n\n"
			"Cancel - Abort Operation:\n"
			"  Cancels the deletion."),
		FText::FromString(Stage->GetActorLabel()),
		FText::AsNumber(DataLayerCount),
		FText::AsNumber(RegisteredEntityCount)
	);

	// Show dialog
	EAppReturnType::Type UserResponse = FMessageDialog::Open(
		EAppMsgType::YesNoCancel,
		DialogMessage,
		LOCTEXT("DeleteStageTitle", "Delete Stage?")
	);

	if (UserResponse == EAppReturnType::Cancel)
	{
		return false;
	}

	bool bDeleteDataLayers = (UserResponse == EAppReturnType::Yes);

	// Warn about orphaned Entities
	if (RegisteredEntityCount > 0)
	{
		UE_LOG(LogStageEditor, Warning,
			TEXT("Deleting Stage '%s' will leave %d Entity/Entities orphaned. Use 'Clean Orphaned Entities' to clear them."),
			*Stage->GetActorLabel(), RegisteredEntityCount);
	}

	return DeleteStage(Stage, bDeleteDataLayers);
}

bool FStageEditorController::DeleteStage(AStage* Stage, bool bDeleteDataLayers)
{
	if (!Stage) return false;

	const FScopedTransaction Transaction(LOCTEXT("DeleteStage", "Delete Stage"));

	// Delete DataLayers if requested
	if (bDeleteDataLayers && IsWorldPartitionActive())
	{
		UDataLayerEditorSubsystem* DataLayerSubsystem = UDataLayerEditorSubsystem::Get();
		if (DataLayerSubsystem)
		{
			const TArray<UDataLayerInstance*> AllInstances = DataLayerSubsystem->GetAllDataLayers();

			// Delete Act DataLayers
			for (const FAct& Act : Stage->Acts)
			{
				if (Act.AssociatedDataLayer)
				{
					for (UDataLayerInstance* Instance : AllInstances)
					{
						if (Instance && Instance->GetAsset() == Act.AssociatedDataLayer)
						{
							DataLayerSubsystem->DeleteDataLayer(Instance);
							UE_LOG(LogStageEditor, Log, TEXT("Deleted Act DataLayer '%s'"),
								*Act.AssociatedDataLayer->GetName());
							break;
						}
					}
				}
			}

			// Delete Stage DataLayer
			if (Stage->StageDataLayerAsset)
			{
				const TArray<UDataLayerInstance*> UpdatedInstances = DataLayerSubsystem->GetAllDataLayers();
				for (UDataLayerInstance* Instance : UpdatedInstances)
				{
					if (Instance && Instance->GetAsset() == Stage->StageDataLayerAsset)
					{
						DataLayerSubsystem->DeleteDataLayer(Instance);
						UE_LOG(LogStageEditor, Log, TEXT("Deleted Stage DataLayer '%s'"),
							*Stage->StageDataLayerAsset->GetName());
						break;
					}
				}
			}
		}
	}

	// PHASE 13: Unregister Stage from Registry before deletion
	int32 StageID = Stage->GetStageID();
	FString StageName = Stage->GetActorLabel();
	if (StageID > 0)
	{
		if (UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>())
		{
			UWorld* StageWorld = Stage->GetWorld();
			if (UStageRegistryAsset* Registry = EditorSubsystem->GetOrLoadRegistryAsset(StageWorld))
			{
				// Step 1: CheckOut Registry to dedicated Changelist (if Multi-user mode)
				bool bCanModify = true;
				bool bNeedOpenChangelistPanel = false;
				if (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
				{
					FString ErrorMessage;
					if (!EditorSubsystem->CheckOutToChangelist(Registry, ErrorMessage))
					{
						UE_LOG(LogStageEditor, Error,
							TEXT("Failed to check out Registry file: %s. Cannot delete Stage."), *ErrorMessage);
						DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Registry locked (%s). Stage deletion cancelled."), *ErrorMessage));
						bCanModify = false;
					}
					else
					{
						bNeedOpenChangelistPanel = true;
					}
				}

				// Step 2: Unregister from Registry
				if (bCanModify && Registry->Unregister(StageID))
				{
					UE_LOG(LogStageEditor, Log, TEXT("Unregistered Stage ID %d from Registry"), StageID);

					// Step 3: Save Registry and open Changelist panel (for Multi-user mode)
					if (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
					{
						// Append change info to Changelist description
						EditorSubsystem->AppendStageChangeToChangelist(StageID, StageName, false);

						// Save Registry to disk
						if (EditorSubsystem->SaveRegistryToDisk(Registry))
						{
							UE_LOG(LogStageEditor, Log, TEXT("Saved Registry after Stage deletion"));

							// Open Changelist panel for user review
							if (bNeedOpenChangelistPanel)
							{
								EditorSubsystem->OpenChangelistPanel();
							}
						}
						else
						{
							UE_LOG(LogStageEditor, Error, TEXT("Failed to save Registry after Stage deletion"));
						}
					}
					else
					{
						// Solo mode: Just mark dirty, user will save manually
						Registry->MarkPackageDirty();
					}
				}
			}
		}
	}

	// Delete the Stage Actor
	UWorld* World = Stage->GetWorld();
	if (World)
	{
		World->DestroyActor(Stage);
		UE_LOG(LogStageEditor, Log, TEXT("Deleted Stage '%s'%s"),
			*Stage->GetActorLabel(),
			bDeleteDataLayers ? TEXT(" with DataLayers") : TEXT(" (DataLayers kept)"));
	}

	// Refresh UI
	FindStageInWorld();
	OnModelChanged.Broadcast();

	return true;
}

//----------------------------------------------------------------
// Phase 13.9: Offline Editing Support
//----------------------------------------------------------------

int32 FStageEditorController::AllocateTemporaryID()
{
	// Static counter for temporary IDs
	// Starts at -1 and decrements: -1, -2, -3, ...
	static int32 TempIDCounter = 0;
	return --TempIDCounter;
}

bool FStageEditorController::ReconcilePendingStages()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Reconcile failed: Cannot get current World"));
		return false;
	}

	UStageEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UStageEditorSubsystem>();
	if (!EditorSubsystem)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Reconcile failed: EditorSubsystem unavailable"));
		return false;
	}

	UStageRegistryAsset* Registry = EditorSubsystem->GetOrLoadRegistryAsset(World);
	if (!Registry)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Reconcile failed: Registry not found"));
		return false;
	}

	// Step 1: Collect all Stages with temporary negative IDs
	TArray<AStage*> PendingStages;
	for (TActorIterator<AStage> It(World); It; ++It)
	{
		AStage* Stage = *It;
		if (Stage && Stage->GetStageID() < 0 && Stage->bPendingRegistration)
		{
			PendingStages.Add(Stage);
		}
	}

	if (PendingStages.Num() == 0)
	{
		DebugHeader::ShowNotifyInfo(TEXT("No pending Stages to reconcile"));
		return true;
	}

	UE_LOG(LogStageEditor, Log, TEXT("ReconcilePendingStages: Found %d Stages with temporary IDs"), PendingStages.Num());

	// Step 2: CheckOut Registry (Multi-user mode only)
	if (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
	{
		// Check SC availability first
		if (!EditorSubsystem->IsSourceControlEnabled())
		{
			DebugHeader::ShowNotifyInfo(TEXT("Reconcile failed: Source Control is offline. Please connect to P4 first."));
			return false;
		}

		// Try to CheckOut Registry
		FString ErrorMessage;
		if (!EditorSubsystem->CheckOutToChangelist(Registry, ErrorMessage))
		{
			DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Reconcile failed: Registry locked (%s)"), *ErrorMessage));
			return false;
		}
	}

	// Step 3: Build ID remapping table (OldTempID -> NewRealID)
	TMap<int32, int32> IDRemapTable;
	TArray<AStage*> SuccessfullyReconciledStages;

	for (AStage* Stage : PendingStages)
	{
		int32 OldTempID = Stage->GetStageID();

		// Allocate real positive ID via Registry
		Stage->Modify();
		int32 NewRealID = Registry->AllocateAndRegister(Stage);

		if (NewRealID > 0)
		{
			IDRemapTable.Add(OldTempID, NewRealID);
			SuccessfullyReconciledStages.Add(Stage);

			UE_LOG(LogStageEditor, Log, TEXT("ReconcilePendingStages: Allocated real ID %d for Stage '%s' (was temp ID %d)"),
				NewRealID, *Stage->GetActorLabel(), OldTempID);
		}
		else
		{
			UE_LOG(LogStageEditor, Error, TEXT("ReconcilePendingStages: Failed to allocate ID for Stage '%s' (temp ID %d)"),
				*Stage->GetActorLabel(), OldTempID);
		}
	}

	if (IDRemapTable.Num() == 0)
	{
		DebugHeader::ShowNotifyInfo(TEXT("Reconcile failed: Could not allocate any IDs"));
		return false;
	}

	// Step 4: Update all Stages with new IDs and clear pending flags
	for (AStage* Stage : SuccessfullyReconciledStages)
	{
		int32 OldTempID = Stage->GetStageID();
		int32 NewRealID = IDRemapTable[OldTempID];

		Stage->Modify();
		Stage->SUID.StageID = NewRealID;
		Stage->bPendingRegistration = false;

		// Step 5: Remap EntityID references in Act.EntityStateOverrides
		for (FAct& Act : Stage->Acts)
		{
			TMap<int32, int32> RemappedOverrides;
			for (const auto& Pair : Act.EntityStateOverrides)
			{
				int32 OldEntityID = Pair.Key;
				int32 StateValue = Pair.Value;

				// Check if EntityID contains a negative StageID that needs remapping
				// EntityID format: (StageID << 16) | LocalEntityID
				// Extract StageID from high 16 bits
				int32 EmbeddedStageID = (OldEntityID >> 16);
				int32 LocalEntityID = (OldEntityID & 0xFFFF);

				if (EmbeddedStageID < 0 && IDRemapTable.Contains(EmbeddedStageID))
				{
					// Remap to new EntityID
					int32 NewStageID = IDRemapTable[EmbeddedStageID];
					int32 NewEntityID = (NewStageID << 16) | LocalEntityID;

					RemappedOverrides.Add(NewEntityID, StateValue);

					UE_LOG(LogStageEditor, Verbose, TEXT("  Remapped EntityID: %d -> %d (Stage %d -> %d, Local %d)"),
						OldEntityID, NewEntityID, EmbeddedStageID, NewStageID, LocalEntityID);
				}
				else
				{
					// Keep original EntityID
					RemappedOverrides.Add(OldEntityID, StateValue);
				}
			}

			// Replace with remapped overrides
			Act.EntityStateOverrides = RemappedOverrides;
		}

		// Append change info to Changelist description (Multi-user mode)
		if (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
		{
			EditorSubsystem->AppendStageChangeToChangelist(NewRealID, Stage->GetActorLabel(), true);
		}
	}

	// Step 6: Save Registry and open Changelist panel
	if (Registry->GetCollaborationMode() == ECollaborationMode::Multi)
	{
		if (EditorSubsystem->SaveRegistryToDisk(Registry))
		{
			UE_LOG(LogStageEditor, Log, TEXT("ReconcilePendingStages: Saved Registry after reconciliation"));

			// Open Changelist panel for user review
			EditorSubsystem->OpenChangelistPanel();
		}
		else
		{
			UE_LOG(LogStageEditor, Error, TEXT("ReconcilePendingStages: Failed to save Registry"));
		}
	}
	else
	{
		// Solo mode: Just mark dirty
		Registry->MarkPackageDirty();
	}

	// Show result
	DebugHeader::ShowNotifyInfo(FString::Printf(TEXT("Reconciled %d Stage(s): Temporary IDs converted to real IDs"), SuccessfullyReconciledStages.Num()));

	// Refresh UI
	OnModelChanged.Broadcast();

	return true;
}

#undef LOCTEXT_NAMESPACE
