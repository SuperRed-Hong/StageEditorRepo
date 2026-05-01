#include "StageEditorModule.h"

#include "CustomStyle/StageEditorStyle.h"
#include "DataLayerSync/SStageDataLayerOutliner.h"
#include "DataLayerSync/DataLayerSyncStatusCache.h"
#include "EditorLogic/StageEditorController.h"
#include "EditorUI/StageEditorPanel.h"
#include "EditorUI/SStageEditorPanelV2.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "FStageEditorModule"

DEFINE_LOG_CATEGORY(LogStageEditor);
DEFINE_LOG_CATEGORY(LogStageMigration);

const FName FStageEditorModule::StageEditorTabName("StageEditorTab");
const FName FStageEditorModule::StageEditorV2TabName("StageEditorV2Tab");
const FName FStageEditorModule::StageDataLayerOutlinerTabName("StageDataLayerOutlinerTab");

#pragma region Module Interface

void FStageEditorModule::StartupModule()
{
	InitializeStyleSet();
	RegisterTabSpawner();

	// Initialize DataLayer sync status cache
	FDataLayerSyncStatusCache::Get().Initialize();

	// Defer toolbar registration until ToolMenus is ready
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
		this, &FStageEditorModule::RegisterToolbarButton));
}

void FStageEditorModule::ShutdownModule()
{
	// Shutdown DataLayer sync status cache
	FDataLayerSyncStatusCache::Get().Shutdown();

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	UnregisterTabSpawner();
	ShutdownStyleSet();
}

#pragma endregion Module Interface

#pragma region Initialization

void FStageEditorModule::InitializeStyleSet()
{
	FStageEditorStyleSetRegistry::Initialize();
}

void FStageEditorModule::ShutdownStyleSet()
{
	FStageEditorStyleSetRegistry::Shutdown();
}

#pragma endregion Initialization

#pragma region Tab Registration

void FStageEditorModule::RegisterTabSpawner()
{
	// Register Stage Editor tab (legacy STreeView-based)
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		StageEditorTabName,
		FOnSpawnTab::CreateRaw(this, &FStageEditorModule::OnSpawnStageEditorTab))
		.SetDisplayName(LOCTEXT("StageEditorTabTitle", "Stage Editor"))
		.SetMenuType(ETabSpawnerMenuType::Enabled)
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetAutoGenerateMenuEntry(true)
		.SetIcon(FSlateIcon(
			FStageEditorStyleSetRegistry::GetStyleSetName(),
			FStageEditorStyleNames::TabIconBrushName));

	// Register Stage Editor V2 tab (SceneOutliner-based)
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		StageEditorV2TabName,
		FOnSpawnTab::CreateRaw(this, &FStageEditorModule::OnSpawnStageEditorV2Tab))
		.SetDisplayName(LOCTEXT("StageEditorV2TabTitle", "Stage Editor V2"))
		.SetMenuType(ETabSpawnerMenuType::Enabled)
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetAutoGenerateMenuEntry(true)
		.SetIcon(FSlateIcon(
			FStageEditorStyleSetRegistry::GetStyleSetName(),
			FStageEditorStyleNames::TabIconBrushName));

	// Register Stage DataLayer Outliner tab
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		StageDataLayerOutlinerTabName,
		FOnSpawnTab::CreateRaw(this, &FStageEditorModule::OnSpawnStageDataLayerOutlinerTab))
		.SetDisplayName(LOCTEXT("StageDataLayerOutlinerTabTitle", "Stage DataLayer Outliner"))
		.SetMenuType(ETabSpawnerMenuType::Enabled)
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetAutoGenerateMenuEntry(true)
		.SetIcon(FSlateIcon(
			FStageEditorStyleSetRegistry::GetStyleSetName(),
			FStageEditorStyleNames::TabIconBrushName)); // TODO: Add unique icon
}

void FStageEditorModule::UnregisterTabSpawner()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(StageEditorTabName);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(StageEditorV2TabName);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(StageDataLayerOutlinerTabName);
}

TSharedRef<SDockTab> FStageEditorModule::OnSpawnStageEditorTab(const FSpawnTabArgs& Args)
{
	// Create controller if not exists (singleton-ish for session)
	if (!Controller.IsValid())
	{
		Controller = MakeShared<FStageEditorController>();
	}

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SStageEditorPanel, Controller)
		];
}

TSharedRef<SDockTab> FStageEditorModule::OnSpawnStageEditorV2Tab(const FSpawnTabArgs& Args)
{
	// Create controller if not exists (singleton-ish for session)
	if (!Controller.IsValid())
	{
		Controller = MakeShared<FStageEditorController>();
	}

	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("StageEditorV2TabLabel", "Stage Editor V2"))
		[
			SNew(SStageEditorPanelV2, Controller)
		];
}

TSharedRef<SDockTab> FStageEditorModule::OnSpawnStageDataLayerOutlinerTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(LOCTEXT("StageDataLayerOutlinerTabLabel", "Stage DataLayer Outliner"))
		[
			SNew(SStageDataLayerOutliner)
		];
}

#pragma endregion Tab Registration

#pragma region Toolbar Extension

void FStageEditorModule::RegisterToolbarButton()
{
	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
	if (!ToolbarMenu)
	{
		return;
	}

	FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("StageEditorTools");
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"OpenStageEditor",
		FUIAction(FExecuteAction::CreateRaw(this, &FStageEditorModule::OnToolbarButtonClicked)),
		LOCTEXT("OpenStageEditorButton", "Stage Editor"),
		LOCTEXT("OpenStageEditorTooltip", "Open the Stage Editor panel"),
		FSlateIcon(
			FStageEditorStyleSetRegistry::GetStyleSetName(),
			FStageEditorStyleNames::TabIconBrushName)
	));
}

void FStageEditorModule::OnToolbarButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(StageEditorTabName);
}

#pragma endregion Toolbar Extension

#pragma region Controller Access

TSharedPtr<FStageEditorController> FStageEditorModule::GetController()
{
	// Create controller if not exists
	if (!Controller.IsValid())
	{
		Controller = MakeShared<FStageEditorController>();
		Controller->Initialize();
	}
	return Controller;
}

#pragma endregion Controller Access

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FStageEditorModule, StageEditor)
