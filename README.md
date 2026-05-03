# StageEditor

**Stage-Act-Entity** architecture plugin for Unreal Engine 5.6 — a dynamic stage management system using DataLayers for level streaming and scene state control.

[中文文档](README_CN.md)

## Overview

StageEditor uses a theatrical metaphor to manage scene composition:

| Concept | Description |
|---------|-------------|
| **Stage** | Root unit of scene management — controls lifecycle of all child Acts and Entities |
| **Act** | Scene state configuration — each Act defines which Entities are active and their states |
| **Entity** | Any game object managed by the Stage system (NPCs, props, vehicles, monsters, etc.) |

### Key Features

- **DataLayer Integration**: Import, Sync, and manage DataLayers from within the editor
- **DataLayer Browser**: Custom SceneOutliner with sync status, SUID, visibility, and action columns
- **StageEditor Panel**: Hierarchical tree view (Stage → Acts → Entities) with drag & drop editing
- **Blueprint Support**: Full Blueprint inheritance for Stage, Entity, and TriggerZone actors
- **Transaction System**: Complete Undo/Redo support for all editor operations
- **Localization**: Chinese and English bilingual support
- **Entity Safety**: Orphan detection, single-Stage constraint, deletion confirmation

## Architecture

```
Plugins/StageEditor/Source/
├── StageEditorRuntime/     # Runtime module (zero editor dependencies)
│   ├── Actors/             # AStage, AStageEntity, ATriggerZoneActor
│   ├── Components/         # UStageEntityComponent, trigger zone components
│   ├── Core/               # FAct, FSUID, core types
│   ├── Data/               # StageRegistryAsset (persistent data)
│   └── Subsystems/         # UStageManagerSubsystem
│
└── StageEditor/            # Editor module
    ├── EditorLogic/        # FStageEditorController (MVC Controller)
    ├── EditorUI/           # StageEditorPanel, SceneOutliner integration
    ├── DataLayerSync/      # Sync status, importer, synchronizer, outliner
    └── Subsystems/         # UStageEditorSubsystem (Registry management)
```

**MVC Pattern**: Model (Runtime) → Controller (FStageEditorController) → View (Slate UI). No direct View→Model communication.

## Modules

- `StageEditorRuntime` — Core data structures and runtime logic (Runtime module)
- `StageEditor` — Editor tools, UI, and DataLayer integration (Editor module)

## Prerequisites

| Requirement | Version | Notes |
|-------------|---------|-------|
| **Unreal Engine** | 5.6+ | Source build recommended for full DataLayer API access |
| **World Partition** | — | Required for test maps and DataLayer streaming features |
| **C++ Compiler** | Visual Studio 2022 | Required only if compiling from source (Method B) |

> **Note:** DataLayer management features require World Partition to be enabled on your map. Traditional (non-WP) levels can use Stage-Act-Entity state management but won't have DataLayer integration.

## Installation

Choose the method that fits your workflow:

### Method A: Pre-compiled Plugin (Quick Start)

This method uses the included pre-compiled binaries — no compilation needed.

1. Copy the `Plugins/StageEditor` directory into your UE 5.6 project's `Plugins/` folder
2. Restart the Unreal Editor or regenerate project files
3. Enable the plugin via **Edit → Plugins → Level Design → StageEditor**

> **About `NoCode: true`**: The plugin descriptor has `"NoCode": true`, meaning the editor will load the pre-compiled binaries (`Binaries/Win64/`) instead of compiling from source. This works out-of-the-box for Win64 Editor builds.

### Method B: Compile from Source

Use this method if you need to modify C++ source code or target a different platform.

1. Copy the `Plugins/StageEditor` directory into your UE 5.6 project's `Plugins/` folder
2. Open `StageEditor.uplugin` and set `"NoCode"` to `false`
3. Regenerate project files (right-click `.uproject` → "Generate Visual Studio project files")
4. Build the project in **Development Editor** configuration (`Win64`)
5. The compiled binaries will replace the pre-compiled ones in `Binaries/Win64/`

> If you encounter missing `#include` errors during compilation, check the `Docs/` folder for troubleshooting guides.

### Method C: Git Clone

```bash
cd YourProject/Plugins
git clone git@github.com:SuperRed-Hong/StageEditorRepo.git StageEditor
```

Then follow Method A or B above.

## Test Level

The plugin includes one test level demonstrating the core features:

### EventRealTest — Elevator Demo

| Property | Value |
|----------|-------|
| **Map** | `Content/Maps/EventRealTest/EventTestMap.umap` |
| **Type** | World Partition (WP) |
| **Blueprint Actors** | `BP_Stage_Elevator`, `BP_EntityActor_Elevator`, `Cube7_Blueprint` |
| **DataLayers** | DL_Act_1_1 through DL_Act_1_5, DL_Stage_1 |

**What it demonstrates:**
- Stage-Act-Entity registration and state management
- DataLayer-driven level streaming across 5 Acts
- `OnStageEntityStateChanged` Blueprint event (Stage-level event handling)
- Elevator movement via state changes (State 0 = reset, 1 = move up, 2 = move down)
- World Partition streaming with on-demand Entity reference resolution

**How to test:**

1. Open `EventTestMap` in the editor
2. Open **Window → Stage Editor Panel** to see the Stage/Act/Entity hierarchy
3. Click **Play (PIE)** to enter Play-In-Editor mode
4. Press **E** to toggle elevator up/down between floors
5. Press **R** to reset the elevator to bottom position
6. Observe the Output Log for state change events: `Entity 1 → State X`

**Blueprint events used in this demo:**

| Event | Trigger | Location |
|-------|---------|----------|
| `OnStageEntityStateChanged` | `SetEntityStateByID()` called | BP_Stage_Elevator |
| `OnEntityStateChanged` | Entity's state changes | BP_EntityActor_Elevator (via StageEntityComponent) |
| `OnActActivated` | Act is activated | Stage Blueprint (optional) |
| `OnActDeactivated` | Act is deactivated | Stage Blueprint (optional) |
| `OnActiveActsChanged` | Active Act set changes | Stage Blueprint (optional) |

> For detailed step-by-step tutorials, see `Docs/Tutorials/StageEventElevator_Tutorial.md` and `Docs/Tutorials/EntityStateEvent_Guide.md`.

## Quick Start: Create Your First Stage

### 1. Create a World Partition Map

1. Create a new level: **File → New Level → Empty Level**
2. Enable World Partition: **Window → World Partition → Enable**

### 2. Create Stage Registry Asset

1. In the Content Browser, navigate to your map's world data folder
2. Right-click → **Miscellaneous → Data Asset → StageRegistryAsset**
3. Name it (e.g., `MyMap_WP_StageRegistry`)

### 3. Place a Stage Actor

1. Drag a `Stage` actor (or a Blueprint subclass) into the level
2. In the **Stage Editor Panel**, select your Stage
3. Click **Set as Active Stage** and link the Registry Asset

### 4. Register Entities

1. Drop any Actor (with `StageEntityComponent`) into the level
2. Select the actor(s) in the viewport
3. In the Stage Editor Panel, click **Register Selected Entities**
4. Entities will appear in the tree under the Default Act (Act 0)

### 5. Create Acts and Assign States

1. In the Stage Editor Panel, click **Create New Act**
2. Assign a DataLayer for streaming control (optional)
3. Drag Entities from the tree into the new Act
4. Set each Entity's desired state value in that Act
5. Click **Preview Act** to preview how the scene will look

### 6. Test in PIE

1. Click **Play**
2. Use Blueprint events (`OnActActivated`, `OnStageEntityStateChanged`) to drive gameplay logic
3. Call `ActivateAct(ActID)` or `SetEntityStateByID(EntityID, State)` from Blueprint

## Blueprint API

### Stage Events (usable directly in Stage Blueprint Event Graph)

| Event | Parameters | Description |
|-------|-----------|-------------|
| `OnStageEntityStateChanged` | EntityID (int), OldState (int), NewState (int) | Called when any registered Entity's state changes |
| `OnActActivated` | ActID (int) | Called when an Act is activated |
| `OnActDeactivated` | ActID (int) | Called when an Act is deactivated |
| `OnActiveActsChanged` | — | Called when the set of active Acts changes |

### Key Functions

| Function | Description |
|----------|-------------|
| `SetEntityStateByID(EntityID, NewState)` | Change an Entity's state (triggers events) |
| `ActivateAct(ActID)` | Activate an Act (applies EntityStateOverrides, loads DataLayer) |
| `DeactivateAct(ActID)` | Deactivate an Act |
| `GetEntityActorByID(EntityID)` | Get Entity Actor reference by ID (on-demand, WP-safe) |
| `GetEntityStateByID(EntityID)` | Get current state of an Entity |

> Always use `GetEntityActorByID()` on-demand in events rather than caching references in `BeginPlay` — this ensures compatibility with World Partition streaming.

## Development Documentation

Comprehensive development documentation is available in the `Docs/` directory:

| Document | Description |
|----------|-------------|
| [Overview.md](Docs/Overview.md) | Full development phase index (Phase 1-28) |
| [StageEventElevator_Tutorial.md](Docs/Tutorials/StageEventElevator_Tutorial.md) | Step-by-step elevator demo tutorial |
| [EntityStateEvent_Guide.md](Docs/Tutorials/EntityStateEvent_Guide.md) | Complete Blueprint event system guide |
| [API_Reference.md](Docs/BlueprintAPI/API_Reference.md) | Blueprint API reference |

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Plugin not showing in Plugins list | Ensure `StageEditor.uplugin` is in the correct `Plugins/StageEditor/` directory |
| "Missing module" error on launch | Recompile the plugin from source (Method B above); the pre-compiled binary may be for a different engine version |
| DataLayer features not working | Make sure World Partition is enabled on your map |
| Entity not responding to state changes | Ensure you're using `SetEntityStateByID()` (not direct property assignment) and Entity is registered to the Stage |
| "Accessed None" on Entity reference | Don't cache Entity references in BeginPlay — use `GetEntityActorByID()` on-demand instead (see Blueprint API) |
| Compilation errors (Method B) | Check `Docs/` for include dependency troubleshooting; set `bUseUnity = false` in Build.cs for validation |

## License

MIT
