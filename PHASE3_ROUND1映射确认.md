# PHASE3 Round1 映射确认

## 结论

按 [NUC_DO4.md](/home/robomaster/QHXD_NUC/NUC_DO4.md#L1) 的要求，本轮 NUC 侧已完成：

- 字段来源确认
- 模块职责确认
- 歧义项确认

因此本轮可判定为：

- **映射冻结阶段的 NUC 配合已完成**

本轮不涉及：

- 新增联调代码
- 网口实机验收
- Dashboard 截图
- mission 闭环复测

## Round 1 映射确认结果

1. `robot_pose / nav_status / task_status` 由 NUC 提供：**确认**
2. `battery_percent / emergency_stop / fault_code / online` 由 RT-Thread 经 NUC 提供：**确认**
3. `env_sensor` 未接入时使用 `null + offline`：**确认**
4. 当前歧义项与缺口：**已列出，见下文**

## 1. 字段来源确认

### 1.1 由 NUC 直接提供的字段

以下字段继续由 NUC 高层语义直接提供：

- `robot_pose`
- `nav_status`
- `task_status`

对应当前代码里的内部结构为：

- `pose`
- `navigation`
- `task`

映射位置见：

- [state_collector.py](/home/robomaster/QHXD_NUC/nuc_state_uploader/state_collector.py#L18)
- [state_mapper.py](/home/robomaster/QHXD_NUC/nuc_state_uploader/state_mapper.py#L103)

当前判断：

- **可落地**

说明：

- 这三类字段已经是 NUC 现有状态上送的主语义
- Phase 3 不需要 RT-Thread 直接提供同名公开字段

### 1.2 由 RT-Thread 提供、再经 NUC 归一化后上送的字段

以下字段在 Phase 3 中按“RT-Thread 原始来源，NUC 归一化后上送”的口径冻结：

- `device_status.battery_percent`
- `device_status.emergency_stop`
- `device_status.fault_code`
- `device_status.online`

对应当前代码中的公开映射位置见：

- [state_mapper.py](/home/robomaster/QHXD_NUC/nuc_state_uploader/state_mapper.py#L154)

当前判断：

- **契约可落地**
- **实际 RT-Thread 采集器尚未落地**

也就是说：

- 公开字段位置已经固定
- NUC 侧也已经有统一归一化入口
- 但本轮代码库里还没有独立的 `rtt_state_collector`
- 目前 `device` 段仍由 `mock/file` 采集链路占位

因此这 4 个字段当前的真实缺口是：

- **不是公开契约缺失**
- **而是 RT-Thread 实际采集接入尚未开始**

### 1.3 `env_sensor` 占位规则确认

当前确认接受以下占位规则：

- `temperature_c = null`
- `humidity_percent = null`
- `status = "offline"`

对应当前代码位置见：

- [state_collector.py](/home/robomaster/QHXD_NUC/nuc_state_uploader/state_collector.py#L58)
- [state_mapper.py](/home/robomaster/QHXD_NUC/nuc_state_uploader/state_mapper.py#L171)

当前判断：

- **可接受**

说明：

- 这不会被视为协议错误
- 只表示环境传感器当前未接入

## 2. 模块职责确认

NUC 侧确认 Phase 3 将沿用以下模块边界：

### 2.1 `rtt_state_collector`

职责：

- 负责从 RT-Thread 侧采集底层设备状态
- 至少输出：
  - `battery_percent`
  - `emergency_stop`
  - `fault_code`
  - `online`

当前状态：

- **职责确认**
- **代码尚未实现**

### 2.2 `state_mapper`

职责：

- 合并 NUC 高层状态与 RT-Thread 底层状态
- 负责字段归一化、默认值、枚举统一、裁剪和公开契约输出

当前状态：

- **职责确认**
- **现有实现已承担该角色**

对应代码：

- [state_mapper.py](/home/robomaster/QHXD_NUC/nuc_state_uploader/state_mapper.py#L103)

### 2.3 `rk3588_sender`

职责：

- 继续负责向 RK3588 上送公开状态
- 不承担字段语义判断
- 不新增 `rtt_*` 平行公开字段

当前状态：

- **职责确认**
- **现有实现已具备**

对应代码：

- [rk3588_sender.py](/home/robomaster/QHXD_NUC/nuc_state_uploader/rk3588_sender.py#L1)

## 3. 字段歧义确认

本轮对最容易打架的 4 个点，给出如下口径。

### 3.1 `device_status.online`

建议冻结语义为：

- **RT-Thread 通信在线**

而不是：

- 底盘所有设备完全健康
- 整车业务绝对可用

原因：

- 这个字段位于 `device_status`，且是最小底层接入字段之一
- 如果把它定义成“整车整体在线”，后续会和导航、业务、桥接状态混淆
- 将其冻结为“RT-Thread 通信在线”最清晰，也最利于故障定位

当前状态：

- **建议采用**
- **需要 Phase 3 团队按此口径统一**

### 3.2 `fault_code`

建议冻结语义为：

- **经 NUC `state_mapper` 归一化后的单字符串 fault 表达**

而不是直接把多个底层 fault 原样平铺到公开接口。

原因：

- RK3588 当前公开契约只有一个 `fault_code`
- 公开接口不应在 Phase 3 额外拆成多个底层字段
- 如果底层是多源 fault，NUC 需要先做优先级归并，再输出单字符串

当前状态：

- **确认需要由 NUC 统一归一化**
- **多 fault 归并规则后续仍需单独细化**

### 3.3 `battery_percent`

建议冻结语义为：

- **优先使用 RT-Thread 直接给出的百分比**
- 如果 RT-Thread 只给电压、不直接给百分比，再由 NUC 换算

原因：

- 直接使用底层已提供的百分比，链路最短、歧义最小
- 如果底层没有百分比，才需要把换算逻辑放到 NUC

当前状态：

- **字段来源方向确认**
- **RT-Thread 是否直接给百分比，当前仍待后续接入时最终确认**

### 3.4 `env_sensor.status`

确认接受：

- 在环境传感器未接入时，固定为 `offline`

当前状态：

- **确认**

## 4. 当前缺口

虽然本轮映射冻结已完成，但当前代码层面仍有以下缺口需要进入后续 Round：

1. 独立的 `rtt_state_collector` 还未实现
2. `battery_percent / emergency_stop / fault_code / online` 目前仍由 mock/file 链路占位
3. `fault_code` 的多源归并规则尚未细化成正式规范
4. `battery_percent` 是否由 RT-Thread 直接给百分比，仍需后续接入时确认

这些缺口不影响本轮“映射冻结确认”通过，但会进入后续 Phase 3 实现阶段。

## 5. 本轮对外可直接使用的简短口径

建议直接对外同步如下内容：

```text
Round 1 映射确认结果：
1. robot_pose / nav_status / task_status 继续由 NUC 提供：确认
2. battery_percent / emergency_stop / fault_code / online 由 RT-Thread 经 NUC 提供：确认
3. env_sensor 未接入时使用 null + offline：确认
4. 当前歧义项：
   - online 建议定义为“RT-Thread 通信在线”
   - fault_code 需由 state_mapper 统一成单字符串
   - battery_percent 优先使用 RT-Thread 直接给出的百分比；若底层仅给电压，再由 NUC 换算
```
