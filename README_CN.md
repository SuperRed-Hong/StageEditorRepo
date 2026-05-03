# StageEditor（舞台编辑器）

**Stage-Act-Entity** 架构的 Unreal Engine 5.6 插件——基于 DataLayer 实现动态舞台管理的关卡流式加载与场景状态控制系统。

[English](README.md)

## 概述

StageEditor 使用剧院的隐喻来管理场景组合：

| 概念 | 说明 |
|------|------|
| **Stage（舞台）** | 场景管理的根单位——控制所有子 Act 和 Entity 的生命周期 |
| **Act（幕）** | 场景状态配置——每个 Act 定义哪些 Entity 处于激活状态及其状态值 |
| **Entity（实体）** | 任何被 Stage 系统管理的游戏对象（NPC、道具、载具、怪物等） |

## 核心功能

- **DataLayer 集成**: 从编辑器中导入、同步、管理 DataLayer
- **DataLayer 浏览器**: 基于 SceneOutliner 框架的自定义浏览器，支持同步状态、SUID、可见性、操作列
- **StageEditorPanel**: 层级树形视图（Stage → Acts → Entities），支持拖拽编辑
- **Blueprint 支持**: Stage、Entity、TriggerZone 均支持蓝图继承
- **事务系统**: 所有编辑器操作均支持完整的 Undo/Redo
- **本地化**: 中英文双语支持
- **Entity 安全管理**: 孤立检测、单 Stage 约束、删除确认对话框

## 架构

```
Plugins/StageEditor/Source/
├── StageEditorRuntime/     # Runtime 模块（零编辑器依赖）
│   ├── Actors/             # AStage, AStageEntity, ATriggerZoneActor
│   ├── Components/         # UStageEntityComponent, 触发区域组件
│   ├── Core/               # FAct, FSUID, 核心类型定义
│   ├── Data/               # StageRegistryAsset（持久化数据）
│   └── Subsystems/         # UStageManagerSubsystem
│
└── StageEditor/            # Editor 模块
    ├── EditorLogic/        # FStageEditorController（MVC 控制器）
    ├── EditorUI/           # StageEditorPanel, SceneOutliner 集成
    ├── DataLayerSync/      # 同步状态检测、导入、同步器、浏览器
    └── Subsystems/         # UStageEditorSubsystem（Registry 管理）
```

**MVC 模式**: Model (Runtime) → Controller (FStageEditorController) → View (Slate UI)。严格禁止 View 直接操作 Model。

## 模块

- `StageEditorRuntime` — 核心数据结构和运行时逻辑（Runtime 模块）
- `StageEditor` — 编辑器工具、UI、DataLayer 集成（Editor 模块）

## 环境要求

| 要求 | 版本 | 说明 |
|------|------|------|
| **Unreal Engine** | 5.6+ | 推荐使用源码版以获取完整 DataLayer API 访问 |
| **World Partition** | — | 测试关卡和 DataLayer 流式功能需要 |
| **C++ 编译器** | Visual Studio 2022 | 仅从源码编译时需要（方法 B） |

> **注意：** DataLayer 管理功能需要关卡启用 World Partition。传统（非 WP）关卡可以使用 Stage-Act-Entity 状态管理，但无法使用 DataLayer 集成功能。

## 安装

根据你的工作流选择适合的方式：

### 方法 A：预编译插件（快速开始）

此方法使用插件自带的预编译二进制文件，无需编译。

1. 将 `Plugins/StageEditor` 目录复制到你的 UE 5.6 项目的 `Plugins/` 文件夹
2. 重启 Unreal Editor 或重新生成项目文件
3. 通过 **Edit → Plugins → Level Design → StageEditor** 启用插件

> **关于 `NoCode: true`**：插件描述文件设置了 `"NoCode": true`，意味着编辑器将加载预编译的二进制文件（`Binaries/Win64/`）而不是从源码编译。对于 Win64 Editor 构建，开箱即用。

### 方法 B：从源码编译

如果需要修改 C++ 源码或面向不同平台，请使用此方法。

1. 将 `Plugins/StageEditor` 目录复制到你的 UE 5.6 项目的 `Plugins/` 文件夹
2. 打开 `StageEditor.uplugin`，将 `"NoCode"` 设为 `false`
3. 重新生成项目文件（右键 `.uproject` → "Generate Visual Studio project files"）
4. 以 **Development Editor** 配置编译项目（`Win64`）
5. 编译后的二进制文件将替换 `Binaries/Win64/` 中的预编译版本

> 编译时遇到 `#include` 缺失错误，请查看 `Docs/` 文件夹中的排错指南。

### 方法 C：Git 克隆

```bash
cd YourProject/Plugins
git clone git@github.com:SuperRed-Hong/StageEditorRepo.git StageEditor
```

然后按照方法 A 或 B 完成安装。

## 测试关卡

插件包含一个演示核心功能的测试关卡：

### EventRealTest — 电梯演示

| 属性 | 值 |
|------|-----|
| **地图** | `Content/Maps/EventRealTest/EventTestMap.umap` |
| **类型** | World Partition (WP) |
| **蓝图 Actor** | `BP_Stage_Elevator`, `BP_EntityActor_Elevator`, `Cube7_Blueprint` |
| **DataLayer** | DL_Act_1_1 到 DL_Act_1_5, DL_Stage_1 |

**演示内容：**
- Stage-Act-Entity 注册与状态管理
- 跨 5 个 Act 的 DataLayer 驱动关卡流式加载
- `OnStageEntityStateChanged` Blueprint 事件（Stage 级别事件处理）
- 通过状态变化控制电梯运动（状态 0 = 重置，1 = 上升，2 = 下降）
- World Partition 流式加载与按需 Entity 引用解析

**测试步骤：**

1. 在编辑器中打开 `EventTestMap`
2. 打开 **Window → Stage Editor Panel** 查看 Stage/Act/Entity 层级
3. 点击 **Play (PIE)** 进入运行模式
4. 按 **E** 键切换电梯上升/下降
5. 按 **R** 键将电梯重置到底部
6. 观察 Output Log 中的状态变化事件：`Entity 1 → State X`

**本演示使用的 Blueprint 事件：**

| 事件 | 触发时机 | 位置 |
|------|---------|------|
| `OnStageEntityStateChanged` | 调用 `SetEntityStateByID()` | BP_Stage_Elevator |
| `OnEntityStateChanged` | Entity 状态变化 | BP_EntityActor_Elevator（通过 StageEntityComponent） |
| `OnActActivated` | Act 被激活 | Stage Blueprint（可选） |
| `OnActDeactivated` | Act 被停用 | Stage Blueprint（可选） |
| `OnActiveActsChanged` | 活跃 Act 集合变化 | Stage Blueprint（可选） |

> 详细的分步教程请参见 `Docs/Tutorials/StageEventElevator_Tutorial.md` 和 `Docs/Tutorials/EntityStateEvent_Guide.md`。

## 快速入门：创建你的第一个 Stage

### 1. 创建 World Partition 地图

1. 创建新关卡：**File → New Level → Empty Level**
2. 启用 World Partition：**Window → World Partition → Enable**

### 2. 创建 Stage Registry 资产

1. 在 Content Browser 中，导航到地图的世界数据文件夹
2. 右键 → **Miscellaneous → Data Asset → StageRegistryAsset**
3. 命名（例如 `MyMap_WP_StageRegistry`）

### 3. 放置 Stage Actor

1. 将 `Stage` actor（或蓝图子类）拖入关卡
2. 在 **Stage Editor Panel** 中，选择你的 Stage
3. 点击 **Set as Active Stage** 并关联 Registry Asset

### 4. 注册 Entity

1. 将带有 `StageEntityComponent` 的 Actor 放入关卡
2. 在视口中选中 actor
3. 在 Stage Editor Panel 中，点击 **Register Selected Entities**
4. Entity 将出现在 Default Act（Act 0）下的树中

### 5. 创建 Act 并分配状态

1. 在 Stage Editor Panel 中，点击 **Create New Act**
2. 分配 DataLayer 用于流式控制（可选）
3. 将 Entity 从树中拖入新 Act
4. 设置每个 Entity 在该 Act 中的目标状态值
5. 点击 **Preview Act** 预览场景效果

### 6. PIE 测试

1. 点击 **Play**
2. 使用 Blueprint 事件（`OnActActivated`、`OnStageEntityStateChanged`）驱动游戏逻辑
3. 从 Blueprint 调用 `ActivateAct(ActID)` 或 `SetEntityStateByID(EntityID, State)`

## Blueprint API

### Stage 事件（可直接在 Stage Blueprint Event Graph 中使用）

| 事件 | 参数 | 说明 |
|------|------|------|
| `OnStageEntityStateChanged` | EntityID (int), OldState (int), NewState (int) | 任意已注册 Entity 状态变化时调用 |
| `OnActActivated` | ActID (int) | Act 被激活时调用 |
| `OnActDeactivated` | ActID (int) | Act 被停用时调用 |
| `OnActiveActsChanged` | — | 活跃 Act 集合变化时调用 |

### 关键函数

| 函数 | 说明 |
|------|------|
| `SetEntityStateByID(EntityID, NewState)` | 修改 Entity 状态（触发事件） |
| `ActivateAct(ActID)` | 激活 Act（应用 EntityStateOverrides，加载 DataLayer） |
| `DeactivateAct(ActID)` | 停用 Act |
| `GetEntityActorByID(EntityID)` | 按 ID 获取 Entity Actor 引用（按需获取，兼容 WP） |
| `GetEntityStateByID(EntityID)` | 获取 Entity 当前状态 |

> **重要：** 始终在事件中按需使用 `GetEntityActorByID()`，不要在 `BeginPlay` 中缓存引用——这确保兼容 World Partition 流式加载。

## 状态管理流程

```
1. Stage 激活 Act
2. Stage 读取 Act 的 EntityStateOverrides
3. Stage 调用每个受影响 Entity 的 SetEntityState()
4. Entity 广播 OnEntityStateChanged 委托
5. Blueprint 逻辑解释状态值（数据驱动行为）
```

## 关键设计决策

1. **组件式 Entity 系统**: `UStageEntityComponent` 可添加到任何 Actor；注册时自动注入
2. **软引用**: `EntityRegistry` 使用 `TSoftObjectPtr<AActor>` 支持关卡流式加载
3. **默认 Act**: Act 0 为保留的"Default Act"（构造函数中创建，不可删除）
4. **MVC 严格分离**: View 不直接访问 Model；所有操作通过 Controller
5. **事务支持**: 所有修改包装在 `FScopedTransaction` 中
6. **DataLayer 集成**: 每个 Act 可关联 `DataLayerAsset` 实现动态资源流式加载
7. **Editor-Only 逻辑**: Runtime 模块零编辑器依赖
8. **单 Stage 约束**: 一个 Entity 同一时间只能注册到一个 Stage
9. **孤立 Entity 检测**: Entity 追踪 OwnerStage 有效性；提供清理工具
10. **显式删除**: Stage 删除需用户通过 UI 按钮确认，不可自动删除

## 开发文档

完整的开发文档请参见 `Docs/` 目录：

| 文档 | 说明 |
|------|------|
| [Overview.md](Docs/Overview.md) | 完整开发阶段索引（Phase 1-28） |
| [StageEventElevator_Tutorial.md](Docs/Tutorials/StageEventElevator_Tutorial.md) | 电梯演示分步教程 |
| [EntityStateEvent_Guide.md](Docs/Tutorials/EntityStateEvent_Guide.md) | Blueprint 事件系统完整指南 |
| [API_Reference.md](Docs/BlueprintAPI/API_Reference.md) | Blueprint API 参考 |

## 常见问题

| 问题 | 解决方法 |
|------|---------|
| 插件列表中找不到插件 | 确认 `StageEditor.uplugin` 在正确的 `Plugins/StageEditor/` 目录中 |
| 启动时报 "Missing module" 错误 | 从源码重新编译插件（方法 B）；预编译二进制可能针对不同引擎版本 |
| DataLayer 功能不工作 | 确认关卡已启用 World Partition |
| Entity 不响应状态变化 | 确认使用 `SetEntityStateByID()`（而非直接赋值），且 Entity 已注册到 Stage |
| Entity 引用报 "Accessed None" | 不要在 BeginPlay 中缓存 Entity 引用——改用按需获取 `GetEntityActorByID()`（参见 Blueprint API） |
| 编译错误（方法 B） | 查看 `Docs/` 中的 include 依赖排错指南；在 Build.cs 中设 `bUseUnity = false` 进行验证 |

## License

MIT
