// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Subsystems/StageManagerSubsystem.h"

/**
 * Shared utility functions for DataLayerSync module.
 * These are inline functions to avoid Unity Build duplicate definition errors.
 */
namespace StageDataLayerSyncUtils
{
	/**
	 * Get the current editor world from GEditor context.
	 * @return The editor world, or nullptr if not available.
	 */
	FORCEINLINE UWorld* GetEditorWorld()
	{
		if (!GEditor)
		{
			return nullptr;
		}
		return GEditor->GetEditorWorldContext().World();
	}

	/**
	 * Get the StageManagerSubsystem from the specified world.
	 * If World is nullptr, attempts to get it from the editor context.
	 * @param World Optional world to get subsystem from. If null, uses editor world.
	 * @return The StageManagerSubsystem, or nullptr if not available.
	 */
	FORCEINLINE UStageManagerSubsystem* GetStageManagerSubsystem(UWorld* World = nullptr)
	{
		if (!World)
		{
			World = GetEditorWorld();
		}
		if (!World)
		{
			return nullptr;
		}
		return World->GetSubsystem<UStageManagerSubsystem>();
	}
}
