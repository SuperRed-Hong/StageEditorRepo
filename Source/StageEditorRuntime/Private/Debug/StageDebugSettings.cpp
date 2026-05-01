#include "Debug/StageDebugSettings.h"

UStageDebugSettings::UStageDebugSettings()
{
}

UStageDebugSettings* UStageDebugSettings::Get()
{
	return GetMutableDefault<UStageDebugSettings>();
}
