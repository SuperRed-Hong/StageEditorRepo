# StageEditor - 开发进度总览

> 文档创建日期: 2025-11-29（项目中途开始记录）
> 当前状态: ✅ Phase 1-27 全部完成 | ✅ Phase 28 Sync Bug + Registry 生命周期 + 性能修复 + 序列化修复完成 | 📋 UStageDataLayerAsset 设计中
> 最后更新: 2026-05-03
>
> 📂 **文档导航**: [Docs README](README.md) - 本文档索引
> 📚 **完整归档**: 详细子文档在主项目 `Docs/StageEditor/` 中（约128个文件），本仓库仅包含核心开发计划（本文档）+ 教程 + API 参考
>
> ⚠️ 本文档中指向 DataLayerIntegration/ CoreArchitecture/ EditorFeatures/ 等的链接需要主项目完整文档目录，GitHub 上不可用。

---

## 📝 文档说明

**本文档记录范围:**
- 本开发文档从 **Phase 1（DataLayer 反向同步功能设计）** 开始记录
- StageEditor 插件的 **Stage-Act-Entity 核心架构** 在此之前已完成基础实现
- 文档创建时（2025-11-29），核心架构已具备：
  - ✅ `AStage` Actor（舞台管理器）
  - ✅ `UStageEntityComponent` 组件（Entity 管理）
  - ✅ `FAct` 结构体（场景状态配置）
  - ✅ `StageEditorPanel`（基于 STreeView 的编辑器 UI）
  - ✅ `StageEditorController`（MVC 模式的控制器）
  - ✅ ID 分配系统、Entity 注册机制、状态管理

**文档化起点:**
- Phase 1 开始设计 **DataLayer 集成功能**（反向查找、Import、Sync）
- 此后所有功能开发均有完整设计文档和实施记录

**未文档化部分:**
- Stage-Act-Entity 核心架构的最初设计决策（已在代码中实现）
- StageEditorPanel 的初版 UI 实现过程

---

## 🎭 架构命名

**Stage-Act-Entity** (原 Stage-Act-Prop，于 Phase 14.5 重命名)

| 概念 | 说明 |
|------|------|
| **Stage** | 舞台 - 场景管理的根单位 |
| **Act** | 幕 - 场景状态配置 |
| **Entity** | 实体 - 任何被 Stage 管理的游戏对象（怪物、NPC、道具、载具等） |

---

## 📊 当前状态

### ✅ 已完成核心功能（Phase 1-15.5）

| 功能模块 | 状态 | 说明 |
|---------|------|------|
| **Stage-Act-Entity 核心架构** | ✅ | MVC 模式、ID 系统、状态管理 |
| **DataLayer 集成** | ✅ | Import、Sync、命名规范、自动创建 |
| **DataLayerBrowser (SceneOutliner)** | ✅ | 自定义列、过滤器、重命名、Sync 状态显示 |
| **StageEditorPanel** | ✅ | TreeView、拖拽注册、选择同步、上下文菜单 |
| **Blueprint 支持** | ✅ | Stage/Entity/TriggerZone 蓝图继承 |
| **Entity 安全管理** | ✅ | 孤立检测、单 Stage 约束、显式删除确认 |
| **事务系统** | ✅ | 完整 Undo/Redo 支持 |
| **本地化** | ✅ | 中英文双语支持 |

**详细进度归档:** [HistoricalProgress.md](DataLayerIntegration/HistoricalProgress.md)

---

## 🎯 Phase 进度

> 详细内容已归档到各自子文档，此处仅保留摘要

#### ✅ Phase 28 - Sync Bug 修复 + Registry 生命周期完善 + UStageDataLayerAsset 设计
> **状态:** ✅ Sync Bug + Registry 修复完成 | 📋 UStageDataLayerAsset 设计中
> **设计文档:** [StageDataLayerAsset_Design.md](CoreArchitecture/StageDataLayerAsset_Design.md)
> **最后一次更新:** 2026-05-03

**28.1 ✅ Sync 数据源 Bug 修复（已完成）:**

**根因:** `FindActIDByDataLayer` 返回 `Act.SUID.ActID`（逻辑 ID），但 `DetectActLevelChanges` / `SyncActLevelChanges` 将其作为 `Stage->Acts[]` 数组索引用，导致：
- 逻辑 ID 与数组索引错位（`SUID.ActID = ArrayIndex + 1`）
- 跨 Act 数据污染（比较源用了 Stage 全局的 `EntityRegistry` 而非 Act 的 `EntityStateOverrides`）

**修法（3 个文件）:**
- `DataLayerSyncStatus.cpp` — `DetectActLevelChanges`: 通过 SUID.ActID 匹配查找 Act；比较源切换到 `Act->EntityStateOverrides`；Actor 名改用 `GetActorNameOrLabel()`
- `DataLayerSynchronizer.cpp` — `SyncActLevelChanges`: 同上匹配逻辑；移除 Entity 时不强制 `UnregisterEntity`（其他 Act 可能引用）
- `StageDataLayerColumns.cpp` — OutOfSync Tooltip 改用 `GenerateTooltip()` 显示完整数据源对比
- 新增 `FDataLayerSyncStatusInfo` 字段: `AddedActorNames`, `RemovedActorNames`, `CurrentDataLayerActorNames`, `ActEntityActorNames`

**28.2 📋 UStageDataLayerAsset 子类（设计中）:**

**核心目标:** 继承 `UDataLayerAsset` 创建 StageEditor 专用子类，携带 `FSUID` + 关卡路径元数据，通过 Asset Registry Tags 在 Content Browser 中显示归属信息。

**设计决策:**
- 模块: StageEditorRuntime（与 AStage/FAct 共享 `FSUID` 类型）
- 方法签名保持 `UDataLayerAsset*`（IS-A 多态，~150 行无需改）
- 存储字段升级为 `TObjectPtr<UStageDataLayerAsset>`
- 旧 `UDataLayerAsset` 资产共存不冲突

**待实施:** 新建类 → 修改创建点 → 修改存储字段 → 填充元数据

**28.3 ✅ Registry 生命周期 + UI 完善（已完成，2026-05-03）:**

**修复内容:**
- ✅ 删除 Stage Actor 时自动清理 Registry 残留条目（`OnLevelActorDeleted` hook）
- ✅ Sync Warning 横幅 Solo 模式可见（移除 `Multi` 模式限定条件）
- ✅ Sync Registry 按钮点击后自动刷新 UI（移除 reconcile 的早返回）
- ✅ Create Registry 按钮完整流程：进度条（4 步）→ 自动 Save → 专用 P4 Changelist → 描述附创建摘要 → 弹出 Changelist 面板
- ✅ `CheckOutToChangelist` 中 `FMoveToChangelist` API 修正（Changelist 作为 Execute 第二参数）

**修改文件:** `StageEditorController.cpp`, `StageEditorStateManager.cpp`, `StageEditorPanel.cpp`, `StageEditorSubsystem.cpp/.h`, `StageEditorActionHandlers.cpp`

**28.4 ✅ 两个 Bug 修复 — Multi 模式性能 + EntityState 序列化（已完成，2026-05-03）:**

**Bug 1 — Multi 模式卡顿：** `GetSyncButtonText/Tooltip` 两个 Slate 绑定每帧调用 `CalculateSyncStatus()`（绕过 Phase 18 缓存），触发同步 P4 ForceUpdate。修法：按钮文本预计算到 StateManager 缓存，绑定改为 1 行读缓存。

**Bug 2 — EntityState 残留非零值：** `EntityState` 缺少 `Transient`，Preview Act 修改被持久化到 .umap，编辑器加载时无 BeginPlay 重置。修法：加 `Transient` 标记。

> **详细文档:** [Phase28.4_TwoBugFixes.md](CoreArchitecture/Phase28.4_TwoBugFixes.md)
> **修改文件:** `StageEditorStateManager.h/.cpp`, `StageEditorPanel.cpp`, `StageEntityComponent.h`

#### ✅ Phase 27 - DeactivateStage / Active→Loaded 转换补全（2026-04-25）
> **文档:** [Phase27_DeactivateStage.md](CoreArchitecture/Phase27_DeactivateStage.md)

补全 `GotoState(Loaded)` 在 `Active` 状态下的分支，新增 `DeactivateStage()` BP 便捷节点。
注意：默认模式下玩家仍在 ActivateZone 时会被自动回弹回 Active（tooltip 已标注缓解方案）。

#### ✅ Phase 26 - PresetAction 权威化（Opt-In 开关）（2026-04-25）
> **文档:** [Phase26_PresetAction_AuthorityRefactor.md](EditorFeatures/Phase26_PresetAction_AuthorityRefactor.md)

新增 `bUseCustomActions` 开关：默认 false 走硬编码路径（与原行为一致），true 时由 PresetAction 接管。
配套：`OnEnterAction`/`OnExitAction` 加 `EditCondition` 灰显；基类 BP 委托补充观察者契约 tooltip。

#### ✅ Phase 25 - Details 面板统一重组 + BuiltInTriggerZoneComponent（2026-04-03）
> **文档:** [Phase25_DetailsPanel_Reorganization.md](EditorFeatures/Phase25_DetailsPanel_Reorganization.md)

所有分类归入 `StageEditor`；新建 `UBuiltInTriggerZoneComponent` 通过 HideCategories 隐藏冗余分类。

#### ✅ Phase 24 - TriggerZone bComponentVisible 可见性控制修复（2026-04-03）
> **文档:** [Phase24_TriggerZone_VisibilityFix.md](EditorFeatures/Phase24_TriggerZone_VisibilityFix.md)

`OnConstruction` → `UpdateBuiltInZoneVisibility()` 忽略 `bComponentVisible`，改为两级开关 AND。

#### ✅ Phase 23 - 编辑器复制操作数据一致性架构修复（2026-04-03）
> **文档:** [Phase23_PostEditImport_DataConsistency.md](EditorFeatures/Phase23_PostEditImport_DataConsistency.md)

4 个类添加 `PostEditImport()` 纵深防御 + `RegisterEntity()` 补全 `Modify()`。
技术选型：用 `PostEditImport` 而非 `PostDuplicate`，因后者在 PIE 复制世界时也会触发。

#### ✅ Phase 22 - Unregister Entity 数据残留修复（2026-04-01）

取消注册时组件字段未清理。修法：新增 `ClearRegistrationState()`，重构 `ClearOrphanedState()` 复用。

#### ✅ Phase 21 - PIE 结束后面板数据错乱修复（2026-03-17）
> **文档:** [Phase21_PIEEndRefreshFix.md](CoreArchitecture/Phase21_PIEEndRefreshFix.md)

`EndPIE` delegate 缺失致 `TWeakObjectPtr` 失效。修法：注册 `FEditorDelegates::EndPIE` 触发刷新。

#### ✅ Phase 20 - SUID 排序 + Entity State WP 时序修复（2026-03-17）
> **文档:** [Phase20_SortingAndBeginPlayFix.md](CoreArchitecture/Phase20_SortingAndBeginPlayFix.md)

- 20.1 三层 SUID 升序排序 / 20.2 WP 时序修复（BeginPlay Pull）
- 20.3 SUID 命名 P_→E_ / 20.4 Entity Row Browse+EditBP 按钮

#### ✅ Phase 19 - 蓝图事件 + Registry 选择 + 配置系统（2025-01-06 ~ 2025-01-20）
> **文档:** [HistoricalProgress.md](DataLayerIntegration/HistoricalProgress.md#phase-19) / [教程](Tutorials/StageEventElevator_Tutorial.md) / [事件 API](Tutorials/EntityStateEvent_Guide.md)

- 19.1-19.7: Select Existing Registry + Personal/Project 配置系统（JSON Import/Export）
- 19.8: 移除 `SetEntityState` 中危险的 `RerunConstructionScripts`
- 19.9: Stage 蓝图事件（OnEntityStateChanged / OnActActivated 等 4 个 BlueprintImplementableEvent）

#### ✅ Phase 18 - WP 性能优化（2025-12-18）
> **文档:** [Phase18_Implementation_Design.md](CoreArchitecture/Phase18_Implementation_Design.md)

SC 查询缓存层（5秒 TTL），消除 Level 加载卡顿。

#### ✅ Phase 17 - SceneOutliner 迁移（2025-12-中）
> **文档:** [Phase17_SceneOutliner_Migration_Feasibility.md](CoreArchitecture/Phase17_SceneOutliner_Migration_Feasibility.md)

StageEditorPanel 从 STreeView 迁移到 SceneOutliner 框架（V2 Panel）。待实施：Phase 17.3 孤立 Entity 过滤器。

#### ✅ Phase 16 - 代码重构（2025-12-08）
> **文档:** [Refactoring_Phase6_Progress.md](CodeRefactoring/Refactoring_Phase6_Progress.md)

Panel 模块化：3720 → 2800 lines，拆分 4 个 Helper 模块。

#### ✅ Phase 13 - StageRegistry 持久化（2025-12-10）
> **文档:** [Phase13_ImplementationProgress.md](CoreArchitecture/Phase13_ImplementationProgress.md)

Registry 持久化 + 双 Subsystem 架构 + 元数据缓存 + ID 回收。提炼 [FEDS 架构原则](../CLAUDE.md#architecture-rules-for-complex-systems)。

---

## 📂 Phase 索引（快速导航）

| Phase | 任务 | 状态 | 详细文档 |
|-------|------|------|---------|
| 1-2 | 反向查找与状态检测 + 性能优化 | ✅ | [Phase1-2_ReverseLookup.md](DataLayerIntegration/Phase1-2_ReverseLookup.md) |
| 3 | 命名解析模块 | ✅ | [Phase3_Parser.md](DataLayerIntegration/Phase3_Parser.md) |
| 4 | DataLayerOutliner UI | ✅ | [Phase4_UI.md](DataLayerIntegration/Phase4_UI.md) |
| 5 | 导入逻辑与预览对话框 | ✅ | [Phase5_Import.md](DataLayerIntegration/Phase5_Import.md) |
| 6 | 同步逻辑 | ✅ | [Phase6_Sync.md](DataLayerIntegration/Phase6_Sync.md) |
| 7 | 本地化（中英文） | ✅ | [Phase7_Localization.md](DataLayerIntegration/Phase7_Localization.md) |
| 8-8.4 | SceneOutliner 架构 + 原生功能迁移 | ✅ | [Phase8_UI_Extension_Research.md](DataLayerIntegration/Phase8_UI_Extension_Research.md) |
| 9-9.5 | 架构整合 + 代码质量优化 | ✅ | [Architecture_Integration_Analysis.md](DataLayerIntegration/Architecture_Integration_Analysis.md) |
| 10 | Import 功能重设计 | ✅ | [Phase10_ImportRedesign.md](DataLayerIntegration/Phase10_ImportRedesign.md) |
| 11 | 缓存事件驱动优化 | ✅ | [Phase11_CacheEventDriven.md](DataLayerIntegration/Phase11_CacheEventDriven.md) |
| 12 | Import/Rename 功能增强 | ✅ | [历史进度](DataLayerIntegration/HistoricalProgress.md#phase-12) |
| **13** | **StageRegistry 持久化架构** | ✅ | [Phase13_ImplementationProgress.md](CoreArchitecture/Phase13_ImplementationProgress.md) |
| **13.5** | **Multi-User Registry Sync** | ✅ | [Phase13.5_MultiUser_RegistrySync_Design.md](CoreArchitecture/Phase13.5_MultiUser_RegistrySync_Design.md) |
| **13.6** | **双 Subsystem 架构拆分** | ✅ | [Phase13.6_DualSubsystem_Implementation.md](CoreArchitecture/Phase13.6_DualSubsystem_Implementation.md) |
| **13.8** | **元数据缓存架构** | ✅ | [设计](CoreArchitecture/Phase13.8_DataConsistency_CriticalFix.md) / [实施](CoreArchitecture/Phase13.8_Implementation_Log.md) |
| 14 | Import 蓝图类支持 + TriggerZone | ✅ | [ImportBlueprintClassSupport.md](DataLayerIntegration/ImportBlueprintClassSupport.md) |
| 14.5 | Prop → Entity 架构重命名 | ✅ | [PropToEntity_RenamingPlan.md](Refactoring/PropToEntity_RenamingPlan.md) |
| 15 | Entity 管理安全性增强 | ✅ | [Phase15_EntityManagement_SafetyEnhancements.md](EditorFeatures/Phase15_EntityManagement_SafetyEnhancements.md) |
| 15.5 | DataLayerOutliner 列宽修复 | ✅ | [SSplitter_ResizeMode_Explained.md](TechNotes/SSplitter_ResizeMode_Explained.md) |
| **16** | **StageEditorPanel 代码重构** | ✅ 全部完成 | [Phase 1-5 完成](CodeRefactoring/Refactoring_Phase1_Complete_Summary.md) / [Phase 6 完成](CodeRefactoring/Refactoring_Phase6_Progress.md) |
| **17** | **StageEditorPanel SceneOutliner 迁移** | 🔄 实施中 | [迁移设计](EditorFeatures/Phase16_StageEditorPanel_SceneOutliner_Migration.md) / [可行性评估](EditorFeatures/Phase17_SceneOutliner_Migration_Feasibility.md) |
| **18** | **✅ WP 性能问题修复（P0）** | ✅ 全部完成 | [设计](CoreArchitecture/Phase18_Implementation_Design.md) / [Phase18.1 SC优化](CoreArchitecture/Phase18.1_SC_Query_Optimization.md) |
| **19.1-19.7** | **Select Existing Registry + 工业级配置系统** | ✅ 全部完成 | [历史进度](DataLayerIntegration/HistoricalProgress.md#phase-19) |
| **19.8** | **移除 SetEntityState RerunConstructionScripts** | ✅ 已完成 | [未来设计](FuturePlans/EditorInstantFeedback_Design.md) |
| **19.9** | **Stage BlueprintImplementableEvent 增强** | ✅ 已完成 | [教程](Tutorials/StageEventElevator_Tutorial.md) |
| **20.1** | **TreeBuilder SUID 升序排序（三层）** | ✅ 已完成 | [Phase20](CoreArchitecture/Phase20_SortingAndBeginPlayFix.md) |
| **20.2** | **Entity State WP 时序 Bug 修复（BeginPlay Pull）** | ✅ 已完成 | [Phase20](CoreArchitecture/Phase20_SortingAndBeginPlayFix.md) |
| **20.3** | **Entity SUID 命名修复（P_ → E_）** | ✅ 已完成 | [Phase20](CoreArchitecture/Phase20_SortingAndBeginPlayFix.md) |
| **20.4** | **Entity Row Browse + Edit BP 快捷按钮** | ✅ 已完成 | [Phase20](CoreArchitecture/Phase20_SortingAndBeginPlayFix.md) |
| **21** | **PIE 结束后面板数据错乱修复** | ✅ 已完成 | [Phase21](CoreArchitecture/Phase21_PIEEndRefreshFix.md) |
| **22** | **Unregister Entity 数据残留修复** | ✅ 已完成 | — |
| **23** | **编辑器复制操作数据一致性架构修复** | ✅ 已完成 | [Phase23](EditorFeatures/Phase23_PostEditImport_DataConsistency.md) |
| **24** | **TriggerZone bComponentVisible 可见性控制修复** | ✅ 已完成 | [Phase24](EditorFeatures/Phase24_TriggerZone_VisibilityFix.md) |
| **25** | **Details 面板统一重组 + BuiltInTriggerZoneComponent** | ✅ 已完成 | [Phase25](EditorFeatures/Phase25_DetailsPanel_Reorganization.md) |
| **26** | **PresetAction 权威化（Opt-In 开关 bUseCustomActions）** | ✅ 已完成 | [Phase26](EditorFeatures/Phase26_PresetAction_AuthorityRefactor.md) |
| **27** | **DeactivateStage / Active→Loaded 转换补全** | ✅ 已完成 | [Phase27](CoreArchitecture/Phase27_DeactivateStage.md) |
| **28** | **Sync Bug + Registry 修复 + UStageDataLayerAsset** | ✅ 修复完成 / 📋 Asset 设计中 | [Phase28](CoreArchitecture/StageDataLayerAsset_Design.md) |

---

## 🗂️ 关键文件索引

```
Plugins/StageEditor/Source/
├── StageEditorRuntime/
│   ├── Public/
│   │   ├── Subsystems/StageManagerSubsystem.h  # 反向查找 API + Stage 注册委托
│   │   ├── Actors/Stage.h                      # Stage Actor (PostLoad 自注册)
│   │   ├── Actors/StageEntity.h                # Entity 基类
│   │   └── Components/StageEntityComponent.h   # Entity 组件（孤立检测、单 Stage 约束）
│   └── Private/
│       ├── Subsystems/StageManagerSubsystem.cpp
│       ├── Actors/Stage.cpp
│       ├── Actors/StageEntity.cpp
│       └── Components/StageEntityComponent.cpp
│
├── StageEditor/
│   ├── Public/EditorLogic/
│   │   └── StageEditorController.h            # MVC Controller（所有编辑操作入口）
│   ├── Public/EditorUI/
│   │   └── StageEditorPanel.h                 # Stage 管理主面板（TreeView）
│   ├── Public/DataLayerSync/
│   │   ├── SStageDataLayerOutliner.h          # DataLayer 浏览器（SceneOutliner）
│   │   ├── StageDataLayerNameParser.h         # 命名解析
│   │   ├── DataLayerSyncStatus.h              # 状态枚举和检测器
│   │   ├── DataLayerSyncStatusCache.h         # 事件驱动缓存层
│   │   ├── DataLayerImporter.h                # 导入逻辑
│   │   ├── SDataLayerImportPreviewDialog.h    # 单个导入预览
│   │   ├── SBatchImportPreviewDialog.h        # 批量导入预览
│   │   ├── DataLayerSynchronizer.h            # 同步逻辑
│   │   ├── StageDataLayerMode.h               # SceneOutliner Mode
│   │   ├── StageDataLayerHierarchy.h          # SceneOutliner Hierarchy
│   │   ├── StageDataLayerTreeItem.h           # SceneOutliner TreeItem
│   │   ├── StageDataLayerColumns.h            # 自定义列
│   │   └── SStdRenamePopup.h                  # 标准重命名对话框
│   └── Private/StageEditorModule.cpp          # Tab 注册 + 缓存生命周期
```

---

## 🔑 架构决策文档

| 决策 | 文档 |
|------|------|
| **反向查找方案（采纳）** | [Architecture_ReverseLookup.md](DataLayerIntegration/Architecture_ReverseLookup.md) |
| **基于 SceneOutliner 框架（采纳）** | [Phase8_UI_Extension_Research.md](DataLayerIntegration/Phase8_UI_Extension_Research.md) |
| **架构整合方案** | [Architecture_Integration_Analysis.md](DataLayerIntegration/Architecture_Integration_Analysis.md) |
| **StageRegistry 持久化方案** | [Phase13_StageRegistry_Discussion.md](CoreArchitecture/Phase13_StageRegistry_Discussion.md) |
| **UStageDataLayerAsset 设计** | [StageDataLayerAsset_Design.md](CoreArchitecture/StageDataLayerAsset_Design.md) |
| 废弃方案存档 | [Obsolete/DataLayerIntegration/README.md](Obsolete/DataLayerIntegration/README.md) |

---

*最后更新: 2026-05-03 - Phase 28 Sync Bug + Registry 修复完成，UStageDataLayerAsset 设计中*
