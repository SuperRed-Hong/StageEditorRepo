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

## 安装

1. 将 `Plugins/StageEditor` 目录复制到你的 UE 5.6 项目的 `Plugins/` 文件夹
2. 重新生成项目文件（右键 `.uproject` → "Generate Visual Studio project files"）
3. 编译项目

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

## 开发状态

当前处于 **Phase 28** — 正在设计 UStageDataLayerAsset 自定义 DataLayerAsset 子类。

已完成的核心功能（Phase 1-27）包括：
- Stage-Act-Entity 核心架构 + MVC 模式
- DataLayer 导入 / 同步 / 命名规范
- DataLayerBrowser (SceneOutliner 框架)
- StageEditorPanel 层级树形视图
- StageRegistry 持久化 + 双 Subsystem 架构
- Entity 安全管理 + 孤立检测
- 事务系统 + Undo/Redo
- 中英文双语本地化
- Sync 数据源准确性修复（Phase 28.1）

## License

MIT
