#include "Actors/StageEntity.h"

AStageEntity::AStageEntity()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AStageEntity::BeginPlay()
{
	Super::BeginPlay();

	// Resolve component that was added in Blueprint.
	// Finds BP subclasses like BPC_BaseStageEntityComponent, not just the C++ base.
	if (!EntityComponent)
	{
		EntityComponent = FindComponentByClass<UStageEntityComponent>();
	}
}
