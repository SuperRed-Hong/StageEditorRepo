# Stage 级别事件测试案例 - 升降电梯

> 本文档演示如何使用 `Event On Stage Entity State Changed` 实现 Stage 统一调度 Entity 的模式
>
> 创建日期: 2025-01-06
> 更新日期: 2025-01-20 - 改用按需获取模式，支持 World Partition 流式加载
>
> 前置阅读: [EntityStateEvent_Guide.md](EntityStateEvent_Guide.md)

---

## Stage 可用的 Blueprint 事件

Stage 提供了 4 个可以直接在蓝图中重写的事件（无需手动绑定委托）：

| 事件名称 | 参数 | 触发时机 | 典型用途 |
|---------|------|---------|---------|
| **On Stage Entity State Changed** | EntityID, OldState, NewState | `SetEntityStateByID()` 调用后 | 响应 Entity 状态变化 |
| **On Act Activated** | ActID | `ActivateAct()` 调用后 | 响应 Act 激活，执行场景切换逻辑 |
| **On Act Deactivated** | ActID | `DeactivateAct()` 调用后 | 响应 Act 停用，执行清理逻辑 |
| **On Active Acts Changed** | 无 | Act 激活/停用后 | 响应活跃 Act 集合变化 |

**使用方法:**
1. 在 Stage 子类蓝图的 Event Graph 中右键
2. 搜索事件名称（如 `On Stage Entity State Changed`）
3. 选择红色事件节点即可直接使用

---

## 概述

### 两种事件模式对比

| 模式 | 事件位置 | 调度方式 | 适用场景 |
|-----|---------|---------|---------|
| **Entity 级别** | Entity Blueprint | Entity 自己响应状态变化 | 简单独立的 Entity 行为 |
| **Stage 级别** | Stage Blueprint | Stage 统一调度所有 Entity | 复杂协调、跨 Entity 逻辑 |

### 本案例采用 Stage 级别模式

```
┌─────────────────────────────────────────────────────────────────┐
│  调用链路:                                                       │
│                                                                 │
│  用户按 E 键                                                     │
│       ↓                                                         │
│  Stage.SetEntityStateByID(ElevatorID, NewState)                 │
│       ↓                                                         │
│  Stage.Event On Stage Entity State Changed 触发                 │
│       ↓                                                         │
│  Stage 根据 EntityID 和 NewState 调用 Elevator 的移动函数         │
│       ↓                                                         │
│  Elevator 执行移动动画                                           │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 准备工作

### 场景需求

- 1 个 Stage Actor（BP_Stage_Elevator）
- 1 个 Elevator Actor（BP_Elevator）
- Elevator 注册到 Stage，EntityID = 1

### 状态定义

| 状态值 | 含义 | 行为 |
|-------|------|------|
| 0 | 重置 | 电梯瞬间回到底部 |
| 1 | 上升 | 电梯平滑上升到顶部 |
| 2 | 下降 | 电梯平滑下降到底部 |

---

## Step 1: 创建 BP_Elevator

### 1.1 创建蓝图

1. Content Browser → 右键 → Blueprint Class → Actor
2. 命名为 `BP_Elevator`
3. 添加组件：
   - `Static Mesh Component`（电梯平台模型）
   - `StageEntityComponent`（状态管理）

### 1.2 添加变量

| 变量名 | 类型 | 说明 |
|-------|------|------|
| BottomLocation | Vector | 电梯底部位置 |
| TopLocation | Vector | 电梯顶部位置 |

### 1.3 创建 Timeline

1. Event Graph 右键 → Add Timeline → 命名 `ElevatorTimeline`
2. 双击打开 Timeline 编辑器
3. 点击 `+ Track` → Add Float Track → 命名 `Alpha`
4. 添加关键帧：
   - Time: 0, Value: 0
   - Time: 2, Value: 1
5. 设置 Length: 2.00

### 1.4 BeginPlay - 初始化位置

```
┌─────────────────────────────────────────────────────────────────┐
│  Event BeginPlay                                                │
│       │                                                         │
│       ▼                                                         │
│  Set BottomLocation = GetActorLocation()                        │
│       │                                                         │
│       ▼                                                         │
│  Set TopLocation = BottomLocation + (0, 0, 500)                 │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 1.5 创建公开函数

**函数 1: MoveToTop**
```
┌─────────────────────────────────────────────────────────────────┐
│  MoveToTop (Function)                                           │
│       │                                                         │
│       ▼                                                         │
│  ElevatorTimeline → Play from Start                             │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**函数 2: MoveToBottom**
```
┌─────────────────────────────────────────────────────────────────┐
│  MoveToBottom (Function)                                        │
│       │                                                         │
│       ▼                                                         │
│  ElevatorTimeline → Reverse from End                            │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**函数 3: ResetToBottom**
```
┌─────────────────────────────────────────────────────────────────┐
│  ResetToBottom (Function)                                       │
│       │                                                         │
│       ▼                                                         │
│  SetActorLocation(BottomLocation)                               │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 1.6 Timeline Update 处理

```
┌─────────────────────────────────────────────────────────────────┐
│  ElevatorTimeline                                               │
│       │                                                         │
│       │── Update ──────────────────────────────┐                │
│       │                                        │                │
│       └── Alpha ────┐                          ▼                │
│                     │                   Print String            │
│  BottomLocation ──► A                   (调试用，可选)           │
│                     │                          │                │
│  TopLocation ────► B                           ▼                │
│                     │                   SetActorLocation        │
│           Lerp (Vector) ◄──────────────────────┘                │
│                     │                                           │
│                     └───────────────► New Location              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

> **重要:** Timeline 的 Update 执行引脚（白色）必须连接到后续节点！

---

## Step 2: 创建 BP_Stage_Elevator

### 2.1 创建蓝图

1. Content Browser → 右键 → Blueprint Class
2. 搜索并选择 `Stage` 作为父类
3. 命名为 `BP_Stage_Elevator`

### 2.2 添加变量

| 变量名 | 类型 | 默认值 | 说明 |
|-------|------|-------|------|
| Elevator_EntityID | Integer | 1 | 电梯的 Entity ID |

> **注意:** 不再需要缓存 `ElevatorRef` 变量！采用按需获取模式。

### 2.3 BeginPlay - 仅启用输入

```
┌─────────────────────────────────────────────────────────────────┐
│  Event BeginPlay                                                │
│       │                                                         │
│       ▼                                                         │
│  Get Player Controller (Index: 0)                               │
│       │                                                         │
│       ▼                                                         │
│  Enable Input (Target: Self)                                    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

> **为什么不在 BeginPlay 中获取 Entity 引用？**
>
> 由于 World Partition 流式加载机制，Entity Actor 可能在 Stage 的 BeginPlay
> 执行时尚未加载。在 BeginPlay 中获取引用会导致 "Accessed None" 错误。
>
> **解决方案:** 在事件触发时按需获取引用（见 Step 2.4）。

### 2.4 添加 Stage 级别事件（核心 - 按需获取模式）

1. 在 Event Graph 空白处右键
2. 搜索 `On Stage Entity State Changed`
3. 选择 `Event On Stage Entity State Changed`（红色事件节点）

**关键:** 在事件触发时按需获取 Entity 引用，而非提前缓存

```
┌─────────────────────────────────────────────────────────────────┐
│  Event On Stage Entity State Changed                            │
│       │ EntityID                                                │
│       │ OldState                                                │
│       │ NewState                                                │
│       ▼                                                         │
│  Print String ("Entity " + EntityID + " → State " + NewState)   │
│       │                                                         │
│       ▼                                                         │
│  Branch: EntityID == Elevator_EntityID                          │
│       │                                                         │
│  ┌────┴────┐                                                    │
│ True     False                                                  │
│  │         │                                                    │
│  ▼        (忽略其他 Entity)                                      │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │  【按需获取 Entity 引用】                                    ││
│  │                                                             ││
│  │  Get Entity Actor By ID (Elevator_EntityID)                 ││
│  │       │                                                     ││
│  │       ▼                                                     ││
│  │  Cast to BP_Elevator ───► As BP Elevator (local variable)   ││
│  │       │                                                     ││
│  │       ▼                                                     ││
│  │  Branch: IsValid(As BP Elevator)                            ││
│  │                                                             ││
│  └─────────────────────────────────────────────────────────────┘│
│       │                                                         │
│       ▼ (IsValid == True)                                       │
│  Switch on Int (NewState)                                       │
│       │                                                         │
│  ┌────┼────┬────┐                                               │
│  0    1    2   Default                                          │
│  │    │    │                                                    │
│  ▼    ▼    ▼                                                    │
│  (As BP Elevator).ResetToBottom()                               │
│       (As BP Elevator).MoveToTop()                              │
│            (As BP Elevator).MoveToBottom()                      │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

> **为什么使用按需获取？**
>
> 这种模式确保在事件触发时 Entity 已经被流式加载系统加载完成。
> 如果 Entity 尚未加载，`GetEntityActorByID` 会返回 None，
> 通过 `IsValid` 检查可以安全地跳过处理。

### 2.5 添加测试按键

**E 键 - 切换上升/下降**
```
┌─────────────────────────────────────────────────────────────────┐
│  Keyboard Event [E] - Pressed                                   │
│       │                                                         │
│       ▼                                                         │
│  Print String ("E Pressed")                                     │
│       │                                                         │
│       ▼                                                         │
│  Flip Flop                                                      │
│       │                                                         │
│  ┌────┴────┐                                                    │
│  A         B                                                    │
│  │         │                                                    │
│  ▼         ▼                                                    │
│  SetEntityStateByID          SetEntityStateByID                 │
│    EntityID: 1                 EntityID: 1                      │
│    NewState: 1 (上升)          NewState: 2 (下降)               │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**R 键 - 重置**
```
┌─────────────────────────────────────────────────────────────────┐
│  Keyboard Event [R] - Pressed                                   │
│       │                                                         │
│       ▼                                                         │
│  SetEntityStateByID                                             │
│    EntityID: 1                                                  │
│    NewState: 0 (重置)                                           │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Step 3: 场景搭建

### 3.1 放置 Actor

1. 将 `BP_Stage_Elevator` 拖入场景
2. 将 `BP_Elevator` 拖入场景（放在合适位置）

### 3.2 注册电梯到 Stage

1. 打开 Stage Editor Panel
2. 选中场景中的 `BP_Elevator`
3. 点击 "Register Selected Entities"
4. 确认电梯的 EntityID = 1（与代码中的变量匹配）

### 3.3 确保 Registry 关联

1. 确保当前关卡已有 `StageRegistryAsset`
2. 在 Stage Editor Panel 中确认 "Current Registry" 显示正确

---

## Step 4: 测试验证

### 4.1 运行测试

1. 点击 Play 进入 PIE 模式
2. 测试按键：

| 按键 | 预期行为 |
|-----|---------|
| E（第1次） | 电梯平滑上升到顶部 |
| E（第2次） | 电梯平滑下降到底部 |
| E（第3次） | 电梯再次上升 |
| R | 电梯瞬间重置到底部 |

### 4.2 验证清单

- [ ] 按 E 后，Output Log 打印 "E Pressed"
- [ ] 按 E 后，Output Log 打印 "Entity 1 → State 1" 或 "Entity 1 → State 2"
- [ ] 电梯平滑移动（Timeline 动画正常）
- [ ] 按 R 后电梯瞬间重置
- [ ] 多次按 E 可正常切换上升/下降

### 4.3 常见问题排查

| 问题 | 可能原因 | 解决方案 |
|-----|---------|---------|
| 按键无反应 | Stage 未启用输入 | 检查 BeginPlay 中的 Enable Input |
| 事件不触发 | 未使用 SetEntityStateByID | 必须通过函数修改状态，不能直接赋值 |
| Cast 返回 None | EntityID 不匹配或未注册 | 检查电梯是否已注册，ID 是否为 1 |
| Cast 返回 None | Entity 尚未流式加载 | 这是正常情况，IsValid 检查会安全跳过 |
| Timeline 只执行一帧 | Update 执行线未连接 | 检查 Timeline Update → SetActorLocation 的白色执行线 |
| 变量被重置为 (0,0,0) | ~~SetEntityState 触发重建~~ | 已修复，不再有此问题 |

> **关于流式加载:**
>
> 如果使用 World Partition，Entity Actor 可能在 Stage 初始化后才被流式加载。
> 按需获取模式可以优雅处理这种情况 - 如果 Entity 尚未加载，事件处理会安全跳过。
> 当 Entity 加载完成后，下次状态变化时会正常响应。

---

## 扩展：多 Entity 协调示例

Stage 级别事件的优势在于可以协调多个 Entity：

```
┌─────────────────────────────────────────────────────────────────┐
│  Event On Stage Entity State Changed                            │
│       │                                                         │
│       ▼                                                         │
│  Switch on EntityID                                             │
│       │                                                         │
│  ┌────┼────┬────┐                                               │
│  1    2    3   ...                                              │
│  │    │    │                                                    │
│  ▼    ▼    ▼                                                    │
│  处理电梯  处理门  处理灯                                         │
│                                                                 │
│  // 或者检查多个 Entity 状态后执行联动逻辑                         │
│  Branch: Lamp1.State==1 AND Lamp2.State==1 AND Lamp3.State==1   │
│       │                                                         │
│       └──► 打开大门                                              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 相关文档

- [EntityStateEvent_Guide.md](EntityStateEvent_Guide.md) - 事件系统完整指南
- [Overview.md](../Overview.md) - 项目总览

---

## 附录: Stage 事件 API 参考

### OnStageEntityStateChanged

```cpp
// C++ 委托
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnStageEntityStateChanged, int32, EntityID, int32, OldState, int32, NewState);

// Blueprint 可重写事件
UFUNCTION(BlueprintImplementableEvent, Category = "Stage|Events")
void ReceiveOnStageEntityStateChanged(int32 EntityID, int32 OldState, int32 NewState);
```

### OnActActivated / OnActDeactivated

```cpp
// C++ 委托
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActActivated, int32, ActID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActDeactivated, int32, ActID);

// Blueprint 可重写事件
UFUNCTION(BlueprintImplementableEvent, Category = "Stage|Events")
void ReceiveOnActActivated(int32 ActID);

UFUNCTION(BlueprintImplementableEvent, Category = "Stage|Events")
void ReceiveOnActDeactivated(int32 ActID);
```

### OnActiveActsChanged

```cpp
// C++ 委托
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnActiveActsChanged);

// Blueprint 可重写事件
UFUNCTION(BlueprintImplementableEvent, Category = "Stage|Events")
void ReceiveOnActiveActsChanged();
```

---

*创建日期: 2025-01-06 - Stage 级别事件测试案例*
*更新日期: 2025-01-20 - 添加按需获取模式、完整事件 API 参考*
