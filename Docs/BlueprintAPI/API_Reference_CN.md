# StageEditor 插件 - API 参考手册

> **版本:** Phase 1
> **最后更新:** 2025-11-28
> **模块:** StageEditorRuntime (运行时) + StageEditor (编辑器)

---

## 目录

1. [概述](#概述)
2. [核心类型与枚举](#核心类型与枚举)
3. [运行时类](#运行时类)
   - [AStage](#astage)
   - [UStagePropComponent](#ustagepropcomponent)
   - [AProp](#aprop)
   - [UTriggerZoneComponentBase](#utriggerzonecomponentbase)
   - [UStageTriggerZoneComponent](#ustagetriggerzonecomponent)
   - [ATriggerZoneActor](#atriggerzoneactor)
   - [UStageManagerSubsystem](#ustagemanagersubsystem)
4. [编辑器类](#编辑器类)
   - [FStageEditorController](#fstageeditorcontroller)
5. [调试与设置](#调试与设置)
   - [AStageDebugHUD](#astagedebughud)
   - [UStageDebugSettings](#ustagedebugsettings)
6. [委托参考](#委托参考)
7. [使用示例](#使用示例)

---

## 概述

StageEditor 插件为 Unreal Engine 5.6 提供动态舞台管理系统，使用 DataLayers 控制关卡流送和状态变化，采用戏剧隐喻：

- **Stage (舞台)** - "导演"，负责编排场景状态
- **Act (幕)** - 场景配置，定义 Prop 的状态
- **Prop (道具)** - 任何响应 Stage 状态变化的 Actor

### 模块结构

| 模块 | 类型 | 用途 |
|------|------|------|
| `StageEditorRuntime` | 运行时 | 核心数据结构、Actor、组件 |
| `StageEditor` | 编辑器 | 编辑器工具、UI、控制器逻辑 |

---

## 核心类型与枚举

### FSUID

**文件:** `Source/StageEditorRuntime/Public/Core/StageCoreTypes.h`

舞台唯一标识符 - 用于唯一标识 Stage 系统中实体的层级 ID 结构。

```cpp
USTRUCT(BlueprintType)
struct STAGEEDITORRUNTIME_API FSUID
{
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 StageID = 0;    // 全局唯一的 Stage ID

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 ActID = 0;      // Stage 内的 Act ID

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 PropID = 0;     // Stage 内的 Prop ID
};
```

#### 工厂方法

| 方法 | 描述 |
|------|------|
| `MakeStageID(int32)` | 创建 Stage 级别的 SUID (X.0.0) |
| `MakeActID(int32, int32)` | 创建 Act 级别的 SUID (X.Y.0) |
| `MakePropID(int32, int32)` | 创建 Prop 级别的 SUID (X.0.Z) |

#### 实用方法

| 方法 | 返回值 | 描述 |
|------|--------|------|
| `ToString()` | `FString` | 返回 "StageID.ActID.PropID" 格式 |
| `IsValid()` | `bool` | StageID > 0 时返回 true |
| `IsStageLevel()` | `bool` | 仅设置了 StageID 时返回 true |
| `IsActLevel()` | `bool` | 设置了 StageID 和 ActID 时返回 true |
| `IsPropLevel()` | `bool` | 设置了 StageID 和 PropID 时返回 true |

---

### EStageRuntimeState

**文件:** `Source/StageEditorRuntime/Public/Core/StageCoreTypes.h`

Stage DataLayer 生命周期的内部 5 状态机（开发者视图）。

```cpp
UENUM(BlueprintType)
enum class EStageRuntimeState : uint8
{
    Unloaded,     // DataLayer 完全卸载
    Preloading,   // DataLayer 正在加载（过渡状态）
    Loaded,       // DataLayer 已加载但未激活
    Active,       // DataLayer 完全激活
    Unloading     // DataLayer 正在卸载（过渡状态）
};
```

**状态流转:**
```
Unloaded → Preloading → Loaded → Active
             ↑                      ↓
             └── Unloading ←────────┘
```

---

### EStageState

**文件:** `Source/StageEditorRuntime/Public/Core/StageCoreTypes.h`

用户接口的简化 3 状态。

```cpp
UENUM(BlueprintType)
enum class EStageState : uint8
{
    Unloaded,   // Stage 未加载
    Loaded,     // Stage 已加载但未激活
    Active      // Stage 完全激活
};
```

**从 EStageRuntimeState 的映射:**
- `Unloaded`, `Unloading` → `EStageState::Unloaded`
- `Preloading`, `Loaded` → `EStageState::Loaded`
- `Active` → `EStageState::Active`

---

### FAct

**文件:** `Source/StageEditorRuntime/Public/Core/StageCoreTypes.h`

定义 Stage 的"场景"或"状态"配置。

```cpp
USTRUCT(BlueprintType)
struct STAGEEDITORRUNTIME_API FAct
{
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    FSUID SUID;                                    // 唯一 Act ID

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DisplayName;                           // 用户可编辑的名称

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bFollowStageState = false;                // 跟随 Stage 状态

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EDataLayerRuntimeState InitialDataLayerState;  // 不跟随时的初始状态

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TMap<int32, int32> PropStateOverrides;         // PropID → 目标状态

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UDataLayerAsset> AssociatedDataLayer;
};
```

#### 属性

| 属性 | 类型 | 描述 |
|------|------|------|
| `SUID` | `FSUID` | 唯一标识符 (StageID.ActID.0) |
| `DisplayName` | `FString` | 用户可编辑的显示名称 |
| `bFollowStageState` | `bool` | 为 true 时，Act DataLayer 跟随 Stage 状态 |
| `InitialDataLayerState` | `EDataLayerRuntimeState` | Stage 变为 Active 时的初始状态（不跟随时） |
| `PropStateOverrides` | `TMap<int32, int32>` | PropID 到目标状态值的映射 |
| `AssociatedDataLayer` | `UDataLayerAsset*` | 此 Act 的 DataLayer 资产 |

---

### FTriggerZoneDescription

**文件:** `Source/StageEditorRuntime/Public/Core/StageCoreTypes.h`

TriggerZone 文档的结构化描述。

```cpp
USTRUCT(BlueprintType)
struct STAGEEDITORRUNTIME_API FTriggerZoneDescription
{
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ETriggerZonePreset Preset;     // 快速填充模板

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Trigger;               // 谁/什么触发（如 "Player"）

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Condition;             // 何时（前置条件）

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Target;                // 影响什么

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Action;                // 执行什么动作

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Effect;                // 为什么（游戏效果）
};
```

#### 方法

| 方法 | 返回值 | 描述 |
|------|--------|------|
| `ToReadableString()` | `FString` | "当 [Trigger] 进入时，如果 [Condition]，则对 [Target] 执行 [Action] 以达到 [Effect]" |
| `ApplyPreset()` | `void` | 根据选择的 Preset 自动填充字段 |
| `IsEmpty()` | `bool` | 无有意义内容时返回 true |

---

### ETriggerZoneDefaultAction

**文件:** `Source/StageEditorRuntime/Public/Core/StageCoreTypes.h`

```cpp
UENUM(BlueprintType)
enum class ETriggerZoneDefaultAction : uint8
{
    Custom,         // 使用蓝图事件
    LoadStage,      // 调用 Stage->LoadStage()
    ActivateStage,  // 调用 Stage->ActivateStage()
    UnloadStage     // 调用 Stage->UnloadStage()
};
```

---

### ETriggerZonePreset

**文件:** `Source/StageEditorRuntime/Public/Core/StageCoreTypes.h`

```cpp
UENUM(BlueprintType)
enum class ETriggerZonePreset : uint8
{
    Custom,              // 手动输入
    StageLoad,           // 玩家进入 → Stage 预加载
    StageActivate,       // 玩家进入 → Stage 激活
    ActActivate,         // 玩家进入 → Act 激活
    ActDeactivate,       // 玩家进入 → Act 取消激活
    PropStateChange,     // 玩家进入 → Prop 状态改变
    ConditionalTrigger   // 玩家进入 + 条件 → 自定义动作
};
```

---

## 运行时类

### AStage

**文件:** `Source/StageEditorRuntime/Public/Actors/Stage.h`

"导演" Actor，负责编排场景状态。管理 Acts 和 Props，处理 DataLayer 激活。

#### 核心属性

| 属性 | 类型 | 描述 |
|------|------|------|
| `SUID` | `FSUID` | Stage 唯一 ID（只读） |
| `Acts` | `TArray<FAct>` | 所有定义的 Acts |
| `PropRegistry` | `TMap<int32, TSoftObjectPtr<AActor>>` | 已注册的 Props (PropID → Actor) |
| `ActiveActIDs` | `TArray<int32>` | 当前激活的 Acts（优先级顺序） |
| `CurrentStageState` | `EStageRuntimeState` | 当前运行时状态 |
| `StageDataLayerAsset` | `UDataLayerAsset*` | 此 Stage 的根 DataLayer |

#### 触发区域属性

| 属性 | 类型 | 描述 |
|------|------|------|
| `BuiltInLoadZone` | `UStageTriggerZoneComponent*` | 外部触发区域（加载 Stage） |
| `BuiltInActivateZone` | `UStageTriggerZoneComponent*` | 内部触发区域（激活 Stage） |
| `LoadZoneExtent` | `FVector` | LoadZone 半尺寸（默认: 2000,2000,500） |
| `ActivateZoneExtent` | `FVector` | ActivateZone 半尺寸（默认: 1000,1000,400） |
| `bDisableBuiltInZones` | `bool` | 禁用内置区域（使用外部区域） |
| `InitialStageState` | `EStageRuntimeState` | 禁用区域时的初始状态 |

#### Stage 状态 API（用户接口，3 状态）

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `GetStageState()` | - | `EStageState` | 获取简化状态 |
| `GotoState(EStageState)` | `TargetState` | `void` | 请求状态转换 |
| `LoadStage()` | - | `void` | 等同于 `GotoState(Loaded)` |
| `ActivateStage()` | - | `void` | 等同于 `GotoState(Active)` |
| `UnloadStage()` | - | `void` | 等同于 `GotoState(Unloaded)` |

#### Stage 状态 API（调试，5 状态）

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `GetRuntimeState()` | - | `EStageRuntimeState` | 获取详细内部状态 |
| `GetCurrentStageState()` | - | `EStageRuntimeState` | 获取当前运行时状态 |
| `IsInTransitionState()` | - | `bool` | Preloading 或 Unloading 时返回 true |
| `IsStageLoaded()` | - | `bool` | Loaded 或 Active 时返回 true |
| `IsStageActive()` | - | `bool` | Active 时返回 true |

#### Act 激活 API（多 Act 系统）

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `ActivateAct(int32)` | `ActID` | `void` | 激活 Act，应用 PropStates，加载 DataLayer |
| `DeactivateAct(int32)` | `ActID` | `void` | 取消激活 Act，卸载 DataLayer |
| `ActivateActs(TArray<int32>)` | `ActIDs` | `void` | 激活多个 Acts（最后一个优先级最高） |
| `DeactivateAllActs()` | - | `void` | 取消激活所有活跃 Acts |

#### Act 查询 API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `GetActiveActIDs()` | - | `TArray<int32>` | 获取活跃 Act IDs（优先级顺序） |
| `IsActActive(int32)` | `ActID` | `bool` | 检查 Act 是否活跃 |
| `GetHighestPriorityActID()` | - | `int32` | 获取最近激活的 Act ID |
| `GetRecentActivatedActID()` | - | `int32` | 获取最后激活的 Act ID |
| `GetActiveActCount()` | - | `int32` | 活跃 Acts 数量 |
| `GetActDisplayName(int32)` | `ActID` | `FString` | 获取 Act 显示名称 |
| `GetActPropStates(int32)` | `ActID` | `TMap<int32,int32>` | 获取 Act 的 PropStateOverrides |
| `GetAllActIDs()` | - | `TArray<int32>` | 获取所有 Act IDs |
| `GetActCount()` | - | `int32` | Act 总数 |
| `DoesActExist(int32)` | `ActID` | `bool` | 检查 Act 是否存在 |

#### Act 锁定 API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `LockAct(int32)` | `ActID` | `void` | 防止 Act 被取消激活 |
| `UnlockAct(int32)` | `ActID` | `void` | 允许 Act 被取消激活 |
| `IsActLocked(int32)` | `ActID` | `bool` | 检查 Act 是否被锁定 |
| `GetLockedActIDs()` | - | `TArray<int32>` | 获取所有锁定的 Act IDs |

#### Prop 状态 API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `SetPropStateByID(int32, int32, bool)` | `PropID`, `NewState`, `bForce` | `bool` | 设置 Prop 状态 |
| `GetPropStateByID(int32)` | `PropID` | `int32` | 获取 Prop 状态（未找到返回 -1） |
| `SetMultiplePropStates(TMap<int32,int32>)` | `PropStates` | `void` | 设置多个 Prop 状态 |
| `GetEffectivePropState(int32)` | `PropID` | `int32` | 从活跃 Acts 获取有效状态 |
| `GetControllingActForProp(int32)` | `PropID` | `int32` | 获取控制此 Prop 的 Act |

#### Prop 查询 API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `GetPropActorByID(int32)` | `PropID` | `AActor*` | 获取 Prop Actor |
| `GetPropComponentByID(int32)` | `PropID` | `UStagePropComponent*` | 获取 Prop 的组件 |
| `GetAllPropIDs()` | - | `TArray<int32>` | 获取所有 Prop IDs |
| `GetAllPropActors()` | - | `TArray<AActor*>` | 获取所有 Prop Actors |
| `GetPropCount()` | - | `int32` | Prop 总数 |
| `DoesPropExist(int32)` | `PropID` | `bool` | 检查 Prop 是否存在 |

#### DataLayer API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `SetActDataLayerState(int32, EDataLayerRuntimeState)` | `ActID`, `NewState` | `bool` | 设置 Act 的 DataLayer 状态 |
| `GetActDataLayerState(int32)` | `ActID` | `EDataLayerRuntimeState` | 获取 Act 的 DataLayer 状态 |
| `IsActDataLayerLoaded(int32)` | `ActID` | `bool` | 检查 Act 的 DataLayer 是否已加载 |
| `LoadActDataLayer(int32)` | `ActID` | `bool` | 加载 Act 的 DataLayer |
| `UnloadActDataLayer(int32)` | `ActID` | `bool` | 卸载 Act 的 DataLayer |
| `SetStageDataLayerState(EDataLayerRuntimeState)` | `NewState` | `bool` | 设置 Stage 的 DataLayer 状态 |
| `GetStageDataLayerState()` | - | `EDataLayerRuntimeState` | 获取 Stage 的 DataLayer 状态 |
| `GetActDataLayerAsset(int32)` | `ActID` | `UDataLayerAsset*` | 获取 Act 的 DataLayer 资产 |

#### 状态锁定 API（供 Subsystem 控制）

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `ForceStageStateOverride(EStageRuntimeState, bool)` | `NewState`, `bLockState` | `void` | 强制状态，可选锁定 |
| `ReleaseStageStateOverride()` | - | `void` | 释放状态锁定 |
| `IsStageStateLocked()` | - | `bool` | 检查状态是否被锁定 |

#### 触发区域 API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `InitializeTriggerZones()` | - | `void` | 初始化所有触发区域 |
| `RegisterTriggerZone(UTriggerZoneComponentBase*)` | `Zone` | `void` | 注册触发区域 |
| `UnregisterTriggerZone(UTriggerZoneComponentBase*)` | `Zone` | `void` | 注销触发区域 |
| `GetAllTriggerZones()` | - | `TArray<UTriggerZoneComponentBase*>` | 获取所有已注册区域 |
| `GetTriggerZoneCount()` | - | `int32` | 已注册区域数量 |

#### 注册 API（编辑器）

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `RegisterProp(AActor*)` | `NewProp` | `int32` | 注册 Prop，返回 PropID |
| `UnregisterProp(int32)` | `PropID` | `void` | 注销 Prop |
| `RemovePropFromAct(int32, int32)` | `PropID`, `ActID` | `void` | 从 Act 移除 Prop |
| `RemoveAct(int32)` | `ActID` | `void` | 移除 Act |

#### 事件/委托

| 委托 | 参数 | 描述 |
|------|------|------|
| `OnActActivated` | `int32 ActID` | Act 被激活时触发 |
| `OnActDeactivated` | `int32 ActID` | Act 被取消激活时触发 |
| `OnActiveActsChanged` | - | 活跃 Acts 列表变化时触发 |
| `OnStagePropStateChanged` | `int32 PropID, int32 OldState, int32 NewState` | 任何 Prop 状态变化时触发 |

---

### UStagePropComponent

**文件:** `Source/StageEditorRuntime/Public/Components/StagePropComponent.h`

核心组件，使任何 Actor 成为 Stage 系统中可控的 Prop。

#### 属性

| 属性 | 类型 | 描述 |
|------|------|------|
| `SUID` | `FSUID` | Stage 唯一 ID (StageID.0.PropID) |
| `OwnerStage` | `TSoftObjectPtr<AStage>` | 对所属 Stage 的引用 |
| `PropState` | `int32` | 当前状态值 |
| `PreviousPropState` | `int32` | 上一个状态值 |

#### 状态控制 API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `SetPropState(int32, bool)` | `NewState`, `bForce` | `void` | 设置新状态 |
| `GetPropState()` | - | `int32` | 获取当前状态 |
| `GetPreviousPropState()` | - | `int32` | 获取上一个状态 |
| `IncrementState()` | - | `void` | 状态加 1 |
| `DecrementState()` | - | `void` | 状态减 1 |
| `ToggleState(int32, int32)` | `StateA`, `StateB` | `void` | 在两个状态之间切换 |

#### 查询 API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `GetPropID()` | - | `int32` | 从 SUID 获取 PropID |
| `GetOwnerStageID()` | - | `int32` | 获取所属 Stage 的 ID |
| `GetOwnerStage()` | - | `AStage*` | 获取所属 Stage Actor |
| `IsRegisteredToStage()` | - | `bool` | 检查是否已注册 |

#### 事件/委托

| 委托 | 参数 | 描述 |
|------|------|------|
| `OnPropStateChanged` | `int32 NewState, int32 OldState` | PropState 变化时触发 |

---

### AProp

**文件:** `Source/StageEditorRuntime/Public/Actors/Prop.h`

Prop Actor 的便捷基类。自动包含 `UStagePropComponent`。

#### 属性

| 属性 | 类型 | 描述 |
|------|------|------|
| `PropComponent` | `UStagePropComponent*` | 核心 Prop 组件 |

#### 便捷包装器

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `SetPropState(int32, bool)` | `NewState`, `bForce` | `void` | 委托给组件 |
| `GetPropState()` | - | `int32` | 委托给组件 |

---

### UTriggerZoneComponentBase

**文件:** `Source/StageEditorRuntime/Public/Components/TriggerZoneComponentBase.h`

所有 TriggerZone 组件的基类。提供带可配置过滤的重叠检测。

#### 属性

| 属性 | 类型 | 描述 |
|------|------|------|
| `Description` | `FTriggerZoneDescription` | 区域文档 |
| `OwnerStage` | `TSoftObjectPtr<AStage>` | 此区域绑定的 Stage |
| `TriggerActorTags` | `TArray<FName>` | 过滤用的标签 |
| `bMustBePawn` | `bool` | 要求 Pawn 类 |
| `bZoneEnabled` | `bool` | 启用/禁用区域 |

#### API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `GetDescriptionText()` | - | `FString` | 获取可读描述 |
| `GetDescription()` | - | `FTriggerZoneDescription&` | 获取描述结构体 |
| `ApplyDescriptionPreset(ETriggerZonePreset)` | `Preset` | `void` | 应用模板 |
| `IsBoundToStage()` | - | `bool` | 检查是否绑定到 Stage |
| `GetOwnerStage()` | - | `AStage*` | 获取所属 Stage |
| `ShouldTriggerForActor(AActor*)` | `Actor` | `bool` | 检查 actor 是否触发 |
| `SetZoneEnabled(bool)` | `bEnabled` | `void` | 启用/禁用区域 |
| `IsZoneEnabled()` | - | `bool` | 检查是否启用 |

#### 事件/委托

| 委托 | 参数 | 描述 |
|------|------|------|
| `OnActorEnter` | `UTriggerZoneComponentBase* Zone, AActor* Actor` | 有效 actor 进入时触发 |
| `OnActorExit` | `UTriggerZoneComponentBase* Zone, AActor* Actor` | 有效 actor 离开时触发 |

---

### UStageTriggerZoneComponent

**文件:** `Source/StageEditorRuntime/Public/Components/StageTriggerZoneComponent.h`

用于 Stage 状态管理的专用 TriggerZone。继承自 `UTriggerZoneComponentBase`。

#### 属性

| 属性 | 类型 | 描述 |
|------|------|------|
| `ZoneType` | `EStageTriggerZoneType` | LoadZone 或 ActivateZone |
| `OnEnterAction` | `ETriggerZoneDefaultAction` | 进入时的预设动作 |
| `OnExitAction` | `ETriggerZoneDefaultAction` | 离开时的预设动作 |

#### EStageTriggerZoneType

```cpp
enum class EStageTriggerZoneType : uint8
{
    LoadZone,      // 外部区域 - 触发 DataLayer 加载
    ActivateZone   // 内部区域 - 触发 Stage 激活
};
```

#### API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `BindToStage(AStage*)` | `Stage` | `void` | 绑定以进行直接转发 |
| `UnbindFromStage()` | - | `void` | 从 Stage 解绑 |
| `GetBoundStage()` | - | `AStage*` | 获取绑定的 Stage |
| `IsBoundToStageForForwarding()` | - | `bool` | 检查是否已绑定 |

---

### ATriggerZoneActor

**文件:** `Source/StageEditorRuntime/Public/Actors/TriggerZoneActor.h`

结合 AProp + UTriggerZoneComponentBase 的便捷 Actor。

**PropState 控制区域启用状态:**
- `PropState == 0`: 区域禁用
- `PropState != 0`: 区域启用

#### 属性

| 属性 | 类型 | 描述 |
|------|------|------|
| `TriggerZoneComponent` | `UTriggerZoneComponentBase*` | 触发区域组件 |

#### API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `GetTriggerZone()` | - | `UTriggerZoneComponentBase*` | 获取触发组件 |
| `IsZoneEnabled()` | - | `bool` | 检查区域是否启用 |
| `SetZoneEnabled(bool)` | `bEnabled` | `void` | 通过 PropState 启用/禁用 |

---

### UStageManagerSubsystem

**文件:** `Source/StageEditorRuntime/Public/Subsystems/StageManagerSubsystem.h`

用于管理 Stage 注册、ID 分配和跨 Stage 通信的 World Subsystem。

#### Stage 注册 API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `RegisterStage(AStage*)` | `Stage` | `int32` | 注册 Stage，获取 ID |
| `UnregisterStage(int32)` | `StageID` | `void` | 注销 Stage |
| `GetStage(int32)` | `StageID` | `AStage*` | 通过 ID 获取 Stage |
| `GetAllStages()` | - | `TArray<AStage*>` | 获取所有已注册 Stages |
| `IsStageIDRegistered(int32)` | `StageID` | `bool` | 检查 ID 是否已注册 |
| `GetNextStageID()` | - | `int32` | 预览下一个可用 ID |
| `GetRegisteredStageCount()` | - | `int32` | 已注册 Stages 数量 |

#### 跨 Stage 控制 API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `ForceActivateStage(int32, bool)` | `StageID`, `bLockState` | `void` | 强制 Stage 为 Active |
| `ForceUnloadStage(int32, bool)` | `StageID`, `bLockState` | `void` | 强制 Stage 为 Unloaded |
| `ForceStageState(int32, EStageRuntimeState, bool)` | `StageID`, `NewState`, `bLockState` | `void` | 强制任意状态 |
| `ReleaseStageOverride(int32)` | `StageID` | `void` | 释放状态锁定 |
| `IsStageOverridden(int32)` | `StageID` | `bool` | 检查是否被覆盖 |
| `GetStageOverrideState(int32, EStageRuntimeState&)` | `StageID`, `OutState` | `bool` | 获取覆盖状态 |
| `ReleaseAllStageOverrides()` | - | `void` | 释放所有锁定 |

#### 调试监视 API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `WatchStage(int32)` | `StageID` | `bool` | 将 Stage 添加到监视列表 |
| `UnwatchStage(int32)` | `StageID` | `bool` | 从监视列表移除 |
| `ClearWatchList()` | - | `void` | 清除所有监视 |
| `WatchAllStages()` | - | `void` | 监视所有已注册 Stages |
| `WatchOnlyStage(int32)` | `StageID` | `bool` | 仅监视单个 Stage |
| `IsStageWatched(int32)` | `StageID` | `bool` | 检查是否正在监视 |
| `GetWatchedStageIDs()` | - | `TArray<int32>` | 获取被监视的 IDs |
| `GetWatchedStageCount()` | - | `int32` | 被监视 Stages 数量 |
| `IsWatchListEmpty()` | - | `bool` | 检查监视列表是否为空 |

---

## 编辑器类

### FStageEditorController

**文件:** `Source/StageEditor/Public/EditorLogic/StageEditorController.h`

Stage 编辑器的逻辑控制器。处理事务和数据修改。

#### 核心 API

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `SetActiveStage(AStage*)` | `NewStage` | `void` | 设置正在编辑的 Stage |
| `GetActiveStage()` | - | `AStage*` | 获取活跃 Stage |
| `GetFoundStages()` | - | `TArray<TWeakObjectPtr<AStage>>&` | 获取所有找到的 Stages |
| `Initialize()` | - | `void` | 初始化控制器 |
| `FindStageInWorld()` | - | `void` | 扫描世界中的 Stages |

#### Act 管理

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `CreateNewAct()` | - | `int32` | 创建新 Act，返回 ActID |
| `DeleteAct(int32)` | `ActID` | `bool` | 删除 Act（不能删除 Default Act） |
| `PreviewAct(int32)` | `ActID` | `void` | 预览 Act 的 Prop 状态 |

#### Prop 管理

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `RegisterProps(TArray<AActor*>, AStage*)` | `Actors`, `TargetStage` | `bool` | 将 actors 注册为 Props |
| `SetPropStateInAct(int32, int32, int32)` | `PropID`, `ActID`, `NewState` | `bool` | 在 Act 中设置 Prop 状态 |
| `RemovePropFromAct(int32, int32)` | `PropID`, `ActID` | `bool` | 从 Act 移除 Prop |
| `RemoveAllPropsFromAct(int32)` | `ActID` | `bool` | 从 Act 移除所有 Props |
| `UnregisterProp(int32)` | `PropID` | `bool` | 完全注销 Prop |
| `UnregisterAllProps()` | - | `bool` | 注销所有 Props |

#### DataLayer 管理

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `CreateDataLayerAsset(FString, FString)` | `AssetName`, `FolderPath` | `UDataLayerAsset*` | 创建 DataLayer 资产 |
| `GetOrCreateInstanceForAsset(UDataLayerAsset*)` | `Asset` | `UDataLayerInstance*` | 获取/创建实例 |
| `CreateDataLayerForStage(AStage*)` | `Stage` | `bool` | 创建 Stage 的根 DataLayer |
| `CreateDataLayerForAct(int32)` | `ActID` | `bool` | 创建 Act 的 DataLayer |
| `DeleteDataLayerForAct(int32)` | `ActID` | `bool` | 删除 Act 的 DataLayer |
| `AssignDataLayerToAct(int32, UDataLayerAsset*)` | `ActID`, `DataLayer` | `bool` | 分配现有 DataLayer |
| `SyncPropToDataLayer(int32, int32)` | `PropID`, `ActID` | `bool` | 将 Prop 添加到 Act 的 DataLayer |
| `AssignPropToStageDataLayer(int32)` | `PropID` | `bool` | 将 Prop 添加到 Stage DataLayer |
| `RemovePropFromStageDataLayer(int32)` | `PropID` | `bool` | 从 Stage DataLayer 移除 Prop |

#### 蓝图创建

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `CreateStageBlueprint(FString)` | `DefaultPath` | `void` | 创建 Stage 蓝图 |
| `CreatePropBlueprint(FString)` | `DefaultPath` | `void` | 创建 Prop 蓝图 |

#### World Partition

| 方法 | 参数 | 返回值 | 描述 |
|------|------|--------|------|
| `IsWorldPartitionActive()` | - | `bool` | 检查 WP 是否启用 |
| `ConvertToWorldPartition()` | - | `void` | 将世界转换为 WP |

#### 委托

| 委托 | 描述 |
|------|------|
| `OnModelChanged` | Stage 数据修改时广播（用于 UI 更新） |

---

## 调试与设置

### AStageDebugHUD

**文件:** `Source/StageEditorRuntime/Public/Debug/StageDebugHUD.h`

在屏幕上绘制 Stage 状态信息的调试 HUD。

**启用方式:**
- 控制台命令: `Stage.Debug 1`
- 项目设置 → 插件 → Stage Editor → Enable Debug HUD

---

### UStageDebugSettings

**文件:** `Source/StageEditorRuntime/Public/Debug/StageDebugSettings.h`

Stage 调试 HUD 设置（项目设置 → 插件 → Stage Editor）。

#### 属性

| 属性 | 类型 | 默认值 | 描述 |
|------|------|--------|------|
| `bEnableDebugHUD` | `bool` | `false` | 启用/禁用 HUD |
| `DisplayPosition` | `EStageDebugPosition` | `TopLeft` | 屏幕位置 |
| `CustomOffset` | `FVector2D` | `(50, 50)` | 自定义位置偏移 |
| `bDetailedMode` | `bool` | `true` | 显示详细信息 |
| `TextScale` | `float` | `1.0` | 文字缩放 (0.5-3.0) |
| `ScreenMargin` | `float` | `50.0` | 边缘边距 (0-200) |

#### EStageDebugPosition

```cpp
enum class EStageDebugPosition : uint8
{
    TopLeft,      // 左上角
    TopRight,     // 右上角
    BottomLeft,   // 左下角
    BottomRight,  // 右下角
    Custom        // 自定义位置
};
```

---

## 委托参考

### Stage 委托

| 委托 | 签名 | 触发时机 |
|------|------|----------|
| `FOnActActivated` | `(int32 ActID)` | Act 被添加到 ActiveActIDs 时 |
| `FOnActDeactivated` | `(int32 ActID)` | Act 从 ActiveActIDs 移除时 |
| `FOnActiveActsChanged` | `()` | 活跃 Acts 列表变化时 |
| `FOnStagePropStateChanged` | `(int32 PropID, int32 OldState, int32 NewState)` | 任何 Prop 状态变化时 |

### Prop 委托

| 委托 | 签名 | 触发时机 |
|------|------|----------|
| `FOnPropStateChanged` | `(int32 NewState, int32 OldState)` | PropState 变化时 |

### TriggerZone 委托

| 委托 | 签名 | 触发时机 |
|------|------|----------|
| `FOnTriggerZoneActorEnter` | `(UTriggerZoneComponentBase* Zone, AActor* Actor)` | 有效 actor 进入时 |
| `FOnTriggerZoneActorExit` | `(UTriggerZoneComponentBase* Zone, AActor* Actor)` | 有效 actor 离开时 |

### Controller 委托

| 委托 | 签名 | 触发时机 |
|------|------|----------|
| `FOnStageModelChanged` | `()` | Stage 数据被修改时 |

---

## 使用示例

### 基本 Stage 控制（蓝图）

```cpp
// 获取 Stage 引用并激活
AStage* MyStage = GetStageFromLevel();

// 简单状态控制
MyStage->ActivateStage();   // 加载并激活
MyStage->UnloadStage();     // 卸载 Stage

// 或使用明确的状态
MyStage->GotoState(EStageState::Loaded);  // 仅预加载
```

### Act 管理（蓝图）

```cpp
AStage* Stage = GetStage();

// 激活一个 Act
Stage->ActivateAct(2);  // ActID = 2

// 检查 Act 是否活跃
if (Stage->IsActActive(2))
{
    // Act 2 是活跃的
}

// 获取控制某个 Prop 的 Act
int32 ControllingAct = Stage->GetControllingActForProp(PropID);
```

### Prop 状态控制（蓝图）

```cpp
// 从 Actor 获取组件
UStagePropComponent* PropComp = Actor->FindComponentByClass<UStagePropComponent>();

// 绑定状态变化事件
PropComp->OnPropStateChanged.AddDynamic(this, &UMyClass::HandlePropStateChanged);

// 直接设置状态
PropComp->SetPropState(1);

// 在两个状态之间切换
PropComp->ToggleState(0, 1);  // 在 0 和 1 之间切换
```

### 跨 Stage 控制（蓝图）

```cpp
// 获取 Subsystem
UStageManagerSubsystem* Subsystem = GetWorld()->GetSubsystem<UStageManagerSubsystem>();

// 强制激活另一个 Stage
Subsystem->ForceActivateStage(TargetStageID, true);  // 锁定在 Active 状态

// 完成后释放
Subsystem->ReleaseStageOverride(TargetStageID);
```

### TriggerZone 设置（蓝图）

```cpp
// 在蓝图事件图表中，绑定 TriggerZone 事件:
// - OnActorEnter
// - OnActorExit

// 获取所属 Stage 并调用方法
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

## 事务支持

`FStageEditorController` 中的所有编辑器修改都包装在 `FScopedTransaction` 中以支持撤销/重做:

```cpp
// Controller 中的示例
FScopedTransaction Transaction(NSLOCTEXT("StageEditor", "CreateAct", "Create New Act"));
StageActor->Modify();
// ... 执行修改 ...
```

---

## 注意事项

1. **Default Act (ID=1):** 每个 Stage 自动创建，无法删除
2. **PropIDs 从 1 开始:** 0 表示无效/未注册
3. **软引用:** `PropRegistry` 使用 `TSoftObjectPtr` 以支持关卡流送
4. **状态优先级:** 当多个 Acts 定义同一个 Prop 时，最高优先级（最后激活的）获胜
5. **DataLayer 层级:** Stage DataLayer 是父级，Act DataLayers 是子级
