# Entity 状态事件使用指南

> 本文档介绍 StageEditor 中状态事件的使用方式和最佳实践
>
> 最后更新: 2025-01-20

---

## 概述

### Entity 级别事件

| 事件 | 位置 | 触发时机 | 适用场景 |
|-----|------|---------|---------|
| `OnEntityStateChanged` | Entity (组件) | 自身状态变化 | Entity 自身响应（动画、特效） |

### Stage 级别事件

Stage 提供 4 个可以直接在蓝图中重写的事件（`BlueprintImplementableEvent`，无需手动绑定委托）：

| 事件 | 参数 | 触发时机 | 适用场景 |
|-----|------|---------|---------|
| `OnStageEntityStateChanged` | EntityID, OldState, NewState | `SetEntityStateByID()` 调用后 | 响应 Entity 状态变化、跨 Entity 协调 |
| `OnActActivated` | ActID | `ActivateAct()` 调用后 | 响应 Act 激活，执行场景切换逻辑 |
| `OnActDeactivated` | ActID | `DeactivateAct()` 调用后 | 响应 Act 停用，执行清理逻辑 |
| `OnActiveActsChanged` | 无 | Act 激活/停用后 | 响应活跃 Act 集合变化 |

---

## 事件触发链路

```
Stage->SetEntityStateByID(EntityID, NewState)
    │
    ├─► EntityComp->SetEntityState(NewState)
    │       │
    │       └─► OnEntityStateChanged.Broadcast(NewState, OldState)
    │                    ↓
    │           [Entity Blueprint 响应]
    │
    └─► OnStageEntityStateChanged.Broadcast(EntityID, OldState, NewState)
                     ↓
            [Stage Blueprint 响应]
```

**关键点：** 必须通过 `SetEntityStateByID()` 或 `SetEntityState()` 函数修改状态，直接修改属性值不会触发事件！

---

## 方式一：Entity 级别事件（分散式）

### 适用场景
- Entity 自身的视觉/行为响应
- 播放动画、音效、粒子特效
- 切换材质、启用/禁用碰撞
- 移动、旋转等 Transform 变化

### 事件签名
```
OnEntityStateChanged(int32 NewState, int32 OldState)
```

### Blueprint 绑定方式

**方法 A：通过 Details 面板（推荐）**
1. 在 Entity Blueprint 的 Components 面板选中 `StageEntityComponent`
2. 在 Details 面板找到 `Events` 区域
3. 点击 `OnEntityStateChanged` 旁边的 **+** 按钮
4. 自动在 Event Graph 生成事件节点

**方法 B：手动绑定**
```
Event BeginPlay
    ↓
Get Component by Class (StageEntityComponent)
    ↓
Bind Event to OnEntityStateChanged
    ↓
Custom Event: HandleStateChanged(NewState, OldState)
```

### 典型用法
```
[OnEntityStateChanged]
    │
    ▼
[Switch on Int: NewState]
    ├── 0 ──► 默认状态逻辑
    ├── 1 ──► 状态1逻辑
    ├── 2 ──► 状态2逻辑
    └── Default ──► 未知状态处理
```

---

## 方式二：Stage 级别事件（集中式）

### 适用场景
- 跨 Entity 协调逻辑（如：所有开关激活后开门）
- 游戏条件判断（如：所有敌人死亡触发胜利）
- 全局统计、日志记录
- 需要知道"哪个 Entity"发生变化的场景
- Act 激活/停用时的场景切换逻辑

### 事件签名

**Entity 状态变化事件:**
```
OnStageEntityStateChanged(int32 EntityID, int32 OldState, int32 NewState)
```

**Act 事件:**
```
OnActActivated(int32 ActID)
OnActDeactivated(int32 ActID)
OnActiveActsChanged()
```

### Blueprint 绑定方式

在 Stage Blueprint 的 Event Graph 中：
1. 右键搜索事件名称（如 `On Stage Entity State Changed`）
2. 选择对应的红色事件节点
3. 该节点会在相应操作时自动触发

> **注意：** 所有 Stage 事件都是 `BlueprintImplementableEvent`，直接覆写即可使用，无需手动绑定委托。

### 典型用法 - Entity 状态变化
```
[Event On Stage Entity State Changed]
    │ EntityID, OldState, NewState
    ▼
[Branch: EntityID == 目标ID]
    │
    ├── True ──► 处理特定 Entity 的变化
    │
    └── False ──► 忽略或处理其他 Entity
```

### 典型用法 - Act 事件
```
[Event On Act Activated]
    │ ActID
    ▼
[Switch on Int: ActID]
    ├── 1 ──► 播放 Act 1 入场动画/音乐
    ├── 2 ──► 播放 Act 2 入场动画/音乐
    └── Default ──► 通用处理
```

### 重要：按需获取 Entity 引用

由于 World Partition 流式加载机制，**不要在 BeginPlay 中缓存 Entity 引用**！

❌ **错误做法:**
```
BeginPlay → GetEntityActorByID → 缓存到变量 (可能返回 None)
```

✅ **正确做法:**
```
Event OnStageEntityStateChanged
    ↓
GetEntityActorByID(EntityID)  ← 按需获取
    ↓
IsValid 检查
    ↓
执行操作
```

详细示例参见: [StageEventElevator_Tutorial.md](StageEventElevator_Tutorial.md)

---

## 案例一：升降电梯（Entity 级别事件）

### 功能需求
- 状态 0：电梯在底部
- 状态 1：电梯上升到顶部
- 状态 2：电梯下降到底部

### 准备工作

#### 1. 创建电梯 Actor Blueprint

1. Content Browser → 右键 → Blueprint Class → Actor
2. 命名为 `BP_Elevator`
3. 添加组件：
   - `Static Mesh Component` (电梯平台模型)
   - `Box Collision` (玩家站立检测，可选)
   - `StageEntityComponent` (状态管理)

#### 2. 创建 Timeline

1. 在 BP_Elevator 的 Event Graph 中右键 → Add Timeline
2. 命名为 `ElevatorTimeline`
3. 双击打开 Timeline 编辑器
4. 添加 Float Track，命名为 `Alpha`
5. 设置关键帧：
   - Time: 0, Value: 0
   - Time: 2, Value: 1 (2秒上升时间)
6. 勾选 `Use Last Keyframe` 确保到达终点

#### 3. 定义位置变量

在 BP_Elevator 中添加变量：
- `BottomLocation` (Vector) - 电梯底部位置
- `TopLocation` (Vector) - 电梯顶部位置
- `bIsAtTop` (Boolean) - 当前是否在顶部

### Blueprint 实现

```
┌─────────────────────────────────────────────────────────────────┐
│  BP_Elevator - Event Graph                                      │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────────┐                                           │
│  │ Event BeginPlay  │                                           │
│  └────────┬─────────┘                                           │
│           ▼                                                     │
│  ┌────────────────────────────┐                                 │
│  │ Set BottomLocation =       │                                 │
│  │   GetActorLocation()       │  ← 记录初始位置为底部           │
│  └────────┬───────────────────┘                                 │
│           ▼                                                     │
│  ┌────────────────────────────┐                                 │
│  │ Set TopLocation =          │                                 │
│  │   BottomLocation + (0,0,500)│  ← 顶部 = 底部 + 500高度       │
│  └────────────────────────────┘                                 │
│                                                                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌───────────────────────────────┐                              │
│  │ OnEntityStateChanged          │  ← 从 StageEntityComponent   │
│  │   NewState, OldState          │    的 Details 面板添加       │
│  └────────┬──────────────────────┘                              │
│           ▼                                                     │
│  ┌────────────────────────────┐                                 │
│  │ Switch on Int (NewState)   │                                 │
│  └────────┬───────────────────┘                                 │
│           │                                                     │
│     ┌─────┼─────┬─────┐                                         │
│     ▼     ▼     ▼     ▼                                         │
│    [0]   [1]   [2]  [Default]                                   │
│     │     │     │                                               │
│     │     ▼     ▼                                               │
│     │  ┌──────────────┐  ┌──────────────┐                       │
│     │  │ GoToTop()    │  │ GoToBottom() │                       │
│     │  └──────────────┘  └──────────────┘                       │
│     │                                                           │
│     ▼                                                           │
│  ┌────────────────────────────┐                                 │
│  │ Reset to Bottom            │  ← 状态0：重置到底部            │
│  │ SetActorLocation(Bottom)   │                                 │
│  └────────────────────────────┘                                 │
│                                                                 │
├─────────────────────────────────────────────────────────────────┤
│  GoToTop 函数:                                                  │
│  ┌────────────────────────────┐                                 │
│  │ ElevatorTimeline           │                                 │
│  │   Play from Start          │                                 │
│  └────────┬───────────────────┘                                 │
│           ▼                                                     │
│  ┌────────────────────────────────────────┐                     │
│  │ Update (Alpha)                          │                    │
│  │   SetActorLocation(                     │                    │
│  │     Lerp(BottomLocation, TopLocation,   │                    │
│  │          Alpha))                        │                    │
│  └────────────────────────────────────────┘                     │
│                                                                 │
├─────────────────────────────────────────────────────────────────┤
│  GoToBottom 函数:                                               │
│  ┌────────────────────────────┐                                 │
│  │ ElevatorTimeline           │                                 │
│  │   Reverse from End         │                                 │
│  └────────────────────────────┘                                 │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 测试步骤

#### 步骤 1：场景搭建
1. 打开测试关卡
2. 将 `BP_Elevator` 拖入场景
3. 确保已有 Stage Actor 和 StageRegistryAsset

#### 步骤 2：注册电梯到 Stage
1. 打开 Stage Editor Panel
2. 选中 `BP_Elevator`
3. 点击 "Register Selected Entities"
4. 确认电梯出现在 Stage 的 Entity 列表中
5. 记住电梯的 EntityID（假设为 1）

#### 步骤 3：配置 Act 状态
1. 在 Stage Editor 中创建 Act 1（电梯上升）
2. 展开 Act 1，设置电梯的 EntityState = 1
3. 创建 Act 2（电梯下降）
4. 展开 Act 2，设置电梯的 EntityState = 2

#### 步骤 4：运行测试
1. 点击 Play 进入 PIE 模式
2. 在 Stage Blueprint 中调用：
   ```
   SetEntityStateByID(1, 1)  → 电梯应该上升
   SetEntityStateByID(1, 2)  → 电梯应该下降
   SetEntityStateByID(1, 0)  → 电梯应该重置到底部
   ```
3. 或者通过激活 Act：
   ```
   ActivateAct(1)  → 电梯上升
   ActivateAct(2)  → 电梯下降
   ```

#### 验证清单
- [ ] 状态 1：电梯平滑上升到顶部
- [ ] 状态 2：电梯平滑下降到底部
- [ ] 状态 0：电梯瞬间重置到底部
- [ ] Timeline 动画流畅，无卡顿
- [ ] 多次切换状态正常工作

---

## 案例二：谜题房间 - 三灯开门（Stage 级别事件）

### 功能需求
- 房间有 3 盏灯和 1 扇门
- 当 3 盏灯都点亮（状态=1）时，门自动打开
- 任意一盏灯熄灭，门关闭

### 状态定义
| Entity | 状态 0 | 状态 1 |
|--------|--------|--------|
| Lamp_1/2/3 | 熄灭 | 点亮 |
| Door | 关闭 | 打开 |

### 准备工作

#### 1. 创建灯 Blueprint (BP_Lamp)
1. 创建 Actor Blueprint，命名 `BP_Lamp`
2. 添加组件：
   - `Point Light Component`
   - `Static Mesh Component` (灯模型)
   - `StageEntityComponent`
3. 实现 OnEntityStateChanged：
   ```
   State 0 → PointLight.SetVisibility(false)
   State 1 → PointLight.SetVisibility(true)
   ```

#### 2. 创建门 Blueprint (BP_PuzzleDoor)
1. 创建 Actor Blueprint，命名 `BP_PuzzleDoor`
2. 添加组件：
   - `Static Mesh Component` (门模型)
   - `StageEntityComponent`
3. 实现 OnEntityStateChanged：
   ```
   State 0 → PlayAnimation("Close")
   State 1 → PlayAnimation("Open")
   ```

### Stage Blueprint 实现

```
┌─────────────────────────────────────────────────────────────────┐
│  BP_PuzzleStage - Event Graph                                   │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  变量定义:                                                      │
│  - Lamp1_EntityID (int32) = 1                                   │
│  - Lamp2_EntityID (int32) = 2                                   │
│  - Lamp3_EntityID (int32) = 3                                   │
│  - Door_EntityID (int32) = 4                                    │
│                                                                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────────────────────────────┐                        │
│  │ OnStageEntityStateChanged_Event     │                        │
│  │   EntityID, OldState, NewState      │                        │
│  └────────┬────────────────────────────┘                        │
│           ▼                                                     │
│  ┌────────────────────────────────────────────┐                 │
│  │ Branch: IsLampEntity(EntityID)             │                 │
│  │   (EntityID == Lamp1 OR Lamp2 OR Lamp3)    │                 │
│  └────────┬───────────────────────────────────┘                 │
│           │                                                     │
│     ┌─────┴─────┐                                               │
│     ▼           ▼                                               │
│   [True]     [False]                                            │
│     │           │                                               │
│     ▼           ▼                                               │
│  ┌──────────────────┐  (忽略非灯 Entity)                        │
│  │ CheckAllLamps()  │                                           │
│  └────────┬─────────┘                                           │
│           ▼                                                     │
│  ┌────────────────────────────────────────────┐                 │
│  │ Get Lamp1 State (GetEntityStateByID)       │                 │
│  │ Get Lamp2 State                            │                 │
│  │ Get Lamp3 State                            │                 │
│  └────────┬───────────────────────────────────┘                 │
│           ▼                                                     │
│  ┌────────────────────────────────────────────┐                 │
│  │ Branch: All States == 1 ?                  │                 │
│  │   (Lamp1==1 AND Lamp2==1 AND Lamp3==1)     │                 │
│  └────────┬───────────────────────────────────┘                 │
│           │                                                     │
│     ┌─────┴─────┐                                               │
│     ▼           ▼                                               │
│   [True]     [False]                                            │
│     │           │                                               │
│     ▼           ▼                                               │
│  ┌─────────────────┐  ┌─────────────────┐                       │
│  │ OpenDoor()      │  │ CloseDoor()     │                       │
│  │ SetEntityState  │  │ SetEntityState  │                       │
│  │ ByID(Door, 1)   │  │ ByID(Door, 0)   │                       │
│  └─────────────────┘  └─────────────────┘                       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 测试步骤

#### 步骤 1：场景搭建
1. 创建新关卡或使用测试关卡
2. 放置 3 个 `BP_Lamp`（命名 Lamp_1, Lamp_2, Lamp_3）
3. 放置 1 个 `BP_PuzzleDoor`
4. 放置 1 个继承自 Stage 的 `BP_PuzzleStage`

#### 步骤 2：注册所有 Entity
1. 打开 Stage Editor Panel
2. 选中所有灯和门
3. 点击 "Register Selected Entities"
4. 记录每个 Entity 的 ID：
   - Lamp_1: ID = 1
   - Lamp_2: ID = 2
   - Lamp_3: ID = 3
   - Door: ID = 4
5. 在 BP_PuzzleStage 中设置对应的 EntityID 变量

#### 步骤 3：创建测试交互（可选）
为方便测试，可以添加按键触发：
```
Event Keyboard [1] → SetEntityStateByID(Lamp1, Toggle 0/1)
Event Keyboard [2] → SetEntityStateByID(Lamp2, Toggle 0/1)
Event Keyboard [3] → SetEntityStateByID(Lamp3, Toggle 0/1)
```

#### 步骤 4：运行测试
1. Play 进入 PIE 模式
2. 测试序列：
   ```
   按 [1] → 灯1亮，门不开
   按 [2] → 灯1+2亮，门不开
   按 [3] → 灯1+2+3亮，门打开！
   按 [2] → 灯2灭，门关闭
   按 [2] → 灯2亮，门再次打开
   ```

#### 验证清单
- [ ] 单独点亮任意灯，门不开
- [ ] 点亮任意两盏灯，门不开
- [ ] 三盏灯全亮，门自动打开
- [ ] 熄灭任意一盏灯，门立即关闭
- [ ] 重新全部点亮，门再次打开
- [ ] Stage 事件正确接收所有灯的状态变化

---

## 最佳实践总结

### 选择事件类型的决策树

```
需要响应状态变化
    │
    ├─► 只关心自己的状态？
    │       │
    │       └─► YES → 使用 OnEntityStateChanged (Entity级)
    │
    └─► 需要知道"哪个Entity"变化？
        或需要协调多个Entity？
            │
            └─► YES → 使用 OnStageEntityStateChanged (Stage级)
```

### 性能考虑

| 方式 | 事件触发频率 | 推荐场景 |
|-----|-------------|---------|
| Entity 级事件 | 仅自身变化时 | 大量 Entity 各自独立响应 |
| Stage 级事件 | 任意 Entity 变化时 | 少量关键 Entity 需要协调 |

### 常见错误

1. **直接修改属性值** - 不会触发事件
   ```
   ❌ EntityState = 1  (直接赋值)
   ✅ SetEntityState(1) (通过函数)
   ```

2. **忘记注册 Entity** - 无法通过 ID 查找
   ```
   确保 Entity 已通过 Stage Editor 注册
   确保 SUID.EntityID > 0
   ```

3. **Stage 事件中忘记过滤 EntityID** - 处理了不相关的 Entity
   ```
   始终检查 EntityID 是否是你关心的那个
   ```

---

## 相关文档

- [StageEventElevator_Tutorial.md](StageEventElevator_Tutorial.md) - Stage 级别事件升降电梯案例（推荐）
- [StageEditor Overview](../Overview.md)
- [HighLevelDesign](../HighLevelDesign.md)
- [StageEditorController](../StageEditorController.md)

---

*最后更新: 2025-01-20 - 添加 Stage 事件完整列表、按需获取模式*
