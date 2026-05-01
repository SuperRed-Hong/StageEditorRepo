// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Forward declarations
class AActor;
class AStage;

/**
 * @brief Tree item type for StageEditor tree view.
 */
enum class EStageTreeItemType
{
	Stage,           // Stage actor row
	ActsFolder,      // "Acts" folder
	EntitiesFolder,  // "Registered Entities" folder
	Act,             // Act row
	Entity           // Entity row (can appear under Act or Entities Folder)
};

/**
 * @brief Tree item data structure for StageEditor hierarchical tree view.
 *
 * Tree Structure:
 * - Stage
 *   - Acts Folder
 *     - Act 0 (Default)
 *       - Entity 1 (State: X)
 *       - Entity 2 (State: Y)
 *     - Act 1
 *       - ...
 *   - Entities Folder
 *     - Entity 1
 *     - Entity 2
 */
struct FStageTreeItem : public TSharedFromThis<FStageTreeItem>
{
	/** Type of tree item */
	EStageTreeItemType Type;

	/** Display name in tree view */
	FString DisplayName;

	/** Act ID or Entity ID (-1 if not applicable) */
	int32 ID;

	/** Actor pointer (for Entity items) */
	TWeakObjectPtr<AActor> ActorPtr;

	/** Stage pointer (for Stage root items) */
	TWeakObjectPtr<AStage> StagePtr;

	/** Child items */
	TArray<TSharedPtr<FStageTreeItem>> Children;

	/** Parent item (weak ptr to avoid circular reference) */
	TWeakPtr<FStageTreeItem> Parent;

	/** Actor path (for viewport selection sync) */
	FString ActorPath;

	/** Entity state value (for Entity items under Act) */
	int32 EntityState = 0;

	/** Whether this Entity has a state override in parent Act */
	bool bHasEntityState = false;

	/**
	 * Constructor.
	 * @param InType - Item type
	 * @param InName - Display name
	 * @param InID - Act ID or Entity ID
	 * @param InActor - Actor pointer (for Entity items)
	 * @param InStage - Stage pointer (for Stage root items)
	 */
	FStageTreeItem(
		EStageTreeItemType InType,
		const FString& InName,
		int32 InID = -1,
		AActor* InActor = nullptr,
		AStage* InStage = nullptr)
		: Type(InType)
		, DisplayName(InName)
		, ID(InID)
		, ActorPtr(InActor)
		, StagePtr(InStage)
	{}
};
