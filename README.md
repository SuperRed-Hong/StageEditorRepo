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

## Installation

1. Copy the `Plugins/StageEditor` directory into your UE 5.6 project's `Plugins/` folder
2. Regenerate project files (right-click `.uproject` → "Generate Visual Studio project files")
3. Build the project

## License

MIT
