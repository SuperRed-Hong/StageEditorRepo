#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Logging/StageEditorLog.h"

class FStageEditorController;
class SDockTab;
class FSpawnTabArgs;

/**
 * StageEditor module - Editor tools for dynamic stage management.
 *
 * Responsibilities:
 * - Register/unregister Stage Editor tab spawner
 * - Register toolbar button for quick access
 * - Manage custom Slate styles and icons
 * - Maintain Controller lifecycle
 */
class FStageEditorModule : public IModuleInterface
{
public:
	/** Tab identifier for the Stage Editor panel (legacy STreeView). */
	static const FName StageEditorTabName;

	/** Tab identifier for the Stage Editor V2 panel (SceneOutliner-based). */
	static const FName StageEditorV2TabName;

	/** Tab identifier for the Stage DataLayer Outliner panel. */
	static const FName StageDataLayerOutlinerTabName;

	#pragma region Module Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	#pragma endregion Module Interface

	/** Get the shared Controller instance. Creates if not exists. */
	TSharedPtr<FStageEditorController> GetController();

	/** Static helper to get the module instance. */
	static FStageEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FStageEditorModule>("StageEditor");
	}

	/** Static helper to check if module is loaded. */
	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("StageEditor");
	}

private:
	#pragma region Initialization
	/** Initialize custom Slate style set (icons, brushes). */
	void InitializeStyleSet();

	/** Shutdown and cleanup style set. */
	void ShutdownStyleSet();
	#pragma endregion Initialization

	#pragma region Tab Registration
	/** Register the Stage Editor tab spawner in Window menu. */
	void RegisterTabSpawner();

	/** Unregister the Stage Editor tab spawner. */
	void UnregisterTabSpawner();

	/** Callback when Stage Editor tab is spawned (legacy). */
	TSharedRef<SDockTab> OnSpawnStageEditorTab(const FSpawnTabArgs& Args);

	/** Callback when Stage Editor V2 tab is spawned (SceneOutliner-based). */
	TSharedRef<SDockTab> OnSpawnStageEditorV2Tab(const FSpawnTabArgs& Args);

	/** Callback when Stage DataLayer Outliner tab is spawned. */
	TSharedRef<SDockTab> OnSpawnStageDataLayerOutlinerTab(const FSpawnTabArgs& Args);
	#pragma endregion Tab Registration

	#pragma region Toolbar Extension
	/** Register toolbar button in LevelEditor.LevelEditorToolBar.User. */
	void RegisterToolbarButton();

	/** Callback to open the Stage Editor tab. */
	void OnToolbarButtonClicked();
	#pragma endregion Toolbar Extension

	#pragma region Members
	/** Controller for Stage Editor logic (shared across tab instances). */
	TSharedPtr<FStageEditorController> Controller;
	#pragma endregion Members
};
