#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Core/StageCoreTypes.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "StageDebugHUD.generated.h"

class AStage;

/**
 * @brief Stage Debug HUD
 * Draws all Stage status information on screen.
 *
 * Usage:
 * 1. Set as HUDClass in your GameMode
 * 2. Enable via console command: Stage.Debug 1
 * 3. Or enable in Project Settings → Plugins → Stage Editor
 */
UCLASS()
class STAGEEDITORRUNTIME_API AStageDebugHUD : public AHUD
{
	GENERATED_BODY()

public:
	AStageDebugHUD();

	virtual void DrawHUD() override;

protected:
	//----------------------------------------------------------------
	// Drawing Methods
	//----------------------------------------------------------------

	/** Draw a single Stage info in simple mode */
	void DrawStageSimple(AStage* Stage, float X, float& YOffset);

	/** Draw a single Stage info in detailed mode */
	void DrawStageDetailed(AStage* Stage, float X, float& YOffset);

	//----------------------------------------------------------------
	// Helper Methods
	//----------------------------------------------------------------

	/** Get color for Stage runtime state */
	FLinearColor GetStateColor(EStageRuntimeState State) const;

	/** Get color for DataLayer runtime state */
	FLinearColor GetDataLayerStateColor(EDataLayerRuntimeState State) const;

	/** Convert Stage state to display string */
	static FString StateToString(EStageRuntimeState State);

	/** Convert DataLayer state to display string */
	static FString DataLayerStateToString(EDataLayerRuntimeState State);

	/** Calculate start position based on settings */
	FVector2D GetStartPosition() const;

	//----------------------------------------------------------------
	// Layout Constants
	//----------------------------------------------------------------

	/** Line height for text */
	float LineHeight = 18.0f;

	/** Indent width for tree structure */
	float IndentWidth = 15.0f;

	/** Estimated width of the debug panel (for right-side positioning) */
	float PanelWidth = 350.0f;
};
