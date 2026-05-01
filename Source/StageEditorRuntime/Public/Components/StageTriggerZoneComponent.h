#pragma once

#include "CoreMinimal.h"
#include "Components/TriggerZoneComponentBase.h"
#include "StageTriggerZoneComponent.generated.h"

// Forward declarations
class AStage;

/**
 * @brief Defines the type/purpose of a Stage Trigger Zone.
 */
UENUM(BlueprintType)
enum class EStageTriggerZoneType : uint8
{
	/** Outer zone: Triggers Stage DataLayer loading (Unloaded → Preloading → Loaded) */
	LoadZone UMETA(DisplayName = "Load Zone"),

	/** Inner zone: Triggers Stage activation (Loaded → Active) */
	ActivateZone UMETA(DisplayName = "Activate Zone"),
};

/**
 * @brief A specialized TriggerZone component for Stage state management.
 *
 * This component extends TriggerZoneComponentBase with Stage-specific functionality:
 * - Zone type configuration (LoadZone / ActivateZone)
 * - Direct forwarding of overlap events to Stage for state transitions
 * - Automatic binding when used as Stage's built-in zones
 *
 * Inheritance from TriggerZoneComponentBase provides:
 * - OnActorEnter/OnActorExit delegates for Blueprint extensions
 * - Preset actions (LoadStage, ActivateStage, UnloadStage)
 * - Actor filtering (tags + Pawn requirement)
 * - Zone enable/disable state
 * - Description system for documentation
 *
 * Usage:
 * - Built-in zones are created by Stage actor in constructor (ZoneType locked)
 * - External zones can be placed separately and referenced by Stage (ZoneType editable)
 * - Stage calls BindToStage() to establish the connection
 *
 * Event Flow:
 * 1. Actor overlaps this component
 * 2. Base class filters and broadcasts OnActorEnter/OnActorExit
 * 3. This class additionally forwards to Stage->HandleZoneBeginOverlap/EndOverlap
 * 4. Stage updates its actor tracking sets and triggers state transitions
 *
 * @see UTriggerZoneComponentBase for base functionality
 * @see AStage for the Stage actor that owns these zones
 */
UCLASS(ClassGroup = (StageEditor), meta = (BlueprintSpawnableComponent, DisplayName = "Stage Trigger Zone"))
class STAGEEDITORRUNTIME_API UStageTriggerZoneComponent : public UTriggerZoneComponentBase
{
	GENERATED_BODY()

public:
	UStageTriggerZoneComponent();

#if WITH_EDITOR
	/**
	 * @brief Controls property editability based on component context.
	 * Built-in zones (created via CreateDefaultSubobject) have ZoneType locked.
	 * External zones (user-placed) can edit ZoneType freely.
	 */
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

	//----------------------------------------------------------------
	// Stage-Specific Configuration
	//----------------------------------------------------------------

	/**
	 * The type of this trigger zone.
	 * - LoadZone: Outer zone for preloading Stage DataLayer
	 * - ActivateZone: Inner zone for activating Stage
	 *
	 * NOTE: For built-in zones (created by Stage), this is read-only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StageEditor|PresetActions")
	EStageTriggerZoneType ZoneType = EStageTriggerZoneType::LoadZone;

	//----------------------------------------------------------------
	// Preset Actions (Stage-Specific)
	//----------------------------------------------------------------

	/**
	 * Master switch for Preset Action override.
	 *
	 * - false (default, recommended): zone uses the default Stage state transition
	 *   based on ZoneType (LoadZone -> Preloading, ActivateZone -> Active, etc.).
	 *   OnEnterAction / OnExitAction below are ignored.
	 * - true: skips the default behavior and uses OnEnterAction / OnExitAction
	 *   for custom control.
	 *
	 * Toggle this when you need to override the recommended default (e.g. an
	 * external zone that should immediately Activate without going through
	 * the LoadZone preload step).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StageEditor|PresetActions",
		meta = (DisplayName = "Use Custom Actions"))
	bool bUseCustomActions = false;

	/**
	 * Action to automatically perform when an actor enters this zone.
	 * Only applied when bUseCustomActions = true.
	 *
	 * - Custom: No auto action, use OnActorEnter event in Blueprint
	 * - Load Stage: Calls Stage->LoadStage()
	 * - Activate Stage: Calls Stage->ActivateStage()
	 * - Unload Stage: Calls Stage->UnloadStage()
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StageEditor|PresetActions",
		meta = (DisplayName = "On Enter Action", EditCondition = "bUseCustomActions"))
	ETriggerZoneDefaultAction OnEnterAction = ETriggerZoneDefaultAction::Custom;

	/**
	 * Action to automatically perform when an actor exits this zone.
	 * Only applied when bUseCustomActions = true.
	 *
	 * Typical use: Set to "Unload Stage" for exit zones.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "StageEditor|PresetActions",
		meta = (DisplayName = "On Exit Action", EditCondition = "bUseCustomActions"))
	ETriggerZoneDefaultAction OnExitAction = ETriggerZoneDefaultAction::Custom;

	//----------------------------------------------------------------
	// Stage Binding (Runtime)
	//----------------------------------------------------------------

	/**
	 * @brief Binds this zone to a Stage actor for direct event forwarding.
	 *
	 * This establishes a direct connection to the Stage, enabling:
	 * - Overlap events to be forwarded to Stage for state transitions
	 * - Stage to track which actors are in each zone
	 *
	 * For built-in zones, this is called automatically by Stage.
	 * For external zones, set OwnerStage in the editor instead.
	 *
	 * @param Stage The Stage to bind to.
	 */
	UFUNCTION(BlueprintCallable, Category = "StageEditor|PresetActions")
	void BindToStage(AStage* Stage);

	/**
	 * @brief Unbinds this zone from its current Stage.
	 * Stops forwarding overlap events to Stage.
	 */
	UFUNCTION(BlueprintCallable, Category = "StageEditor|PresetActions")
	void UnbindFromStage();

	/**
	 * @brief Gets the Stage this zone is bound to (for direct forwarding).
	 * @return The Stage actor, or nullptr if not bound.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "StageEditor|PresetActions")
	AStage* GetBoundStage() const { return BoundStage.Get(); }

	/**
	 * @brief Checks if this zone is bound to a Stage for direct forwarding.
	 * @return True if BoundStage is valid.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "StageEditor|PresetActions")
	bool IsBoundToStageForForwarding() const { return BoundStage.IsValid(); }

protected:
	//----------------------------------------------------------------
	// Lifecycle
	//----------------------------------------------------------------

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	//----------------------------------------------------------------
	// Overlap Event Overrides
	//----------------------------------------------------------------

	/**
	 * @brief Called when an actor enters the zone.
	 * Extends base class to also forward to Stage.
	 */
	virtual void HandleActorEnter(AActor* Actor);

	/**
	 * @brief Called when an actor exits the zone.
	 * Extends base class to also forward to Stage.
	 */
	virtual void HandleActorExit(AActor* Actor);

private:
	//----------------------------------------------------------------
	// Stage Binding (Direct Reference)
	//----------------------------------------------------------------

	/**
	 * Direct reference to Stage for event forwarding.
	 * This is separate from base class OwnerStage to support:
	 * - Built-in zones: Set by BindToStage() at runtime
	 * - External zones: Can use either BindToStage() or base class OwnerStage
	 *
	 * Transient because it's set at runtime, not serialized.
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<AStage> BoundStage;

	//----------------------------------------------------------------
	// Preset Action Execution
	//----------------------------------------------------------------

	/**
	 * Execute a preset action on the bound Stage.
	 * @param Action The action to execute.
	 */
	void ExecutePresetAction(ETriggerZoneDefaultAction Action);
};
