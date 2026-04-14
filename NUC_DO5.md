# NUC_DO5.md

## 1. 本轮文档目的

本文件用于说明：

- Phase 3 Round 2 完成最小 `rtt_state_collector` 之后
- NUC 端接下来需要做哪些测试
- 如何判断这一轮是否验收通过

本轮重点不是 RK3588 联调，而是确认：

- NUC 侧已经具备独立的低层状态采集接口
- 低层状态可以被标准化
- 标准化结果可以并入现有状态上送链路

---

## 2. 当前已完成的实现前提

当前 NUC 仓库已具备：

- 独立的 `rtt_state_collector.py`
- 标准化低层状态对象 `NormalizedLowLevelState`
- `mock` / `file` 两种低层采集方式
- 主 `state_collector` 会把低层状态合并进：
  - `device`
  - `environment`
- `state_mapper` 可继续把这些字段映射成 RK3588 契约

所以本轮 NUC 端要验证的，不是“架构是否存在”，而是：

- **这个 collector 是否真的可用**
- **是否满足 Phase 3 Round 2 的最小要求**

---

## 3. 本轮需要 NUC 做什么

NUC 端本轮建议执行 4 组测试。

### A. 低层采集器对象测试

目标：

- 确认 `rtt_state_collector` 能独立产出标准化低层状态对象

至少要确认这些字段：

- `battery_percent`
- `emergency_stop`
- `fault_code`
- `online`
- `velocity_mps`（如果当前已有）

### B. 主采集链路合并测试

目标：

- 确认主 `state_collector` 已把 RT-Thread 低层状态并入原始状态

也就是要确认最终 raw state 中：

- `device`
- `environment`

已经不再完全依赖旧的内联占位逻辑，而是能消费 `rtt_state_collector` 产物。

### C. 最终 payload 映射测试

目标：

- 确认 `state_mapper` 生成的最终 payload 可以把低层状态正确放进：
  - `device_status`
  - `env_sensor`

### D. 配置切换测试

目标：

- 确认 `configs/default_config.json` 和 `config.py` 已支持 `rtt_collector`
- 确认 `mock` / `file` 两种模式都能工作

---

## 4. 推荐执行步骤

### Step 1：进入 NUC 仓库

```bash
cd /home/robomaster/QHXD_Web_NUC
```

### Step 2：运行单元测试

```bash
python3 -m unittest discover -s tests -v
```

本轮至少应看到以下测试通过：

- `test_rtt_file_collector_normalizes_low_level_state`
- `test_main_collector_merges_rtt_low_level_state_into_raw_state`

### Step 3：做一次 dry-run payload 检查

```bash
python3 -m nuc_state_uploader.main --config configs/default_config.json send-once --dry-run --print-payload
```

预期：

- 命令执行成功
- 能打印最终 payload
- payload 中能看到：
  - `device_status.battery_percent`
  - `device_status.emergency_stop`
  - `device_status.fault_code`
  - `device_status.online`
  - `env_sensor.status`

### Step 4：直接检查标准化对象

可以执行一个最小 Python 片段：

```bash
python3 - <<'PY'
from nuc_state_uploader.rtt_state_collector import MockRttStateCollector
state = MockRttStateCollector().collect(4)
print(state.as_dict())
print(state.as_internal_sections())
PY
```

预期：

- 第一段输出是标准化低层对象
- 第二段输出是可被主 `state_collector` 合并的 `device/environment` 结构

### Step 5：验证 file 模式

建议准备一个临时文件，例如：

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

然后在测试或交互式脚本中让 `rtt_collector.type=file` 指向该文件，确认：

- `battery_percent=64`
- `online=true`
- `velocity_mps=0.45`

能够最终进入 `raw_state["device"]` 和映射后的 payload。

---

## 5. 本轮最小验收指标

如果要判定本轮“NUC 侧最小 RT-Thread low-level collector”通过，至少满足以下 5 条：

1. 已存在独立 `rtt_state_collector` 模块
2. collector 可产出标准化低层状态对象
3. 主 `state_collector` 能消费该对象并并入 raw state
4. `state_mapper` 生成的最终 payload 中可看到对应 `device_status` / `env_sensor`
5. `mock` 和 `file` 两种低层采集模式至少有一种单测通过，且 dry-run 可打印最终 payload

满足这 5 条，可以判定：

- **Phase 3 Round 2 的最小 collector 骨架通过**

---

## 6. 建议额外检查项

下面这些不是“最小通过”必需，但建议顺手确认。

### 6.1 battery_percent 边界

检查：

- 小于 0 是否被裁剪到 0
- 大于 100 是否被裁剪到 100

### 6.2 emergency_stop 布尔归一化

检查：

- `"true"` / `"false"`
- `"yes"` / `"no"`
- `1` / `0`

是否都能归一化成布尔值。

### 6.3 fault_code 空值处理

检查：

- `""`
- `null`

是否最终会归一化成 `None` / `null`。

### 6.4 env_sensor 占位语义

检查：

- 没有真实传感器时
  - `temperature_c=null`
  - `humidity_percent=null`
  - `status="offline"`

这组语义是否始终成立。

---

## 7. 本轮不要求的内容

以下内容不属于本轮验收范围：

- 不要求已经接上真实 RT-Thread 串口/CAN/USB 通信
- 不要求已经接上真实底盘电量
- 不要求已经接上真实急停物理信号
- 不要求 RK3588 页面已经显示真实 RT-Thread 值
- 不要求 mission 到 RT-Thread 的真实闭环

这些都属于后续 Round。

本轮只验：

- **采集器接口**
- **标准化对象**
- **合并链路**

---

## 8. 本轮通过后的结论表述

如果本轮通过，建议对外统一表述为：

```text
Phase 3 Round 2 已完成 NUC 侧最小 RT-Thread low-level state collector 骨架。

当前已验证：
1. rtt_state_collector 可独立产出标准化低层状态对象
2. 主 state_collector 可将低层状态并入原始状态
3. state_mapper 可将其映射到 RK3588 payload 的 device_status / env_sensor
4. mock / file 占位模式可用于后续在未接真实 RT-Thread 前继续联调

因此，本轮可判定为“NUC 侧低层状态采集骨架通过”，后续进入真实 RT-Thread 接入阶段。
```

---

## 9. 本轮通过后剩余 TODO

本轮通过后，下一步仍需继续推进：

1. 用真实 RT-Thread 通信源替换 `MockRttStateCollector` / `FileRttStateCollector`
2. 明确 `battery_percent` 是底层直给还是由 NUC 换算
3. 细化多源 `fault_code` 到单字符串的归并规则
4. 确认 `online` 的最终语义严格采用“RT-Thread 通信在线”
5. 若环境传感器接入，补齐真实 `temperature_c / humidity_percent`
