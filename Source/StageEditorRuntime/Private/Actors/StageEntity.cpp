#pragma region Imports
#include "Actors/StageEntity.h"
#pragma endregion Imports

#pragma region Construction
AStageEntity::AStageEntity()
{
	PrimaryActorTick.bCanEverTick = false;

	// Create the core EntityComponent
	// Note: Blueprint subclasses of AStageEntity can override the component class in the editor
	EntityComponent = CreateDefaultSubobject<UStageEntityComponent>(TEXT("EntityComponent"));
}
#pragma endregion Construction

#pragma region Lifecycle
void AStageEntity::BeginPlay()
{
	Super::BeginPlay();
}
#pragma endregion Lifecycle

#pragma region Convenience Wrappers
#pragma endregion Convenience Wrappers
