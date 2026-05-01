#include "Components/StageTriggerZoneComponent.h"
#include "Actors/Stage.h"

DEFINE_LOG_CATEGORY_STATIC(LogStageTriggerZone, Log, All);

UStageTriggerZoneComponent::UStageTriggerZoneComponent()
{
	// Stage zones use larger default extent
	BoxExtent = FVector(500.0f, 500.0f, 200.0f);

	// Visual settings - default to LoadZone color
	ShapeColor = FColor::Yellow;
	LineThickness = 10.0f;
}

#if WITH_EDITOR
bool UStageTriggerZoneComponent::CanEditChange(const FProperty* InProperty) const
{
	if (!Super::CanEditChange(InProperty))
	{
		return false;
	}

	// ZoneType should be read-only for built-in zones (created via CreateDefaultSubobject)
	// but editable for user-placed external zones
	if (InProperty && InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UStageTriggerZoneComponent, ZoneType))
	{
		// DefaultSubobject components are created by CreateDefaultSubobject in owner's constructor
		// These are the built-in zones that should have fixed ZoneType
		return !IsDefaultSubobject();
	}

	return true;
}
#endif

void UStageTriggerZoneComponent::BeginPlay()
{
	Super::BeginPlay();

	// Update visual color based on zone type
	if (ZoneType == EStageTriggerZoneType::LoadZone)
	{
		ShapeColor = FColor::Yellow;
	}
	else
	{
		ShapeColor = FColor::Green;
	}
}

void UStageTriggerZoneComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindFromStage();
	Super::EndPlay(EndPlayReason);
}

//----------------------------------------------------------------
// Stage Binding
//----------------------------------------------------------------

void UStageTriggerZoneComponent::BindToStage(AStage* Stage)
{
	if (!Stage)
	{
		UE_LOG(LogStageTriggerZone, Warning, TEXT("StageTriggerZone [%s]: Cannot bind to null Stage"), *GetName());
		return;
	}

	if (BoundStage.Get() == Stage)
	{
		// Already bound to this Stage
		return;
	}

	// Unbind from previous Stage if any
	if (BoundStage.IsValid())
	{
		UnbindFromStage();
	}

	BoundStage = Stage;

	UE_LOG(LogStageTriggerZone, Log, TEXT("StageTriggerZone [%s] (%s): Bound to Stage '%s'"),
		*GetName(),
		ZoneType == EStageTriggerZoneType::LoadZone ? TEXT("LoadZone") : TEXT("ActivateZone"),
		*Stage->GetName());
}

void UStageTriggerZoneComponent::UnbindFromStage()
{
	if (BoundStage.IsValid())
	{
		UE_LOG(LogStageTriggerZone, Log, TEXT("StageTriggerZone [%s]: Unbound from Stage '%s'"),
			*GetName(), *BoundStage->GetName());
		BoundStage.Reset();
	}
}

//----------------------------------------------------------------
// Overlap Event Overrides
//----------------------------------------------------------------

void UStageTriggerZoneComponent::HandleActorEnter(AActor* Actor)
{
	if (bUseCustomActions)
	{
		// Custom mode: user-configured PresetAction takes over
		UE_LOG(LogStageTriggerZone, Verbose, TEXT("StageTriggerZone [%s]: Custom mode enter - executing OnEnterAction"),
			*GetName());
		ExecutePresetAction(OnEnterAction);
	}
	else if (AStage* Stage = BoundStage.Get())
	{
		// Default (recommended) mode: hardcoded Stage state transition based on ZoneType
		UE_LOG(LogStageTriggerZone, Verbose, TEXT("StageTriggerZone [%s]: Default mode enter - forwarding to Stage '%s' (%s)"),
			*GetName(), *Stage->GetName(),
			ZoneType == EStageTriggerZoneType::LoadZone ? TEXT("LoadZone") : TEXT("ActivateZone"));

		Stage->HandleZoneBeginOverlap(this, Actor);
	}
}

void UStageTriggerZoneComponent::HandleActorExit(AActor* Actor)
{
	if (bUseCustomActions)
	{
		// Custom mode: user-configured PresetAction takes over
		UE_LOG(LogStageTriggerZone, Verbose, TEXT("StageTriggerZone [%s]: Custom mode exit - executing OnExitAction"),
			*GetName());
		ExecutePresetAction(OnExitAction);
	}
	else if (AStage* Stage = BoundStage.Get())
	{
		// Default (recommended) mode: hardcoded Stage state transition based on ZoneType
		UE_LOG(LogStageTriggerZone, Verbose, TEXT("StageTriggerZone [%s]: Default mode exit - forwarding to Stage '%s' (%s)"),
			*GetName(), *Stage->GetName(),
			ZoneType == EStageTriggerZoneType::LoadZone ? TEXT("LoadZone") : TEXT("ActivateZone"));

		Stage->HandleZoneEndOverlap(this, Actor);
	}
}

//----------------------------------------------------------------
// Preset Action Execution
//----------------------------------------------------------------

void UStageTriggerZoneComponent::ExecutePresetAction(ETriggerZoneDefaultAction Action)
{
	// Skip if Custom (user handles via Blueprint event)
	if (Action == ETriggerZoneDefaultAction::Custom)
	{
		return;
	}

	// Get Stage (prefer BoundStage, fallback to base class OwnerStage)
	AStage* Stage = BoundStage.Get();
	if (!Stage)
	{
		Stage = GetOwnerStage();
	}

	if (!Stage)
	{
		UE_LOG(LogStageTriggerZone, Warning, TEXT("StageTriggerZone [%s]: Cannot execute preset action - no Stage bound"),
			*GetName());
		return;
	}

	// Execute the appropriate action
	switch (Action)
	{
	case ETriggerZoneDefaultAction::LoadStage:
		UE_LOG(LogStageTriggerZone, Log, TEXT("StageTriggerZone [%s]: Executing preset action -> LoadStage()"),
			*GetName());
		Stage->LoadStage();
		break;

	case ETriggerZoneDefaultAction::ActivateStage:
		UE_LOG(LogStageTriggerZone, Log, TEXT("StageTriggerZone [%s]: Executing preset action -> ActivateStage()"),
			*GetName());
		Stage->ActivateStage();
		break;

	case ETriggerZoneDefaultAction::UnloadStage:
		UE_LOG(LogStageTriggerZone, Log, TEXT("StageTriggerZone [%s]: Executing preset action -> UnloadStage()"),
			*GetName());
		Stage->UnloadStage();
		break;

	default:
		break;
	}
}
