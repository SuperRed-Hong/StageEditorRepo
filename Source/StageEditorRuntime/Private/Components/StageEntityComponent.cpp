#include "Components/StageEntityComponent.h"
#include "Actors/Stage.h"

#if WITH_EDITOR
#include "Misc/MessageDialog.h"
#endif

UStageEntityComponent::UStageEntityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

#if WITH_EDITOR
void UStageEntityComponent::OnRegister()
{
	Super::OnRegister();

	// === Prevent adding StageEntityComponent to Stage actors ===
	// Stage actors cannot be Entities (nested Stage is dangerous and not allowed)
	if (AActor* Owner = GetOwner())
	{
		if (Owner->IsA<AStage>())
		{
			// Log the error
			UE_LOG(LogTemp, Error,
				TEXT("StageEntityComponent cannot be added to Stage actor '%s'! "
				     "Stage actors cannot be registered as Entities. This component will be destroyed."),
				*Owner->GetName());

			// Show warning dialog to user
			FMessageDialog::Open(
				EAppMsgType::Ok,
				FText::Format(
					NSLOCTEXT("StageEditor", "StageCannotBeEntity",
						"Cannot add StageEntityComponent to Stage actor!\n\n"
						"Stage: {0}\n\n"
						"Stage actors cannot be registered as Entities.\n"
						"This is a dangerous nested operation and is not allowed.\n\n"
						"The component will be removed."),
					FText::FromString(Owner->GetName())),
				NSLOCTEXT("StageEditor", "StageCannotBeEntityTitle", "Invalid Operation")
			);

			// Mark component for destruction
			DestroyComponent();
			return;
		}
	}
}

void UStageEntityComponent::PostEditImport()
{
	Super::PostEditImport();

	// When an Entity actor is duplicated (Ctrl+C/V), the component carries stale
	// SUID and OwnerStage from the original. Clear them so the copy starts as
	// an unregistered Entity, preventing data corruption when registered to a
	// different Stage.
	SUID.StageID = -1;
	SUID.EntityID = -1;
	OwnerStage = nullptr;
	EntityState = 0;
	PreviousEntityState = 0;

	UE_LOG(LogTemp, Log, TEXT("PostEditImport: Cleared stale registration data on duplicated Entity '%s'"),
		*GetOwner()->GetActorLabel());
}
#endif

void UStageEntityComponent::BeginPlay()
{
	Super::BeginPlay();

	// World Partition streaming support: Entity Actors load asynchronously after their DataLayer
	// activates, so ActivateAct's push-based SetEntityState has already missed this Entity.
	// Pull the effective state here so the Entity initializes correctly regardless of load timing.
	if (SUID.EntityID > 0)
	{
		if (AStage* Stage = GetOwnerStage())
		{
			// GetControllingActForEntity respects ActiveActIDs priority order (last = highest)
			if (Stage->GetControllingActForEntity(SUID.EntityID) != -1)
			{
				// GetEffectiveEntityState applies the full multi-Act arbitration algorithm
				const int32 EffectiveState = Stage->GetEffectiveEntityState(SUID.EntityID);
				SetEntityState(EffectiveState);
			}
		}
	}
}

void UStageEntityComponent::SetEntityState(int32 NewState, bool bForce)
{
	if (EntityState != NewState || bForce)
	{
		// Store previous state before updating
		PreviousEntityState = EntityState;
		EntityState = NewState;

		// Notify listeners (Blueprints)
		OnEntityStateChanged.Broadcast(EntityState, PreviousEntityState);

		// Broadcast to OwnerStage so Stage-level events fire
		if (AStage* Stage = GetOwnerStage())
		{
			Stage->OnStageEntityStateChanged.Broadcast(SUID.EntityID, PreviousEntityState, EntityState);
			Stage->ReceiveOnStageEntityStateChanged(SUID.EntityID, PreviousEntityState, EntityState);
		}

		UE_LOG(LogTemp, Verbose, TEXT("Entity Component [%s] (ID:%d) State Changed: %d -> %d"),
			   *GetOwner()->GetName(), SUID.EntityID, PreviousEntityState, EntityState);
	}
}

AStage* UStageEntityComponent::GetOwnerStage() const
{
	// Fast path: only return if already loaded in memory
	return OwnerStage.Get();
}

AStage* UStageEntityComponent::GetOwnerStageLoaded() const
{
	// Check if we have a valid soft reference first
	if (OwnerStage.IsNull())
	{
		return nullptr;
	}

	// Try fast path first
	if (AStage* Stage = OwnerStage.Get())
	{
		return Stage;
	}

	// Synchronously load if not in memory
	return OwnerStage.LoadSynchronous();
}

bool UStageEntityComponent::HasValidOwnerStageReference() const
{
	// Check if the soft reference path is valid (not null/empty)
	// This does NOT check if the Stage is loaded, only if the reference exists
	return !OwnerStage.IsNull();
}

void UStageEntityComponent::IncrementState()
{
	SetEntityState(EntityState + 1);
}

void UStageEntityComponent::DecrementState()
{
	SetEntityState(EntityState - 1);
}

void UStageEntityComponent::ToggleState(int32 StateA, int32 StateB)
{
	if (EntityState == StateA)
	{
		SetEntityState(StateB);
	}
	else
	{
		SetEntityState(StateA);
	}
}

bool UStageEntityComponent::IsRegisteredToStage() const
{
	// Must have a valid EntityID (> 0) AND a valid OwnerStage reference
	// Note: We check IsNull() not Get() - reference can be valid even if Stage isn't loaded
	return SUID.EntityID > 0 && !OwnerStage.IsNull();
}

bool UStageEntityComponent::IsOrphaned() const
{
	// Orphaned = Was registered (EntityID > 0) but OwnerStage reference is now invalid
	// This happens when the Stage actor was deleted from the level
	// Note: EntityID > 0 (not >= 0) because 0 means "never registered"
	return SUID.EntityID > 0 && OwnerStage.IsNull();
}

void UStageEntityComponent::ClearOrphanedState()
{
	if (!IsOrphaned())
	{
		return;
	}

	ClearRegistrationState();
}

void UStageEntityComponent::ClearRegistrationState()
{
#if WITH_EDITOR
	Modify();
#endif

	// Reset to unregistered state
	SUID.StageID = -1;
	SUID.EntityID = -1;
	OwnerStage = nullptr;
	EntityState = 0;
	PreviousEntityState = 0;

#if WITH_EDITOR
	UE_LOG(LogTemp, Log, TEXT("Cleared registration state for Entity '%s'"),
		*GetOwner()->GetActorLabel());
#else
	UE_LOG(LogTemp, Log, TEXT("Cleared registration state for Entity '%s'"),
		*GetOwner()->GetName());
#endif
}
