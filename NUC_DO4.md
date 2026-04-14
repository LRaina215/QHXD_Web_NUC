# NUC_DO4.md

## 1. 本轮是否需要 NUC 执行任务

结论：**需要，但不需要做实机联调开发。**

更准确地说：

- **Round 1 不要求 NUC 新增代码**
- **Round 1 不要求 NUC 做网口联调验收**
- **Round 1 需要 NUC 配合做一次“字段来源与映射确认”**

因为这一轮的目标不是打通新链路，而是冻结 Phase 3 的三节点状态映射规则，避免后续：

- RK3588 以为某字段来自 NUC
- NUC 以为某字段来自 RT-Thread
- RT-Thread 又没有按这个口径提供

所以本轮需要 NUC 的，是**契约确认**，不是**功能实现**。

---

## 2. 本轮 RK3588 已冻结的映射结论

当前 RK3588 侧已冻结如下口径：

### 2.1 由 NUC 直接提供的字段

- `robot_pose`
- `nav_status`
- `task_status`

说明：

- 这些字段保持 NUC 高层语义
- RK3588 不要求 RT-Thread 直接提供这些内容

### 2.2 由 RT-Thread 提供、再经 NUC 归一化后上送的字段

- `device_status.battery_percent`
- `device_status.emergency_stop`
- `device_status.fault_code`
- `device_status.online`

说明：

- 这 4 个字段是 Phase 3 最小必接字段
- RK3588 不新增 `rtt_*` 平行字段
- NUC 负责把底层真实状态映射回现有公开契约

### 2.3 可选的 env_sensor 占位规则

- `env_sensor.temperature_c`
- `env_sensor.humidity_percent`
- `env_sensor.status`

冻结规则：

- 如果真实环境传感器还没准备好：
  - `temperature_c = null`
  - `humidity_percent = null`
  - `status = "offline"`

这不算协议错误，只表示“传感器暂未接入”。

---

## 3. 本轮需要 NUC 做什么

NUC 本轮只需要完成 3 件事。

### Step 1：确认字段来源是否可落地

NUC 同学需要逐项确认：

#### A. NUC 侧直接已有

- `robot_pose`
- `nav_status`
- `task_status`

#### B. RT-Thread 侧可采到，且 NUC 能映射

- `battery_percent`
- `emergency_stop`
- `fault_code`
- `online`

#### C. 环境传感器如果没有，就接受占位

- `temperature_c = null`
- `humidity_percent = null`
- `status = "offline"`

如果其中任一项当前拿不到，需要明确写出：

- 是 RT-Thread 没有该字段
- 还是 NUC 还没做采集
- 还是已有字段但语义不稳定

### Step 2：确认 NUC 内部模块职责

NUC 本轮需要确认自己的 Phase 3 实现会沿用下面的拆分：

- `rtt_state_collector`
  负责采 RT-Thread
- `state_mapper`
  负责把 NUC 高层状态 + RT-Thread 底层状态合并
- `rk3588_sender`
  负责继续上送 RK3588

本轮不要求代码已经写完，但要求：

- **团队内部确认这条模块边界**

### Step 3：确认是否存在字段歧义

NUC 需要特别确认下面几个最容易打架的点：

1. `device_status.online`
   到底表示：
   - RT-Thread 通信在线
   - 还是底盘设备整体在线

2. `fault_code`
   到底是：
   - 单一底层 fault
   - 还是多个设备 fault 的归一化结果

3. `battery_percent`
   到底是：
   - RT-Thread 直接给百分比
   - 还是 NUC 由电压换算百分比

4. `env_sensor.status`
   在没有传感器时是否接受：
   - `offline`

如果这些点里有任意一个不能接受，必须在本轮就提出来，不要拖到 Phase 3 实机联调时再改。

---

## 4. 本轮不需要 NUC 做的事

以下事情**本轮不要求**：

- 不要求启动 NUC mission 服务
- 不要求做网口联调
- 不要求做 RK3588 命令闭环测试
- 不要求做 RT-Thread 实机读数演示
- 不要求做 Dashboard 截图

这些属于后续 Round 的实现与联调范围，不是本轮“映射冻结”的验收重点。

---

## 5. 本轮最低验收标准

如果要判定“NUC 已完成 Round 1 配合”，至少满足下面 4 条：

1. 明确确认 `robot_pose / nav_status / task_status` 继续由 NUC 直接提供
2. 明确确认 `battery_percent / emergency_stop / fault_code / online` 由 RT-Thread 经 NUC 上送
3. 明确确认 `env_sensor` 在未接入时可接受 `null + offline` 占位
4. 明确写出仍存在的字段歧义或缺口

只要满足这 4 条，本轮就可以判定为：

- **映射冻结阶段的 NUC 配合已完成**

---

## 6. 建议 NUC 回传格式

本轮建议 NUC 同学给一个简短确认结果，格式类似：

```text
Round 1 映射确认结果：
1. robot_pose / nav_status / task_status 由 NUC 提供：确认
2. battery_percent / emergency_stop / fault_code / online 由 RT-Thread 经 NUC 提供：确认
3. env_sensor 未接入时使用 null + offline：确认
4. 当前歧义项：
   - online 的语义需定义为“RT-Thread 通信在线”
   - fault_code 需由 state_mapper 统一成单字符串
```

这样就足够支撑进入下一轮 T3-2。

---

## 7. 本轮结论

本轮 **需要 NUC 配合**，但配合内容是：

- **字段来源确认**
- **模块职责确认**
- **歧义项确认**

而不是：

- **代码开发**
- **实机联调**
- **网口验收**

所以这轮更像是一次 **Phase 3 的契约对齐会议输出**，不是联调验收单。
