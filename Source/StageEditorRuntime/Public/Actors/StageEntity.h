#pragma once

#pragma region Imports
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StageEntityComponent.h"
#include "StageEntity.generated.h"
#pragma endregion Imports

/**
 * @brief Convenience base class for Entity Actors.
 * Automatically includes UStageEntityComponent for quick setup.
 * All core logic is in the component - this is just a wrapper.
 */
UCLASS(Abstract, Blueprintable)
class STAGEEDITORRUNTIME_API AStageEntity : public AActor
{
	GENERATED_BODY()
	
public:	
#pragma region Construction
	/**
	 * @brief Default constructor.
	 * Initializes the EntityComponent.
	 */
	AStageEntity();
#pragma endregion Construction

protected:
#pragma region Lifecycle
	/**
	 * @brief Called when the game starts or when spawned.
	 */
	virtual void BeginPlay() override;
#pragma endregion Lifecycle

public:	
#pragma region Components
	/** The core Entity component that holds all logic. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stage Entity")
	TObjectPtr<UStageEntityComponent> EntityComponent;
#pragma endregion Components

#pragma region Convenience Wrappers
	//----------------------------------------------------------------
	// Convenience Wrappers (delegates to Component)
	//----------------------------------------------------------------

	/**
	 * @brief Get the current EntityState.
	 * @return The current state value.
	 */
	UFUNCTION(BlueprintCallable, Category = "Stage Entity")
	int32 GetEntityState() const { return EntityComponent ? EntityComponent->EntityState : 0; }
#pragma endregion Convenience Wrappers
};
