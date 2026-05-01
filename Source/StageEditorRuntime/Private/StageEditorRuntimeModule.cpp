#pragma region Imports
#include "StageEditorRuntimeModule.h"
#include "Debug/StageDebugSettings.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "Actors/Stage.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "FStageEditorRuntimeModule"
#pragma endregion Imports

#pragma region Console Command Helpers
/**
 * Helper to get the StageManagerSubsystem from the current game world.
 * Works in PIE and standalone game.
 */
static UStageManagerSubsystem* GetStageManagerSubsystem()
{
	if (!GEngine) return nullptr;

	// Try to find the game world (PIE or standalone)
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
		{
			if (UWorld* World = Context.World())
			{
				return World->GetSubsystem<UStageManagerSubsystem>();
			}
		}
	}
	return nullptr;
}

/**
 * Helper to print feedback to console and screen.
 */
static void PrintCommandFeedback(const FString& Message, FColor Color = FColor::Cyan)
{
	UE_LOG(LogTemp, Log, TEXT("%s"), *Message);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, Color, Message);
	}
}
#pragma endregion Console Command Helpers

#pragma region Console Commands
/**
 * Console command: Stage.Debug [0|1]
 * Toggles the Stage Debug HUD display.
 * Usage:
 *   Stage.Debug     - Toggle on/off
 *   Stage.Debug 1   - Enable
 *   Stage.Debug 0   - Disable
 */
static FAutoConsoleCommand StageDebugCommand(
	TEXT("Stage.Debug"),
	TEXT("Toggle Stage Debug HUD. Usage: Stage.Debug [0|1]"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		UStageDebugSettings* Settings = UStageDebugSettings::Get();
		if (!Settings)
		{
			UE_LOG(LogTemp, Warning, TEXT("Stage.Debug: Failed to get StageDebugSettings"));
			return;
		}

		if (Args.Num() > 0)
		{
			Settings->bEnableDebugHUD = FCString::Atoi(*Args[0]) != 0;
		}
		else
		{
			// Toggle
			Settings->bEnableDebugHUD = !Settings->bEnableDebugHUD;
		}

		UE_LOG(LogTemp, Log, TEXT("Stage Debug HUD: %s"),
			Settings->bEnableDebugHUD ? TEXT("Enabled") : TEXT("Disabled"));

		// Also print to screen for immediate feedback
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f,
				Settings->bEnableDebugHUD ? FColor::Green : FColor::Yellow,
				FString::Printf(TEXT("Stage Debug HUD: %s"),
					Settings->bEnableDebugHUD ? TEXT("Enabled") : TEXT("Disabled")));
		}
	})
);

/**
 * Console command: Stage.Watch <StageID>
 * Add a Stage to the debug watch list.
 */
static FAutoConsoleCommand StageWatchCommand(
	TEXT("Stage.Watch"),
	TEXT("Add Stage(s) to watch list. Usage: Stage.Watch <ID> [ID2] [ID3]..."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		UStageManagerSubsystem* Subsystem = GetStageManagerSubsystem();
		if (!Subsystem)
		{
			PrintCommandFeedback(TEXT("Stage.Watch: No game world active (start PIE first)"), FColor::Red);
			return;
		}

		if (Args.Num() == 0)
		{
			PrintCommandFeedback(TEXT("Stage.Watch: Usage: Stage.Watch <ID> [ID2] [ID3]..."), FColor::Yellow);
			return;
		}

		for (const FString& Arg : Args)
		{
			int32 StageID = FCString::Atoi(*Arg);
			if (Subsystem->WatchStage(StageID))
			{
				AStage* Stage = Subsystem->GetStage(StageID);
				FString Name = Stage ? Stage->GetStageName() : TEXT("Unknown");
				PrintCommandFeedback(FString::Printf(TEXT("Watching: %s (ID:%d)"), *Name, StageID), FColor::Green);
			}
			else
			{
				PrintCommandFeedback(FString::Printf(TEXT("Failed to watch Stage ID:%d"), StageID), FColor::Red);
			}
		}
	})
);

/**
 * Console command: Stage.Unwatch <StageID>
 * Remove a Stage from the debug watch list.
 */
static FAutoConsoleCommand StageUnwatchCommand(
	TEXT("Stage.Unwatch"),
	TEXT("Remove Stage(s) from watch list. Usage: Stage.Unwatch <ID> [ID2] [ID3]..."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		UStageManagerSubsystem* Subsystem = GetStageManagerSubsystem();
		if (!Subsystem)
		{
			PrintCommandFeedback(TEXT("Stage.Unwatch: No game world active"), FColor::Red);
			return;
		}

		if (Args.Num() == 0)
		{
			PrintCommandFeedback(TEXT("Stage.Unwatch: Usage: Stage.Unwatch <ID> [ID2]..."), FColor::Yellow);
			return;
		}

		for (const FString& Arg : Args)
		{
			int32 StageID = FCString::Atoi(*Arg);
			if (Subsystem->UnwatchStage(StageID))
			{
				PrintCommandFeedback(FString::Printf(TEXT("Unwatched Stage ID:%d"), StageID), FColor::Yellow);
			}
		}
	})
);

/**
 * Console command: Stage.WatchClear
 * Clear the watch list.
 */
static FAutoConsoleCommand StageWatchClearCommand(
	TEXT("Stage.WatchClear"),
	TEXT("Clear the Stage watch list."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		UStageManagerSubsystem* Subsystem = GetStageManagerSubsystem();
		if (!Subsystem)
		{
			PrintCommandFeedback(TEXT("Stage.WatchClear: No game world active"), FColor::Red);
			return;
		}

		Subsystem->ClearWatchList();
		PrintCommandFeedback(TEXT("Watch list cleared"), FColor::Yellow);
	})
);

/**
 * Console command: Stage.WatchAll
 * Add all registered Stages to watch list.
 */
static FAutoConsoleCommand StageWatchAllCommand(
	TEXT("Stage.WatchAll"),
	TEXT("Watch all registered Stages."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		UStageManagerSubsystem* Subsystem = GetStageManagerSubsystem();
		if (!Subsystem)
		{
			PrintCommandFeedback(TEXT("Stage.WatchAll: No game world active"), FColor::Red);
			return;
		}

		Subsystem->WatchAllStages();
		PrintCommandFeedback(FString::Printf(TEXT("Now watching all %d Stages"), Subsystem->GetWatchedStageCount()), FColor::Green);
	})
);

/**
 * Console command: Stage.WatchOnly <StageID>
 * Clear list and watch only one Stage.
 */
static FAutoConsoleCommand StageWatchOnlyCommand(
	TEXT("Stage.WatchOnly"),
	TEXT("Watch only the specified Stage. Usage: Stage.WatchOnly <ID>"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		UStageManagerSubsystem* Subsystem = GetStageManagerSubsystem();
		if (!Subsystem)
		{
			PrintCommandFeedback(TEXT("Stage.WatchOnly: No game world active"), FColor::Red);
			return;
		}

		if (Args.Num() == 0)
		{
			PrintCommandFeedback(TEXT("Stage.WatchOnly: Usage: Stage.WatchOnly <ID>"), FColor::Yellow);
			return;
		}

		int32 StageID = FCString::Atoi(*Args[0]);
		if (Subsystem->WatchOnlyStage(StageID))
		{
			AStage* Stage = Subsystem->GetStage(StageID);
			FString Name = Stage ? Stage->GetStageName() : TEXT("Unknown");
			PrintCommandFeedback(FString::Printf(TEXT("Now watching only: %s (ID:%d)"), *Name, StageID), FColor::Green);
		}
		else
		{
			PrintCommandFeedback(FString::Printf(TEXT("Failed: Stage ID:%d not found"), StageID), FColor::Red);
		}
	})
);

/**
 * Console command: Stage.WatchList
 * Print current watch list.
 */
static FAutoConsoleCommand StageWatchListCommand(
	TEXT("Stage.WatchList"),
	TEXT("Print the current Stage watch list."),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		UStageManagerSubsystem* Subsystem = GetStageManagerSubsystem();
		if (!Subsystem)
		{
			PrintCommandFeedback(TEXT("Stage.WatchList: No game world active"), FColor::Red);
			return;
		}

		TArray<int32> WatchedIDs = Subsystem->GetWatchedStageIDs();
		if (WatchedIDs.Num() == 0)
		{
			PrintCommandFeedback(TEXT("Watch list is empty. Use Stage.Watch <ID> to add."), FColor::Yellow);
			return;
		}

		PrintCommandFeedback(FString::Printf(TEXT("=== Watched Stages (%d) ==="), WatchedIDs.Num()), FColor::Cyan);
		for (int32 StageID : WatchedIDs)
		{
			AStage* Stage = Subsystem->GetStage(StageID);
			FString Name = Stage ? Stage->GetStageName() : TEXT("(invalid)");
			PrintCommandFeedback(FString::Printf(TEXT("  [%d] %s"), StageID, *Name), FColor::White);
		}
	})
);

#pragma endregion Console Commands

#pragma region Module Interface
void FStageEditorRuntimeModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FStageEditorRuntimeModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}
#pragma endregion Module Interface

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FStageEditorRuntimeModule, StageEditorRuntime)
