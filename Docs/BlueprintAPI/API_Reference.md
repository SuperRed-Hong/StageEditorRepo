# StageEditor Plugin - API Reference

> **Version:** Phase 1
> **Last Updated:** 2025-11-28
> **Module:** StageEditorRuntime (Runtime) + StageEditor (Editor)

---

## Table of Contents

1. [Overview](#overview)
2. [Core Types & Enums](#core-types--enums)
3. [Runtime Classes](#runtime-classes)
   - [AStage](#astage)
   - [UStagePropComponent](#ustagepropcomponent)
   - [AProp](#aprop)
   - [UTriggerZoneComponentBase](#utriggerzonecomponentbase)
   - [UStageTriggerZoneComponent](#ustagetriggerzonecomponent)
   - [ATriggerZoneActor](#atriggerzoneactor)
   - [UStageManagerSubsystem](#ustagemanagersubsystem)
4. [Editor Classes](#editor-classes)
   - [FStageEditorController](#fstageeditorcontroller)
5. [Debug & Settings](#debug--settings)
   - [AStageDebugHUD](#astagedebughud)
   - [UStageDebugSettings](#ustagedebugsettings)
6. [Delegates Reference](#delegates-reference)
7. [Usage Examples](#usage-examples)

---

## Overview

The StageEditor plugin provides a dynamic stage management system for Unreal Engine 5.6, using DataLayers to control level streaming and state changes through a theatrical metaphor:

- **Stage** - The "Director" that orchestrates scene states
- **Act** - A scene configuration defining Prop states
- **Prop** - Any Actor that responds to Stage state changes

### Module Structure

| Module | Type | Purpose |
|--------|------|---------|
| `StageEditorRuntime` | Runtime | Core data structures, actors, components |
| `StageEditor` | Editor | Editor tools, UI, and controller logic |

---

## Core Types & Enums

### FSUID

**File:** `Source/StageEditorRuntime/Public/Core/StageCoreTypes.h`

Stage Unique ID - A hierarchical ID structure to uniquely identify entities within the Stage system.

```cpp
USTRUCT(BlueprintType)
struct STAGEEDITORRUNTIME_API FSUID
{
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 StageID = 0;    // Globally unique Stage ID

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ActID = 0;      // Local Act ID within Stage

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 PropID = 0;     // Local Prop ID within Stage
};
```

#### Factory Methods

| Method | Description |
|--------|-------------|
| `MakeStageID(int32)` | Creates SUID for Stage level (X.0.0) |
| `MakeActID(int32, int32)` | Creates SUID for Act level (X.Y.0) |
| `MakePropID(int32, int32)` | Creates SUID for Prop level (X.0.Z) |

#### Utility Methods

| Method | Return | Description |
|--------|--------|-------------|
| `ToString()` | `FString` | Returns "StageID.ActID.PropID" format |
| `IsValid()` | `bool` | True if StageID > 0 |
| `IsStageLevel()` | `bool` | True if only StageID is set |
| `IsActLevel()` | `bool` | True if StageID and ActID are set |
| `IsPropLevel()` | `bool` | True if StageID and PropID are set |

---

### EStageRuntimeState

**File:** `Source/StageEditorRuntime/Public/Core/StageCoreTypes.h`

Internal 5-state machine for Stage DataLayer lifecycle (developer view).

```cpp
UENUM(BlueprintType)
enum class EStageRuntimeState : uint8
{
    Unloaded,     // DataLayer completely unloaded
    Preloading,   // DataLayer being loaded (transition)
    Loaded,       // DataLayer loaded but not activated
    Active,       // DataLayer fully activated
    Unloading     // DataLayer being unloaded (transition)
};
```

**State Flow:**
```
Unloaded → Preloading → Loaded → Active
             ↑                      ↓
             └── Unloading ←────────┘
```

---

### EStageState

**File:** `Source/StageEditorRuntime/Public/Core/StageCoreTypes.h`

Simplified 3-state for user-facing API.

```cpp
UENUM(BlueprintType)
enum class EStageState : uint8
{
    Unloaded,   // Stage not loaded
    Loaded,     // Stage loaded but not active
    Active      // Stage fully active
};
```

**Mapping from EStageRuntimeState:**
- `Unloaded`, `Unloading` → `EStageState::Unloaded`
- `Preloading`, `Loaded` → `EStageState::Loaded`
- `Active` → `EStageState::Active`

---

### FAct

**File:** `Source/StageEditorRuntime/Public/Core/StageCoreTypes.h`

Defines a "Scene" or "State" configuration for a Stage.

```cpp
USTRUCT(BlueprintType)
struct STAGEEDITORRUNTIME_API FAct
{
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FSUID SUID;                                    // Unique Act ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DisplayName;                           // User-editable name

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bFollowStageState = false;                // Mirror Stage state

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EDataLayerRuntimeState InitialDataLayerState;  // Initial state when not following

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TMap<int32, int32> PropStateOverrides;         // PropID → TargetState

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UDataLayerAsset> AssociatedDataLayer;
};
```

#### Properties

| Property | Type | Description |
|----------|------|-------------|
| `SUID` | `FSUID` | Unique identifier (StageID.ActID.0) |
| `DisplayName` | `FString` | User-editable display name |
| `bFollowStageState` | `bool` | When true, Act DataLayer mirrors Stage state |
| `InitialDataLayerState` | `EDataLayerRuntimeState` | Initial state when Stage becomes Active (if not following) |
| `PropStateOverrides` | `TMap<int32, int32>` | Map of PropID to target state values |
| `AssociatedDataLayer` | `UDataLayerAsset*` | The DataLayer asset for this Act |

---

### FTriggerZoneDescription

**File:** `Source/StageEditorRuntime/Public/Core/StageCoreTypes.h`

Structured description for TriggerZone documentation.

```cpp
USTRUCT(BlueprintType)
struct STAGEEDITORRUNTIME_API FTriggerZoneDescription
{
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ETriggerZonePreset Preset;     // Quick-fill template

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Trigger;               // WHO/WHAT triggers (e.g., "Player")

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Condition;             // WHEN (pre-conditions)

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Target;                // WHAT is affected

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Action;                // WHAT action is performed

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Effect;                // WHY (gameplay effect)
};
```

#### Methods

| Method | Return | Description |
|--------|--------|-------------|
| `ToReadableString()` | `FString` | "When [Trigger] enters, if [Condition], then [Action] on [Target] to [Effect]" |
| `ApplyPreset()` | `void` | Auto-fills fields based on selected Preset |
| `IsEmpty()` | `bool` | True if no meaningful content |

---

### ETriggerZoneDefaultAction

**File:** `Source/StageEditorRuntime/Public/Core/StageCoreTypes.h`

```cpp
UENUM(BlueprintType)
enum class ETriggerZoneDefaultAction : uint8
{
    Custom,         // Use Blueprint events
    LoadStage,      // Calls Stage->LoadStage()
    ActivateStage,  // Calls Stage->ActivateStage()
    UnloadStage     // Calls Stage->UnloadStage()
};
```

---

### ETriggerZonePreset

**File:** `Source/StageEditorRuntime/Public/Core/StageCoreTypes.h`

```cpp
UENUM(BlueprintType)
enum class ETriggerZonePreset : uint8
{
    Custom,              // Manual entry
    StageLoad,           // Player enters → Stage preloads
    StageActivate,       // Player enters → Stage activates
    ActActivate,         // Player enters → Act activates
    ActDeactivate,       // Player enters → Act deactivates
    PropStateChange,     // Player enters → Prop state changes
    ConditionalTrigger   // Player enters + conditions → custom action
};
```

---

## Runtime Classes

### AStage

**File:** `Source/StageEditorRuntime/Public/Actors/Stage.h`

The "Director" actor that orchestrates scene states. Manages Acts and Props, handles DataLayer activation.

#### Core Properties

| Property | Type | Description |
|----------|------|-------------|
| `SUID` | `FSUID` | Stage Unique ID (read-only) |
| `Acts` | `TArray<FAct>` | All defined Acts |
| `PropRegistry` | `TMap<int32, TSoftObjectPtr<AActor>>` | Registered Props (PropID → Actor) |
| `ActiveActIDs` | `TArray<int32>` | Currently active Acts (priority order) |
| `CurrentStageState` | `EStageRuntimeState` | Current runtime state |
| `StageDataLayerAsset` | `UDataLayerAsset*` | Root DataLayer for this Stage |

#### Trigger Zone Properties

| Property | Type | Description |
|----------|------|-------------|
| `BuiltInLoadZone` | `UStageTriggerZoneComponent*` | Outer trigger zone (loads Stage) |
| `BuiltInActivateZone` | `UStageTriggerZoneComponent*` | Inner trigger zone (activates Stage) |
| `LoadZoneExtent` | `FVector` | Half-size of LoadZone (default: 2000,2000,500) |
| `ActivateZoneExtent` | `FVector` | Half-size of ActivateZone (default: 1000,1000,400) |
| `bDisableBuiltInZones` | `bool` | Disable built-in zones (use external) |
| `InitialStageState` | `EStageRuntimeState` | Initial state when zones disabled |

#### Stage State API (User-Facing, 3-State)

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `GetStageState()` | - | `EStageState` | Get simplified state |
| `GotoState(EStageState)` | `TargetState` | `void` | Request state transition |
| `LoadStage()` | - | `void` | Equivalent to `GotoState(Loaded)` |
| `ActivateStage()` | - | `void` | Equivalent to `GotoState(Active)` |
| `UnloadStage()` | - | `void` | Equivalent to `GotoState(Unloaded)` |

#### Stage State API (Debug, 5-State)

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `GetRuntimeState()` | - | `EStageRuntimeState` | Get detailed internal state |
| `GetCurrentStageState()` | - | `EStageRuntimeState` | Get current runtime state |
| `IsInTransitionState()` | - | `bool` | True if Preloading or Unloading |
| `IsStageLoaded()` | - | `bool` | True if Loaded or Active |
| `IsStageActive()` | - | `bool` | True if Active |

#### Act Activation API (Multi-Act System)

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `ActivateAct(int32)` | `ActID` | `void` | Activate Act, apply PropStates, load DataLayer |
| `DeactivateAct(int32)` | `ActID` | `void` | Deactivate Act, unload DataLayer |
| `ActivateActs(TArray<int32>)` | `ActIDs` | `void` | Activate multiple Acts (last has priority) |
| `DeactivateAllActs()` | - | `void` | Deactivate all active Acts |

#### Act Query API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `GetActiveActIDs()` | - | `TArray<int32>` | Get active Act IDs (priority order) |
| `IsActActive(int32)` | `ActID` | `bool` | Check if Act is active |
| `GetHighestPriorityActID()` | - | `int32` | Get most recently activated Act ID |
| `GetRecentActivatedActID()` | - | `int32` | Get last activated Act ID |
| `GetActiveActCount()` | - | `int32` | Count of active Acts |
| `GetActDisplayName(int32)` | `ActID` | `FString` | Get Act's display name |
| `GetActPropStates(int32)` | `ActID` | `TMap<int32,int32>` | Get Act's PropStateOverrides |
| `GetAllActIDs()` | - | `TArray<int32>` | Get all Act IDs |
| `GetActCount()` | - | `int32` | Total Act count |
| `DoesActExist(int32)` | `ActID` | `bool` | Check if Act exists |

#### Act Lock API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `LockAct(int32)` | `ActID` | `void` | Prevent Act from being deactivated |
| `UnlockAct(int32)` | `ActID` | `void` | Allow Act to be deactivated |
| `IsActLocked(int32)` | `ActID` | `bool` | Check if Act is locked |
| `GetLockedActIDs()` | - | `TArray<int32>` | Get all locked Act IDs |

#### Prop State API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `SetPropStateByID(int32, int32, bool)` | `PropID`, `NewState`, `bForce` | `bool` | Set Prop state |
| `GetPropStateByID(int32)` | `PropID` | `int32` | Get Prop state (-1 if not found) |
| `SetMultiplePropStates(TMap<int32,int32>)` | `PropStates` | `void` | Set multiple Prop states |
| `GetEffectivePropState(int32)` | `PropID` | `int32` | Get effective state from active Acts |
| `GetControllingActForProp(int32)` | `PropID` | `int32` | Get which Act controls this Prop |

#### Prop Query API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `GetPropActorByID(int32)` | `PropID` | `AActor*` | Get Prop Actor |
| `GetPropComponentByID(int32)` | `PropID` | `UStagePropComponent*` | Get Prop's component |
| `GetAllPropIDs()` | - | `TArray<int32>` | Get all Prop IDs |
| `GetAllPropActors()` | - | `TArray<AActor*>` | Get all Prop Actors |
| `GetPropCount()` | - | `int32` | Total Prop count |
| `DoesPropExist(int32)` | `PropID` | `bool` | Check if Prop exists |

#### DataLayer API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `SetActDataLayerState(int32, EDataLayerRuntimeState)` | `ActID`, `NewState` | `bool` | Set Act's DataLayer state |
| `GetActDataLayerState(int32)` | `ActID` | `EDataLayerRuntimeState` | Get Act's DataLayer state |
| `IsActDataLayerLoaded(int32)` | `ActID` | `bool` | Check if Act's DataLayer is loaded |
| `LoadActDataLayer(int32)` | `ActID` | `bool` | Load Act's DataLayer |
| `UnloadActDataLayer(int32)` | `ActID` | `bool` | Unload Act's DataLayer |
| `SetStageDataLayerState(EDataLayerRuntimeState)` | `NewState` | `bool` | Set Stage's DataLayer state |
| `GetStageDataLayerState()` | - | `EDataLayerRuntimeState` | Get Stage's DataLayer state |
| `GetActDataLayerAsset(int32)` | `ActID` | `UDataLayerAsset*` | Get Act's DataLayer asset |

#### State Lock API (for Subsystem Control)

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `ForceStageStateOverride(EStageRuntimeState, bool)` | `NewState`, `bLockState` | `void` | Force state, optionally lock |
| `ReleaseStageStateOverride()` | - | `void` | Release state lock |
| `IsStageStateLocked()` | - | `bool` | Check if state is locked |

#### Trigger Zone API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `InitializeTriggerZones()` | - | `void` | Initialize all trigger zones |
| `RegisterTriggerZone(UTriggerZoneComponentBase*)` | `Zone` | `void` | Register a trigger zone |
| `UnregisterTriggerZone(UTriggerZoneComponentBase*)` | `Zone` | `void` | Unregister a trigger zone |
| `GetAllTriggerZones()` | - | `TArray<UTriggerZoneComponentBase*>` | Get all registered zones |
| `GetTriggerZoneCount()` | - | `int32` | Count of registered zones |

#### Registration API (Editor)

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `RegisterProp(AActor*)` | `NewProp` | `int32` | Register Prop, returns PropID |
| `UnregisterProp(int32)` | `PropID` | `void` | Unregister Prop |
| `RemovePropFromAct(int32, int32)` | `PropID`, `ActID` | `void` | Remove Prop from Act |
| `RemoveAct(int32)` | `ActID` | `void` | Remove Act |

#### Events/Delegates

| Delegate | Parameters | Description |
|----------|------------|-------------|
| `OnActActivated` | `int32 ActID` | Fired when Act is activated |
| `OnActDeactivated` | `int32 ActID` | Fired when Act is deactivated |
| `OnActiveActsChanged` | - | Fired when active Acts list changes |
| `OnStagePropStateChanged` | `int32 PropID, int32 OldState, int32 NewState` | Fired when any Prop state changes |

---

### UStagePropComponent

**File:** `Source/StageEditorRuntime/Public/Components/StagePropComponent.h`

Core component that makes any Actor a controllable Prop in the Stage system.

#### Properties

| Property | Type | Description |
|----------|------|-------------|
| `SUID` | `FSUID` | Stage Unique ID (StageID.0.PropID) |
| `OwnerStage` | `TSoftObjectPtr<AStage>` | Reference to owning Stage |
| `PropState` | `int32` | Current state value |
| `PreviousPropState` | `int32` | Previous state value |

#### State Control API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `SetPropState(int32, bool)` | `NewState`, `bForce` | `void` | Set new state |
| `GetPropState()` | - | `int32` | Get current state |
| `GetPreviousPropState()` | - | `int32` | Get previous state |
| `IncrementState()` | - | `void` | Increment state by 1 |
| `DecrementState()` | - | `void` | Decrement state by 1 |
| `ToggleState(int32, int32)` | `StateA`, `StateB` | `void` | Toggle between two states |

#### Query API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `GetPropID()` | - | `int32` | Get PropID from SUID |
| `GetOwnerStageID()` | - | `int32` | Get owning Stage's ID |
| `GetOwnerStage()` | - | `AStage*` | Get owning Stage Actor |
| `IsRegisteredToStage()` | - | `bool` | Check if registered |

#### Events/Delegates

| Delegate | Parameters | Description |
|----------|------------|-------------|
| `OnPropStateChanged` | `int32 NewState, int32 OldState` | Fired when PropState changes |

---

### AProp

**File:** `Source/StageEditorRuntime/Public/Actors/Prop.h`

Convenience base class for Prop Actors. Automatically includes `UStagePropComponent`.

#### Properties

| Property | Type | Description |
|----------|------|-------------|
| `PropComponent` | `UStagePropComponent*` | The core Prop component |

#### Convenience Wrappers

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `SetPropState(int32, bool)` | `NewState`, `bForce` | `void` | Delegates to component |
| `GetPropState()` | - | `int32` | Delegates to component |

---

### UTriggerZoneComponentBase

**File:** `Source/StageEditorRuntime/Public/Components/TriggerZoneComponentBase.h`

Base class for all TriggerZone components. Provides overlap detection with configurable filtering.

#### Properties

| Property | Type | Description |
|----------|------|-------------|
| `Description` | `FTriggerZoneDescription` | Zone documentation |
| `OwnerStage` | `TSoftObjectPtr<AStage>` | Stage this zone is bound to |
| `TriggerActorTags` | `TArray<FName>` | Tags for filtering |
| `bMustBePawn` | `bool` | Require Pawn class |
| `bZoneEnabled` | `bool` | Enable/disable zone |

#### API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `GetDescriptionText()` | - | `FString` | Get readable description |
| `GetDescription()` | - | `FTriggerZoneDescription&` | Get description struct |
| `ApplyDescriptionPreset(ETriggerZonePreset)` | `Preset` | `void` | Apply template |
| `IsBoundToStage()` | - | `bool` | Check if bound to Stage |
| `GetOwnerStage()` | - | `AStage*` | Get owning Stage |
| `ShouldTriggerForActor(AActor*)` | `Actor` | `bool` | Check if actor triggers |
| `SetZoneEnabled(bool)` | `bEnabled` | `void` | Enable/disable zone |
| `IsZoneEnabled()` | - | `bool` | Check if enabled |

#### Events/Delegates

| Delegate | Parameters | Description |
|----------|------------|-------------|
| `OnActorEnter` | `UTriggerZoneComponentBase* Zone, AActor* Actor` | Fired when valid actor enters |
| `OnActorExit` | `UTriggerZoneComponentBase* Zone, AActor* Actor` | Fired when valid actor exits |

---

### UStageTriggerZoneComponent

**File:** `Source/StageEditorRuntime/Public/Components/StageTriggerZoneComponent.h`

Specialized TriggerZone for Stage state management. Extends `UTriggerZoneComponentBase`.

#### Properties

| Property | Type | Description |
|----------|------|-------------|
| `ZoneType` | `EStageTriggerZoneType` | LoadZone or ActivateZone |
| `OnEnterAction` | `ETriggerZoneDefaultAction` | Preset action on enter |
| `OnExitAction` | `ETriggerZoneDefaultAction` | Preset action on exit |

#### EStageTriggerZoneType

```cpp
enum class EStageTriggerZoneType : uint8
{
    LoadZone,      // Outer zone - triggers DataLayer loading
    ActivateZone   // Inner zone - triggers Stage activation
};
```

#### API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `BindToStage(AStage*)` | `Stage` | `void` | Bind for direct forwarding |
| `UnbindFromStage()` | - | `void` | Unbind from Stage |
| `GetBoundStage()` | - | `AStage*` | Get bound Stage |
| `IsBoundToStageForForwarding()` | - | `bool` | Check if bound |

---

### ATriggerZoneActor

**File:** `Source/StageEditorRuntime/Public/Actors/TriggerZoneActor.h`

Convenience Actor combining AProp + UTriggerZoneComponentBase.

**PropState controls zone enabled state:**
- `PropState == 0`: Zone disabled
- `PropState != 0`: Zone enabled

#### Properties

| Property | Type | Description |
|----------|------|-------------|
| `TriggerZoneComponent` | `UTriggerZoneComponentBase*` | The trigger zone component |

#### API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `GetTriggerZone()` | - | `UTriggerZoneComponentBase*` | Get trigger component |
| `IsZoneEnabled()` | - | `bool` | Check if zone enabled |
| `SetZoneEnabled(bool)` | `bEnabled` | `void` | Enable/disable via PropState |

---

### UStageManagerSubsystem

**File:** `Source/StageEditorRuntime/Public/Subsystems/StageManagerSubsystem.h`

World Subsystem for managing Stage registration, ID allocation, and cross-Stage communication.

#### Stage Registration API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `RegisterStage(AStage*)` | `Stage` | `int32` | Register Stage, get ID |
| `UnregisterStage(int32)` | `StageID` | `void` | Unregister Stage |
| `GetStage(int32)` | `StageID` | `AStage*` | Get Stage by ID |
| `GetAllStages()` | - | `TArray<AStage*>` | Get all registered Stages |
| `IsStageIDRegistered(int32)` | `StageID` | `bool` | Check if ID is registered |
| `GetNextStageID()` | - | `int32` | Preview next available ID |
| `GetRegisteredStageCount()` | - | `int32` | Count of registered Stages |

#### Cross-Stage Control API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `ForceActivateStage(int32, bool)` | `StageID`, `bLockState` | `void` | Force Stage to Active |
| `ForceUnloadStage(int32, bool)` | `StageID`, `bLockState` | `void` | Force Stage to Unloaded |
| `ForceStageState(int32, EStageRuntimeState, bool)` | `StageID`, `NewState`, `bLockState` | `void` | Force any state |
| `ReleaseStageOverride(int32)` | `StageID` | `void` | Release state lock |
| `IsStageOverridden(int32)` | `StageID` | `bool` | Check if overridden |
| `GetStageOverrideState(int32, EStageRuntimeState&)` | `StageID`, `OutState` | `bool` | Get override state |
| `ReleaseAllStageOverrides()` | - | `void` | Release all locks |

#### Debug Watch API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `WatchStage(int32)` | `StageID` | `bool` | Add Stage to watch list |
| `UnwatchStage(int32)` | `StageID` | `bool` | Remove from watch list |
| `ClearWatchList()` | - | `void` | Clear all watches |
| `WatchAllStages()` | - | `void` | Watch all registered Stages |
| `WatchOnlyStage(int32)` | `StageID` | `bool` | Watch single Stage only |
| `IsStageWatched(int32)` | `StageID` | `bool` | Check if being watched |
| `GetWatchedStageIDs()` | - | `TArray<int32>` | Get watched IDs |
| `GetWatchedStageCount()` | - | `int32` | Count of watched Stages |
| `IsWatchListEmpty()` | - | `bool` | Check if watch list empty |

---

## Editor Classes

### FStageEditorController

**File:** `Source/StageEditor/Public/EditorLogic/StageEditorController.h`

Logic controller for the Stage Editor. Handles transactions and data modification.

#### Core API

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `SetActiveStage(AStage*)` | `NewStage` | `void` | Set Stage being edited |
| `GetActiveStage()` | - | `AStage*` | Get active Stage |
| `GetFoundStages()` | - | `TArray<TWeakObjectPtr<AStage>>&` | Get all found Stages |
| `Initialize()` | - | `void` | Initialize controller |
| `FindStageInWorld()` | - | `void` | Scan world for Stages |

#### Act Management

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `CreateNewAct()` | - | `int32` | Create new Act, returns ActID |
| `DeleteAct(int32)` | `ActID` | `bool` | Delete Act (not Default Act) |
| `PreviewAct(int32)` | `ActID` | `void` | Preview Act's Prop states |

#### Prop Management

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `RegisterProps(TArray<AActor*>, AStage*)` | `Actors`, `TargetStage` | `bool` | Register actors as Props |
| `SetPropStateInAct(int32, int32, int32)` | `PropID`, `ActID`, `NewState` | `bool` | Set Prop state in Act |
| `RemovePropFromAct(int32, int32)` | `PropID`, `ActID` | `bool` | Remove Prop from Act |
| `RemoveAllPropsFromAct(int32)` | `ActID` | `bool` | Remove all Props from Act |
| `UnregisterProp(int32)` | `PropID` | `bool` | Unregister Prop entirely |
| `UnregisterAllProps()` | - | `bool` | Unregister all Props |

#### DataLayer Management

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `CreateDataLayerAsset(FString, FString)` | `AssetName`, `FolderPath` | `UDataLayerAsset*` | Create DataLayer asset |
| `GetOrCreateInstanceForAsset(UDataLayerAsset*)` | `Asset` | `UDataLayerInstance*` | Get/create instance |
| `CreateDataLayerForStage(AStage*)` | `Stage` | `bool` | Create Stage's root DataLayer |
| `CreateDataLayerForAct(int32)` | `ActID` | `bool` | Create Act's DataLayer |
| `DeleteDataLayerForAct(int32)` | `ActID` | `bool` | Delete Act's DataLayer |
| `AssignDataLayerToAct(int32, UDataLayerAsset*)` | `ActID`, `DataLayer` | `bool` | Assign existing DataLayer |
| `SyncPropToDataLayer(int32, int32)` | `PropID`, `ActID` | `bool` | Add Prop to Act's DataLayer |
| `AssignPropToStageDataLayer(int32)` | `PropID` | `bool` | Add Prop to Stage DataLayer |
| `RemovePropFromStageDataLayer(int32)` | `PropID` | `bool` | Remove Prop from Stage DataLayer |

#### Blueprint Creation

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `CreateStageBlueprint(FString)` | `DefaultPath` | `void` | Create Stage Blueprint |
| `CreatePropBlueprint(FString)` | `DefaultPath` | `void` | Create Prop Blueprint |

#### World Partition

| Method | Parameters | Return | Description |
|--------|------------|--------|-------------|
| `IsWorldPartitionActive()` | - | `bool` | Check if WP enabled |
| `ConvertToWorldPartition()` | - | `void` | Convert world to WP |

#### Delegates

| Delegate | Description |
|----------|-------------|
| `OnModelChanged` | Broadcasts when Stage data changes (for UI update) |

---

## Debug & Settings

### AStageDebugHUD

**File:** `Source/StageEditorRuntime/Public/Debug/StageDebugHUD.h`

Debug HUD that draws Stage status information on screen.

**Enable via:**
- Console command: `Stage.Debug 1`
- Project Settings → Plugins → Stage Editor → Enable Debug HUD

---

### UStageDebugSettings

**File:** `Source/StageEditorRuntime/Public/Debug/StageDebugSettings.h`

Stage Debug HUD Settings (Project Settings → Plugins → Stage Editor).

#### Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `bEnableDebugHUD` | `bool` | `false` | Enable/disable HUD |
| `DisplayPosition` | `EStageDebugPosition` | `TopLeft` | Screen position |
| `CustomOffset` | `FVector2D` | `(50, 50)` | Custom position offset |
| `bDetailedMode` | `bool` | `true` | Show detailed info |
| `TextScale` | `float` | `1.0` | Text scale (0.5-3.0) |
| `ScreenMargin` | `float` | `50.0` | Margin from edge (0-200) |

#### EStageDebugPosition

```cpp
enum class EStageDebugPosition : uint8
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Custom
};
```

---

## Delegates Reference

### Stage Delegates

| Delegate | Signature | Broadcast When |
|----------|-----------|----------------|
| `FOnActActivated` | `(int32 ActID)` | Act added to ActiveActIDs |
| `FOnActDeactivated` | `(int32 ActID)` | Act removed from ActiveActIDs |
| `FOnActiveActsChanged` | `()` | Active Acts list changes |
| `FOnStagePropStateChanged` | `(int32 PropID, int32 OldState, int32 NewState)` | Any Prop state changes |

### Prop Delegates

| Delegate | Signature | Broadcast When |
|----------|-----------|----------------|
| `FOnPropStateChanged` | `(int32 NewState, int32 OldState)` | PropState changes |

### TriggerZone Delegates

| Delegate | Signature | Broadcast When |
|----------|-----------|----------------|
| `FOnTriggerZoneActorEnter` | `(UTriggerZoneComponentBase* Zone, AActor* Actor)` | Valid actor enters |
| `FOnTriggerZoneActorExit` | `(UTriggerZoneComponentBase* Zone, AActor* Actor)` | Valid actor exits |

### Controller Delegates

| Delegate | Signature | Broadcast When |
|----------|-----------|----------------|
| `FOnStageModelChanged` | `()` | Stage data modified |

---

## Usage Examples

### Basic Stage Control (Blueprint)

```cpp
// Get Stage reference and activate it
AStage* MyStage = GetStageFromLevel();

// Simple state control
MyStage->ActivateStage();   // Load and activate
MyStage->UnloadStage();     // Unload Stage

// Or use explicit state
MyStage->GotoState(EStageState::Loaded);  // Preload only
```

### Act Management (Blueprint)

```cpp
AStage* Stage = GetStage();

// Activate an Act
Stage->ActivateAct(2);  // ActID = 2

// Check if Act is active
if (Stage->IsActActive(2))
{
    // Act 2 is active
}

// Get the controlling Act for a Prop
int32 ControllingAct = Stage->GetControllingActForProp(PropID);
```

### Prop State Control (Blueprint)

```cpp
// Get component from Actor
UStagePropComponent* PropComp = Actor->FindComponentByClass<UStagePropComponent>();

// Bind to state change event
PropComp->OnPropStateChanged.AddDynamic(this, &UMyClass::HandlePropStateChanged);

// Set state directly
PropComp->SetPropState(1);

// Toggle between states
PropComp->ToggleState(0, 1);  // Toggle between 0 and 1
```

### Cross-Stage Control (Blueprint)

```cpp
// Get Subsystem
UStageManagerSubsystem* Subsystem = GetWorld()->GetSubsystem<UStageManagerSubsystem>();

// Force activate another Stage
Subsystem->ForceActivateStage(TargetStageID, true);  // Lock in Active

// Release when done
Subsystem->ReleaseStageOverride(TargetStageID);
```

### TriggerZone Setup (Blueprint)

```cpp
// In Blueprint Event Graph, bind to TriggerZone events:
// - OnActorEnter
// - OnActorExit

// Get owning Stage and call methods
void OnActorEnterHandler(UTriggerZoneComponentBase* Zone, AActor* Actor)
{
    AStage* Stage = Zone->GetOwnerStage();
    if (Stage)
    {
        Stage->ActivateAct(2);
    }
}
```

---

## Transaction Support

All editor modifications in `FStageEditorController` are wrapped in `FScopedTransaction` for Undo/Redo support:

```cpp
// Example from Controller
FScopedTransaction Transaction(NSLOCTEXT("StageEditor", "CreateAct", "Create New Act"));
StageActor->Modify();
// ... perform modifications ...
```

---

## Notes

1. **Default Act (ID=1):** Created automatically with each Stage, cannot be deleted
2. **PropIDs start from 1:** 0 is invalid/unregistered
3. **Soft References:** `PropRegistry` uses `TSoftObjectPtr` to support level streaming
4. **State Priority:** When multiple Acts define the same Prop, highest priority (last activated) wins
5. **DataLayer Hierarchy:** Stage DataLayer is parent, Act DataLayers are children
