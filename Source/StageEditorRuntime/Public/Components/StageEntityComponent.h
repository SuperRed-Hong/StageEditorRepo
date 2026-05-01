#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/StageCoreTypes.h"
#include "StageEntityComponent.generated.h"

class AStage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEntityStateChanged, int32, NewState, int32, OldState);

/**
 * @brief Core component that makes any Actor a controllable Entity in the Stage system.
 * Can be added to any Actor to make it respond to Stage state changes.
 */
UCLASS(ClassGroup=(StageEditor), Blueprintable, meta=(BlueprintSpawnableComponent))
class STAGEEDITORRUNTIME_API UStageEntityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStageEntityComponent();

protected:
	virtual void BeginPlay() override;

#if WITH_EDITOR
	/**
	 * Called when component is registered.
	 * Used to prevent adding this component to Stage actors (nested Stage is not allowed).
	 */
	virtual void OnRegister() override;

	/**
	 * Called after this component is duplicated (e.g., Ctrl+C/V in editor).
	 * Clears stale registration data so the copy doesn't carry the original's Stage binding.
	 */
	virtual void PostEditImport() override;
#endif

public:	
	//----------------------------------------------------------------
	// Core Properties
	//----------------------------------------------------------------

	/** Stage Unique ID. Contains StageID and EntityID. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StageEditor", meta = (DisplayName = "SUID"))
	FSUID SUID;

	/**
	 * @brief Gets the Entity's unique ID within its Stage (convenience getter).
	 * @return The EntityID from SUID.EntityID
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "StageEditor")
	int32 GetEntityID() const { return SUID.EntityID; }

	/**
	 * @brief Gets the owning Stage's ID (convenience getter).
	 * @return The StageID from SUID.StageID
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "StageEditor")
	int32 GetOwnerStageID() const { return SUID.StageID; }

	/**
	 * Soft reference to the owning Stage Actor.
	 * Used to quickly access the Stage without lookup.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StageEditor")
	TSoftObjectPtr<AStage> OwnerStage;

	/**
	 * The current state of this Entity.
	 * Modified by the Stage Manager via SetEntityState().
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StageEditor")
	int32 EntityState = 0;

	/** The previous state before the last change. Useful for transition logic. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "StageEditor")
	int32 PreviousEntityState = 0;

	/** Event fired when EntityState changes. Implement logic here in Blueprints. */
	UPROPERTY(BlueprintAssignable, Category = "StageEditor")
	FOnEntityStateChanged OnEntityStateChanged;

	//----------------------------------------------------------------
	// State Control API
	//----------------------------------------------------------------

	/**
	 * @brief Sets the new state for this Entity.
	 * @param NewState The target state value.
	 * @param bForce If true, triggers update even if NewState == CurrentState.
	 */
	UFUNCTION(BlueprintCallable, Category = "StageEditor")
	void SetEntityState(int32 NewState, bool bForce = false);

	/**
	 * @brief Gets the current state of this Entity.
	 * @return The current EntityState value.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "StageEditor")
	int32 GetEntityState() const { return EntityState; }

	/**
	 * @brief Gets the previous state before the last change.
	 * Useful for implementing transition logic in Blueprints.
	 * @return The previous EntityState value.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "StageEditor")
	int32 GetPreviousEntityState() const { return PreviousEntityState; }

	/**
	 * @brief Increments the current state by 1.
	 * Useful for simple sequential state machines.
	 */
	UFUNCTION(BlueprintCallable, Category = "StageEditor")
	void IncrementState();

	/**
	 * @brief Decrements the current state by 1.
	 * Useful for simple sequential state machines.
	 */
	UFUNCTION(BlueprintCallable, Category = "StageEditor")
	void DecrementState();

	/**
	 * @brief Toggles between two state values.
	 * If current state equals StateA, switches to StateB, and vice versa.
	 * If current state is neither, switches to StateA.
	 * @param StateA First toggle state.
	 * @param StateB Second toggle state.
	 */
	UFUNCTION(BlueprintCallable, Category = "StageEditor")
	void ToggleState(int32 StateA, int32 StateB);

	//----------------------------------------------------------------
	// Stage Interaction API
	//----------------------------------------------------------------

	/**
	 * @brief Gets the owning Stage Actor if already loaded in memory.
	 * Fast path - does NOT load the Stage if not in memory.
	 * @return The owning Stage, or nullptr if not loaded or not registered.
	 * @see GetOwnerStageLoaded() for synchronous loading version.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "StageEditor",
		meta = (DisplayName = "Get Owner Stage (Fast)"))
	AStage* GetOwnerStage() const;

	/**
	 * @brief Gets the owning Stage Actor, loading it synchronously if needed.
	 * Use this when you need guaranteed access to the Stage.
	 * Warning: May cause hitches if Stage is in an unloaded streaming level.
	 * @return The owning Stage, or nullptr if not registered or Stage was deleted.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "StageEditor",
		meta = (DisplayName = "Get Owner Stage (Load)"))
	AStage* GetOwnerStageLoaded() const;

	/**
	 * @brief Checks if the OwnerStage soft reference points to a valid asset.
	 * Does NOT check if the Stage is currently loaded in memory.
	 * @return True if the soft reference path is valid (Stage exists on disk).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "StageEditor")
	bool HasValidOwnerStageReference() const;

	/**
	 * @brief Checks if this Entity is registered to a Stage.
	 * @return True if registered (has EntityID > 0 and valid OwnerStage reference).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "StageEditor")
	bool IsRegisteredToStage() const;

	/**
	 * @brief Checks if this Entity is orphaned (was registered but OwnerStage was deleted).
	 * An orphaned Entity has a valid EntityID but its OwnerStage reference is broken.
	 * @return True if orphaned (EntityID > 0 but OwnerStage reference is invalid/null).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "StageEditor")
	bool IsOrphaned() const;

	/**
	 * @brief Clears orphaned state, resetting to unregistered state.
	 * Used to clean up Entities whose owner Stage was deleted.
	 * Resets SUID, OwnerStage, and EntityState to default values.
	 */
	UFUNCTION(BlueprintCallable, Category = "StageEditor")
	void ClearOrphanedState();

	/**
	 * @brief Clears all Stage registration data, resetting to unregistered state.
	 * Called when an Entity is explicitly unregistered from a Stage.
	 * Unlike ClearOrphanedState(), this works regardless of orphan status.
	 */
	void ClearRegistrationState();
};
