// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * @file StageEditorLog.h
 * @brief Centralized logging categories for StageEditor plugin
 *
 * This file defines all log categories used throughout the StageEditor plugin.
 * Keeping log categories in a dedicated file makes them easy to maintain and reference.
 */

/**
 * Main log category for StageEditor plugin.
 * Used for general editor operations, UI events, and stage management.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogStageEditor, Log, All);

/**
 * Log category for Stage migration and registry operations.
 * Used for tracking ID allocation, conflict resolution, and data migration.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogStageMigration, Log, All);
