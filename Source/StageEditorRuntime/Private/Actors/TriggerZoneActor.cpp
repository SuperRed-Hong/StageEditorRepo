#include "Actors/TriggerZoneActor.h"
#include "Components/TriggerZoneComponentBase.h"
#include "Components/StageEntityComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogTriggerZoneActor, Log, All);

ATriggerZoneActor::ATriggerZoneActor()
{
	// Create the trigger zone component
	TriggerZoneComponent = CreateDefaultSubobject<UTriggerZoneComponentBase>(TEXT("TriggerZone"));
	RootComponent = TriggerZoneComponent;

	// Default EntityState to 1 (enabled)
	// This will be set Properly after EntityComponent is created in AEntity constructor
}

void ATriggerZoneActor::BeginPlay()
{
	Super::BeginPlay();

	// Bind to EntityState changes
	if (EntityComponent)
	{
		EntityComponent->OnEntityStateChanged.AddDynamic(this, &ATriggerZoneActor::OnEntityStateChangedHandler);

		// Set default EntityState to 1 (enabled) if still at 0
		// This ensures the zone is enabled by default
		if (EntityComponent->EntityState == 0)
		{
			EntityComponent->SetEntityState(1, false);
		}
	}

	// Initial sync
	SyncZoneEnabledState();

	UE_LOG(LogTriggerZoneActor, Log, TEXT("TriggerZoneActor [%s]: BeginPlay - EntityState: %d, ZoneEnabled: %s"),
		*GetName(),
		EntityComponent ? EntityComponent->EntityState : -1,
		IsZoneEnabled() ? TEXT("Yes") : TEXT("No"));
}

void ATriggerZoneActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unbind from EntityState changes
	if (EntityComponent)
	{
		EntityComponent->OnEntityStateChanged.RemoveDynamic(this, &ATriggerZoneActor::OnEntityStateChangedHandler);
	}

	Super::EndPlay(EndPlayReason);
}

//----------------------------------------------------------------
// Convenience Accessors
//----------------------------------------------------------------

bool ATriggerZoneActor::IsZoneEnabled() const
{
	if (TriggerZoneComponent)
	{
		return TriggerZoneComponent->IsZoneEnabled();
	}
	return false;
}

void ATriggerZoneActor::SetZoneEnabled(bool bEnabled)
{
	// Set EntityState which will trigger OnEntityStateChangedHandler
	if (EntityComponent)
	{
		EntityComponent->SetEntityState(bEnabled ? 1 : 0, false);
	}
}

//----------------------------------------------------------------
// EntityState Sync
//----------------------------------------------------------------

void ATriggerZoneActor::OnEntityStateChangedHandler(int32 NewState, int32 OldState)
{
	UE_LOG(LogTriggerZoneActor, Verbose, TEXT("TriggerZoneActor [%s]: EntityState changed %d → %d"),
		*GetName(), OldState, NewState);

	SyncZoneEnabledState();
}

void ATriggerZoneActor::SyncZoneEnabledState()
{
	if (!TriggerZoneComponent || !EntityComponent)
	{
		return;
	}

	// EntityState == 0 → Disabled, EntityState != 0 → Enabled
	const bool bShouldBeEnabled = EntityComponent->EntityState != 0;
	const bool bCurrentlyEnabled = TriggerZoneComponent->IsZoneEnabled();

	if (bShouldBeEnabled != bCurrentlyEnabled)
	{
		TriggerZoneComponent->SetZoneEnabled(bShouldBeEnabled);

		UE_LOG(LogTriggerZoneActor, Log, TEXT("TriggerZoneActor [%s]: Zone %s (EntityState: %d)"),
			*GetName(),
			bShouldBeEnabled ? TEXT("ENABLED") : TEXT("DISABLED"),
			EntityComponent->EntityState);
	}
}
