#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "StageDebugSettings.generated.h"

/**
 * @brief Display position preset for Stage Debug HUD
 */
UENUM(BlueprintType)
enum class EStageDebugPosition : uint8
{
	TopLeft      UMETA(DisplayName = "Top Left"),
	TopRight     UMETA(DisplayName = "Top Right"),
	BottomLeft   UMETA(DisplayName = "Bottom Left"),
	BottomRight  UMETA(DisplayName = "Bottom Right"),
	Custom       UMETA(DisplayName = "Custom Position")
};

/**
 * @brief Stage Debug HUD Settings
 * Appears in Project Settings → Plugins → Stage Editor
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Stage Editor"))
class STAGEEDITORRUNTIME_API UStageDebugSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UStageDebugSettings();

	/** Get singleton instance */
	static UStageDebugSettings* Get();

	//----------------------------------------------------------------
	// Debug HUD Settings
	//----------------------------------------------------------------

	/** Enable/Disable Debug HUD display */
	UPROPERTY(config, EditAnywhere, Category = "Debug HUD", meta = (DisplayName = "Enable Debug HUD"))
	bool bEnableDebugHUD = false;

	/** Display position on screen */
	UPROPERTY(config, EditAnywhere, Category = "Debug HUD", meta = (DisplayName = "Display Position"))
	EStageDebugPosition DisplayPosition = EStageDebugPosition::TopLeft;

	/** Custom offset when DisplayPosition is set to Custom */
	UPROPERTY(config, EditAnywhere, Category = "Debug HUD",
		meta = (DisplayName = "Custom Offset", EditCondition = "DisplayPosition == EStageDebugPosition::Custom"))
	FVector2D CustomOffset = FVector2D(50.0f, 50.0f);

	/** Use detailed mode (shows more info per Stage) */
	UPROPERTY(config, EditAnywhere, Category = "Debug HUD", meta = (DisplayName = "Detailed Mode"))
	bool bDetailedMode = true;

	/** Text scale factor */
	UPROPERTY(config, EditAnywhere, Category = "Debug HUD",
		meta = (DisplayName = "Text Scale", ClampMin = "0.5", ClampMax = "3.0"))
	float TextScale = 1.0f;

	/** Margin from screen edge */
	UPROPERTY(config, EditAnywhere, Category = "Debug HUD",
		meta = (DisplayName = "Screen Margin", ClampMin = "0.0", ClampMax = "200.0"))
	float ScreenMargin = 50.0f;

	//----------------------------------------------------------------
	// UDeveloperSettings Interface
	//----------------------------------------------------------------

	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	virtual FName GetSectionName() const override { return FName("Stage Editor"); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override { return NSLOCTEXT("StageEditor", "StageEditorSettingsName", "Stage Editor"); }
	virtual FText GetSectionDescription() const override { return NSLOCTEXT("StageEditor", "StageEditorSettingsDesc", "Configure Stage Editor plugin settings"); }
#endif
};
