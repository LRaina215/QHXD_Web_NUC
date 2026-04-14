# PHASE3 Round2 验收

## 验收时间

- 本次验收时间：`2026-04-14`（Asia/Shanghai）

## 最终结论

按 [NUC_DO5.md](/home/robomaster/QHXD_NUC/NUC_DO5.md#L1) 的最小验收指标，本轮 **可以判定通过**。

更准确地说：

- 独立 `rtt_state_collector` 模块：**已存在**
- collector 可产出标准化低层状态对象：**通过**
- 主 `state_collector` 可消费并并入 raw state：**通过**
- `state_mapper` 可继续映射到 `device_status / env_sensor`：**通过**
- `mock / file` 低层采集模式已验证：**通过**

因此本轮可对外判定为：

- **Phase 3 Round 2 的最小 RT-Thread low-level collector 骨架通过**

## 本次实际检查内容

### 1. 独立模块存在性检查

已确认以下文件存在：

- [rtt_state_collector.py](/home/robomaster/QHXD_NUC/nuc_state_uploader/rtt_state_collector.py#L1)
- [state_collector.py](/home/robomaster/QHXD_NUC/nuc_state_uploader/state_collector.py#L1)
- [config.py](/home/robomaster/QHXD_NUC/nuc_state_uploader/config.py#L1)

结论：

- **通过**

### 2. 单元测试

执行：

```bash
python3 -m unittest discover -s tests -v
```

实际结果：

- `Ran 9 tests`
- `OK`

其中本轮直接相关的测试包括：

- `test_rtt_file_collector_normalizes_low_level_state`
- `test_main_collector_merges_rtt_low_level_state_into_raw_state`
- `test_rtt_file_collector_normalizes_boundaries_and_empty_values`

结论：

- **通过**

### 3. dry-run payload 检查

执行：

```bash
python3 -m nuc_state_uploader.main --config configs/default_config.json send-once --dry-run --print-payload
```

实际打印出的 payload 中确认可见：

- `device_status.battery_percent`
- `device_status.emergency_stop`
- `device_status.fault_code`
- `device_status.online`
- `env_sensor.status`

本次样例结果中关键字段为：

- `device_status.battery_percent = 98`
- `device_status.emergency_stop = false`
- `device_status.fault_code = null`
- `device_status.online = true`
- `env_sensor.status = "offline"`

结论：

- **通过**

### 4. 标准化低层对象独立检查

执行：

```bash
python3 - <<'PY'
from nuc_state_uploader.rtt_state_collector import MockRttStateCollector
state = MockRttStateCollector().collect(4)
print(state.as_dict())
print(state.as_internal_sections())
PY
```

实际结果确认：

- 第一段输出为标准化低层状态对象
- 第二段输出为可供主 `state_collector` 合并的 `device/environment` 结构

样例关键字段：

- `battery_percent = 94`
- `emergency_stop = False`
- `fault_code = None`
- `online = True`
- `velocity_mps = 0.646`
- `env_status = "offline"`

结论：

- **通过**

### 5. file 模式检查

我按 [NUC_DO5.md](/home/robomaster/QHXD_NUC/NUC_DO5.md#L92) 的建议准备了一个临时文件样例：

```json
{
  "battery_percent": 64,
  "emergency_stop": false,
  "fault_code": null,
  "online": true,
  "velocity_mps": 0.45,
  "temperature_c": null,
  "humidity_percent": null,
  "status": "offline"
}
```

然后让 `rtt_collector.type=file` 指向该文件，并检查主采集器与最终 payload。

确认结果：

- `raw_state["device"]["battery_percent"] = 64`
- `raw_state["device"]["velocity_mps"] = 0.45`
- `payload["device_status"]["battery_percent"] = 64`
- `payload["device_status"]["online"] = true`
- `payload["env_sensor"]["status"] = "offline"`

结论：

- **通过**

## 推荐检查项结果

### 1. `battery_percent` 边界

已补测试确认：

- `< 0` 会裁剪到 `0`
- `> 100` 会裁剪到 `100`

结论：

- **通过**

### 2. `emergency_stop` 布尔归一化

已确认以下输入可归一化：

- `"true"` / `"false"`
- `"yes"` / `"no"`
- `1` / `0`

结论：

- **通过**

### 3. `fault_code` 空值处理

已确认：

- `""`
- `null`

最终都可归一化为 `None / null`。

结论：

- **通过**

### 4. `env_sensor` 占位语义

已确认在未接入真实传感器时：

- `temperature_c = null`
- `humidity_percent = null`
- `status = "offline"`

这组语义在 mock / file 检查中都成立。

结论：

- **通过**

## 本轮代码变更

本轮为了补足推荐检查项，新增了一条测试：

- [test_state_collector.py](/home/robomaster/QHXD_NUC/tests/test_state_collector.py#L1)

新增测试名称：

- `test_rtt_file_collector_normalizes_boundaries_and_empty_values`

它用于固定以下行为：

- `battery_percent` 裁剪
- `emergency_stop` 布尔归一化
- `fault_code` 空值归一化
- `online` 布尔归一化
- `env_status = "offline"` 占位语义

## 对照最小验收指标

按 [NUC_DO5.md](/home/robomaster/QHXD_NUC/NUC_DO5.md#L117) 的 5 条最小指标逐项对照：

1. 已存在独立 `rtt_state_collector` 模块
   - **通过**
2. collector 可产出标准化低层状态对象
   - **通过**
3. 主 `state_collector` 能消费该对象并并入 raw state
   - **通过**
4. `state_mapper` 生成的最终 payload 中可看到对应 `device_status / env_sensor`
   - **通过**
5. `mock` 和 `file` 两种低层采集模式至少有一种单测通过，且 dry-run 可打印最终 payload
   - **通过**

因此本轮：

- **正式通过**

## 本轮通过后的结论表述

建议当前对外统一使用下面这段表述：

```text
Phase 3 Round 2 已完成 NUC 侧最小 RT-Thread low-level state collector 骨架。

当前已验证：
1. rtt_state_collector 可独立产出标准化低层状态对象
2. 主 state_collector 可将低层状态并入原始状态
3. state_mapper 可将其映射到 RK3588 payload 的 device_status / env_sensor
4. mock / file 占位模式可用于后续在未接真实 RT-Thread 前继续联调

因此，本轮可判定为“NUC 侧低层状态采集骨架通过”，后续进入真实 RT-Thread 接入阶段。
```

## 本轮通过后剩余 TODO

本轮通过后仍需继续推进：

1. 用真实 RT-Thread 通信源替换 `MockRttStateCollector / FileRttStateCollector`
2. 明确 `battery_percent` 是底层直给还是由 NUC 换算
3. 细化多源 `fault_code` 到单字符串的归并规则
4. 将 `online` 的最终语义固定为“RT-Thread 通信在线”
5. 若环境传感器后续接入，补齐真实 `temperature_c / humidity_percent`
