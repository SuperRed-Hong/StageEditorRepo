#pragma once

#include "CoreMinimal.h"
#include "Components/StageTriggerZoneComponent.h"
#include "BuiltInTriggerZoneComponent.generated.h"

/**
 * @brief Lightweight subclass of UStageTriggerZoneComponent for Stage's built-in zones.
 *
 * Hides redundant categories in the Details panel that are already proxied
 * by AStage or not applicable to built-in zones:
 * - Binding: OwnerStage is auto-set to the owning Stage
 * - Documentation: Built-in zones don't need user descriptions
 * - Filtering: Proxied by Stage's SharedTriggerActorTags
 *
 * PresetActions is kept visible so users can see ZoneType (locked) and optionally
 * override OnEnter/OnExit actions on a per-zone basis.
 *
 * Keeps exposed: bZoneEnabled, bComponentVisible, Events (OnActorEnter/Exit),
 * PresetActions so users can individually control each built-in zone.
 *
 * @see UStageTriggerZoneComponent for the full-featured version (used by external zones)
 */
UCLASS(HideCategories = (Binding, Documentation, Filtering))
class STAGEEDITORRUNTIME_API UBuiltInTriggerZoneComponent : public UStageTriggerZoneComponent
{
	GENERATED_BODY()
};
