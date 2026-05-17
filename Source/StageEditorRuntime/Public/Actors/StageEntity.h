#pragma once

#pragma region Imports
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StageEntityComponent.h"
#include "StageEntity.generated.h"
#pragma endregion Imports

/**
 * @brief Convenience base class for Entity Actors.
 *
 * On BeginPlay, automatically finds a UStageEntityComponent (or BP subclass like
 * BPC_BaseStageEntityComponent) that you added in the Blueprint Components panel.
 *
 * C++ does NOT auto-create the component — that's intentional. It lets you choose
 * whichever BP component class you want. Just add it once in each Entity BP.
 */
UCLASS(Abstract, Blueprintable)
class STAGEEDITORRUNTIME_API AStageEntity : public AActor
{
	GENERATED_BODY()

public:
	AStageEntity();

protected:
	virtual void BeginPlay() override;

public:
	/** The Entity component. Auto-resolved from BP-added components in BeginPlay. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stage Entity")
	TObjectPtr<UStageEntityComponent> EntityComponent;

	UFUNCTION(BlueprintCallable, Category = "Stage Entity")
	int32 GetEntityState() const { return EntityComponent ? EntityComponent->EntityState : 0; }
};
