#include "Debug/StageDebugHUD.h"
#include "Debug/StageDebugSettings.h"
#include "Subsystems/StageManagerSubsystem.h"
#include "Actors/Stage.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

AStageDebugHUD::AStageDebugHUD()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AStageDebugHUD::DrawHUD()
{
	Super::DrawHUD();

	// Check if enabled
	UStageDebugSettings* Settings = UStageDebugSettings::Get();
	if (!Settings || !Settings->bEnableDebugHUD)
	{
		return;
	}

	// Get Subsystem
	UWorld* World = GetWorld();
	if (!World) return;

	UStageManagerSubsystem* Subsystem = World->GetSubsystem<UStageManagerSubsystem>();
	if (!Subsystem) return;

	// Calculate start position
	FVector2D StartPos = GetStartPosition();
	float YOffset = StartPos.Y;
	const float Scale = Settings->TextScale;
	const float ScaledLineHeight = LineHeight * Scale;

	// Get watch list and all stages info
	TArray<int32> WatchedIDs = Subsystem->GetWatchedStageIDs();
	int32 TotalStageCount = Subsystem->GetRegisteredStageCount();

	if (!Canvas) return;

	// Draw header with watch count
	FString Header;
	if (WatchedIDs.Num() > 0)
	{
		Header = FString::Printf(TEXT("=== Stage Debug (%d/%d) ==="), WatchedIDs.Num(), TotalStageCount);
	}
	else
	{
		Header = FString::Printf(TEXT("=== Stage Debug (0/%d) ==="), TotalStageCount);
	}
	DrawText(Header, FLinearColor::Yellow, StartPos.X, YOffset, nullptr, Scale);
	YOffset += ScaledLineHeight;

	// Draw separator
	DrawText(TEXT("─────────────────────────"), FLinearColor(0.5f, 0.5f, 0.5f), StartPos.X, YOffset, nullptr, Scale * 0.8f);
	YOffset += ScaledLineHeight * 0.8f;

	// Check if watch list is empty
	if (WatchedIDs.Num() == 0)
	{
		// Show "no tracking" message with help
		DrawText(TEXT("(No Stage being tracked)"), FLinearColor::Gray, StartPos.X, YOffset, nullptr, Scale * 0.9f);
		YOffset += ScaledLineHeight;
		DrawText(TEXT("Use: Stage.Watch <ID>"), FLinearColor(0.5f, 0.5f, 0.5f), StartPos.X, YOffset, nullptr, Scale * 0.8f);
		YOffset += ScaledLineHeight * 0.8f;
		DrawText(TEXT("  or Stage.WatchAll"), FLinearColor(0.5f, 0.5f, 0.5f), StartPos.X, YOffset, nullptr, Scale * 0.8f);
		return;
	}

	// Draw only watched Stages
	for (int32 StageID : WatchedIDs)
	{
		AStage* Stage = Subsystem->GetStage(StageID);
		if (!Stage) continue;

		if (Settings->bDetailedMode)
		{
			DrawStageDetailed(Stage, StartPos.X, YOffset);
		}
		else
		{
			DrawStageSimple(Stage, StartPos.X, YOffset);
		}
	}
}

FVector2D AStageDebugHUD::GetStartPosition() const
{
	UStageDebugSettings* Settings = UStageDebugSettings::Get();
	if (!Settings) return FVector2D(50.0f, 50.0f);

	const float ScreenWidth = Canvas ? Canvas->SizeX : 1920.0f;
	const float ScreenHeight = Canvas ? Canvas->SizeY : 1080.0f;
	const float Margin = Settings->ScreenMargin;

	switch (Settings->DisplayPosition)
	{
	case EStageDebugPosition::TopLeft:
		return FVector2D(Margin, Margin);

	case EStageDebugPosition::TopRight:
		return FVector2D(ScreenWidth - PanelWidth - Margin, Margin);

	case EStageDebugPosition::BottomLeft:
		return FVector2D(Margin, ScreenHeight - 300.0f);

	case EStageDebugPosition::BottomRight:
		return FVector2D(ScreenWidth - PanelWidth - Margin, ScreenHeight - 300.0f);

	case EStageDebugPosition::Custom:
		return Settings->CustomOffset;

	default:
		return FVector2D(Margin, Margin);
	}
}

void AStageDebugHUD::DrawStageSimple(AStage* Stage, float X, float& YOffset)
{
	UStageDebugSettings* Settings = UStageDebugSettings::Get();
	const float Scale = Settings ? Settings->TextScale : 1.0f;

	// Format: StageName: State | DL: DataLayerState
	FString Text = FString::Printf(TEXT("%s: %s | DL: %s"),
		*Stage->GetStageName(),
		*StateToString(Stage->GetCurrentStageState()),
		*DataLayerStateToString(Stage->GetStageDataLayerState()));

	// Add lock indicator
	if (Stage->IsStageStateLocked())
	{
		Text += TEXT(" [LOCKED]");
	}

	FLinearColor Color = GetStateColor(Stage->GetCurrentStageState());
	DrawText(Text, Color, X, YOffset, nullptr, Scale);
	YOffset += LineHeight * Scale;
}

void AStageDebugHUD::DrawStageDetailed(AStage* Stage, float X, float& YOffset)
{
	UStageDebugSettings* Settings = UStageDebugSettings::Get();
	const float Scale = Settings ? Settings->TextScale : 1.0f;
	const float ScaledLineHeight = LineHeight * Scale;
	const float Indent = IndentWidth * Scale;
	const float SubScale = Scale * 0.9f;

	FLinearColor StateColor = GetStateColor(Stage->GetCurrentStageState());
	FLinearColor TextColor = FLinearColor::White;
	FLinearColor DimColor = FLinearColor(0.7f, 0.7f, 0.7f);

	// Stage name and ID
	FString Header = FString::Printf(TEXT("%s (ID:%d)"),
		*Stage->GetStageName(), Stage->GetStageID());
	DrawText(Header, StateColor, X, YOffset, nullptr, Scale);
	YOffset += ScaledLineHeight;

	// State
	FString StateText = FString::Printf(TEXT("├─ State: %s%s"),
		*StateToString(Stage->GetCurrentStageState()),
		Stage->IsStageStateLocked() ? TEXT(" (Locked)") : TEXT(""));
	DrawText(StateText, TextColor, X + Indent, YOffset, nullptr, SubScale);
	YOffset += ScaledLineHeight;

	// DataLayer State
	FString DLText = FString::Printf(TEXT("├─ DataLayer: %s"),
		*DataLayerStateToString(Stage->GetStageDataLayerState()));
	FLinearColor DLColor = GetDataLayerStateColor(Stage->GetStageDataLayerState());
	DrawText(DLText, DLColor, X + Indent, YOffset, nullptr, SubScale);
	YOffset += ScaledLineHeight;

	// Active Acts
	TArray<int32> ActiveActs = Stage->GetActiveActIDs();
	TArray<int32> AllActs = Stage->GetAllActIDs();
	FString ActsStr;
	for (int32 ActID : AllActs)
	{
		if (!ActsStr.IsEmpty()) ActsStr += TEXT(", ");
		bool bIsActive = ActiveActs.Contains(ActID);
		bool bIsLocked = Stage->IsActLocked(ActID);
		ActsStr += FString::Printf(TEXT("%d%s%s"),
			ActID,
			bIsActive ? TEXT("✓") : TEXT(""),
			bIsLocked ? TEXT("🔒") : TEXT(""));
	}
	FString ActsText = FString::Printf(TEXT("├─ Acts: [%s]"), *ActsStr);
	DrawText(ActsText, TextColor, X + Indent, YOffset, nullptr, SubScale);
	YOffset += ScaledLineHeight;

	// Zone Actor Counts
	int32 LoadZoneCount = Stage->OverlappingLoadZoneActors.Num();
	int32 ActivateZoneCount = Stage->OverlappingActivateZoneActors.Num();

	FString LoadZoneText = FString::Printf(TEXT("├─ LoadZone: %d actor%s"),
		LoadZoneCount, LoadZoneCount == 1 ? TEXT("") : TEXT("s"));
	FLinearColor LoadZoneColor = LoadZoneCount > 0 ? FLinearColor::Green : DimColor;
	DrawText(LoadZoneText, LoadZoneColor, X + Indent, YOffset, nullptr, SubScale);
	YOffset += ScaledLineHeight;

	FString ActivateZoneText = FString::Printf(TEXT("└─ ActivateZone: %d actor%s"),
		ActivateZoneCount, ActivateZoneCount == 1 ? TEXT("") : TEXT("s"));
	FLinearColor ActivateZoneColor = ActivateZoneCount > 0 ? FLinearColor::Green : DimColor;
	DrawText(ActivateZoneText, ActivateZoneColor, X + Indent, YOffset, nullptr, SubScale);
	YOffset += ScaledLineHeight;

	// Spacing between Stages
	YOffset += ScaledLineHeight * 0.3f;
}

FLinearColor AStageDebugHUD::GetStateColor(EStageRuntimeState State) const
{
	switch (State)
	{
	case EStageRuntimeState::Unloaded:
		return FLinearColor::Gray;

	case EStageRuntimeState::Preloading:
		return FLinearColor::Yellow;

	case EStageRuntimeState::Loaded:
		return FLinearColor(0.4f, 0.7f, 1.0f); // Light Blue

	case EStageRuntimeState::Active:
		return FLinearColor::Green;

	case EStageRuntimeState::Unloading:
		return FLinearColor(1.0f, 0.5f, 0.0f); // Orange

	default:
		return FLinearColor::White;
	}
}

FLinearColor AStageDebugHUD::GetDataLayerStateColor(EDataLayerRuntimeState State) const
{
	switch (State)
	{
	case EDataLayerRuntimeState::Unloaded:
		return FLinearColor::Gray;

	case EDataLayerRuntimeState::Loaded:
		return FLinearColor(0.4f, 0.7f, 1.0f); // Light Blue

	case EDataLayerRuntimeState::Activated:
		return FLinearColor::Green;

	default:
		return FLinearColor::White;
	}
}

FString AStageDebugHUD::StateToString(EStageRuntimeState State)
{
	switch (State)
	{
	case EStageRuntimeState::Unloaded:   return TEXT("Unloaded");
	case EStageRuntimeState::Preloading: return TEXT("Preloading");
	case EStageRuntimeState::Loaded:     return TEXT("Loaded");
	case EStageRuntimeState::Active:     return TEXT("Active");
	case EStageRuntimeState::Unloading:  return TEXT("Unloading");
	default:                             return TEXT("Unknown");
	}
}

FString AStageDebugHUD::DataLayerStateToString(EDataLayerRuntimeState State)
{
	switch (State)
	{
	case EDataLayerRuntimeState::Unloaded:  return TEXT("Unloaded");
	case EDataLayerRuntimeState::Loaded:    return TEXT("Loaded");
	case EDataLayerRuntimeState::Activated: return TEXT("Activated");
	default:                                return TEXT("Unknown");
	}
}
