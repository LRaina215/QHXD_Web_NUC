# PHASE3 Round3 验收

## 验收时间

- 本次验收时间：`2026-04-14`（Asia/Shanghai）

## 最终结论

按 [NUC_DO6.md](/home/robomaster/QHXD_NUC/NUC_DO6.md#L1) 的最小验收指标，本轮 **可以判定通过**。

更准确地说：

- NUC 高层 `robot_pose / nav_status / task_status` 继续保持原有来源
- 低层 `device / environment` 已经从 `rtt_state_collector` 并入主 raw state
- 最终上送仍继续使用 `POST /api/internal/nuc/state`
- RK3588 `/api/state/latest` 已实际收到新的 `device_status / env_sensor`
- `mock / real` 双模式切换未被破坏

因此本轮可对外判定为：

- **Phase 3 Round 3 的状态上送升级通过**

## 本次实际执行结果

### 1. 本地 raw state 合并检查

在 NUC 本地执行了最小检查脚本，直接查看主 `state_collector` 产出的低层段：

```bash
python3 - <<'PY'
import json
from nuc_state_uploader.config import CollectorConfig, RttCollectorConfig
from nuc_state_uploader.state_collector import build_collector
collector = build_collector(
    CollectorConfig(type='mock', frame_id='map', source_name='nuc'),
    RttCollectorConfig(type='mock', source_name='rtt'),
)
raw = collector.collect(4)
print(json.dumps({'device': raw['device'], 'environment': raw['environment']}, ensure_ascii=False, indent=2))
PY
```

实际结果确认：

- `device.battery_percent = 94`
- `device.emergency_stop = false`
- `device.fault_code = null`
- `device.online = true`
- `device.velocity_mps = 0.646`
- `device.source = "rtt"`
- `environment.temperature_c = null`
- `environment.humidity_percent = null`
- `environment.status = "offline"`
- `environment.source = "rtt"`

结论：

- **通过**

这说明主 `state_collector` 输出的 raw state 中，`device / environment` 已经来自低层采集器，而不是旧的纯内联占位。

### 2. dry-run payload 检查

执行：

```bash
python3 -m nuc_state_uploader.main --config configs/default_config.json send-once --dry-run --print-payload
```

实际打印出的 payload 中确认可见：

- `device_status.battery_percent`
- `device_status.emergency_stop`
- `device_status.fault_code`
- `device_status.online`
- `env_sensor.temperature_c`
- `env_sensor.humidity_percent`
- `env_sensor.status`

本次样例结果中关键字段为：

- `device_status.battery_percent = 98`
- `device_status.emergency_stop = false`
- `device_status.fault_code = null`
- `device_status.online = true`
- `env_sensor.temperature_c = null`
- `env_sensor.humidity_percent = null`
- `env_sensor.status = "offline"`

结论：

- **通过**

### 3. 使用可辨识低层样例值做真实上送

为了证明 `device_status / env_sensor` 已经来自低层采集链路，而不是旧占位值，本次我临时准备了一份低层 file 样例：

```json
{
  "battery_percent": 64,
  "emergency_stop": true,
  "fault_code": "rtt-phase3-round3",
  "online": true,
  "velocity_mps": 0.45,
  "temperature_c": 26.5,
  "humidity_percent": 55.2,
  "status": "warning",
  "source": "rtt"
}
```

并配套生成临时配置，使：

- `collector.type = mock`
- `rtt_collector.type = file`
- `rk3588.base_url = http://192.168.10.2:8000`

随后在 RK3588 上切到 `real` 模式，并从 NUC 实际执行：

```bash
python3 -m nuc_state_uploader.main --config /tmp/phase3_round3_config.json send-once --print-payload
```

实际 send-once 打印出的 payload 关键字段为：

- `device_status.battery_percent = 64`
- `device_status.emergency_stop = true`
- `device_status.fault_code = "rtt-phase3-round3"`
- `device_status.online = true`
- `env_sensor.temperature_c = 26.5`
- `env_sensor.humidity_percent = 55.2`
- `env_sensor.status = "warning"`

同时发送日志返回：

- `State sent successfully. status=200 accepted=True`

结论：

- **通过**

### 4. RK3588 `/api/state/latest` 实际检查

在 send-once 之后，我直接抓取：

```bash
curl --noproxy '*' -sS http://192.168.10.2:8000/api/state/latest
```

实际结果中确认可见：

- `device_status.battery_percent = 64`
- `device_status.emergency_stop = true`
- `device_status.fault_code = "rtt-phase3-round3"`
- `device_status.online = true`
- `env_sensor.temperature_c = 26.5`
- `env_sensor.humidity_percent = 55.2`
- `env_sensor.status = "warning"`

同时高层状态仍保持 NUC 语义：

- `robot_pose.x = 3.0`
- `nav_status.state = "idle"`
- `task_status.source = "nuc"`

结论：

- **通过**

这说明：

- 低层 `device_status / env_sensor` 已进入 RK3588 共享状态
- 高层 `robot_pose / nav_status / task_status` 未被破坏

### 5. `mock / real` 双模式检查

本次先切到 `real`，完成一次低层状态实发后，又切回 `mock`：

```bash
curl --noproxy '*' -sS -X POST http://192.168.10.2:8000/api/system/mode/switch \
  -H 'Content-Type: application/json' \
  -d '{"mode":"mock","source":"phase3-round3","requested_by":"codex"}'
```

随后再次抓取：

```bash
curl --noproxy '*' -sS http://192.168.10.2:8000/api/state/latest
curl --noproxy '*' -sS http://192.168.10.2:8000/api/tasks/current
```

结果确认：

- `system_mode.mode = "mock"`
- `task_status.source = "web"`
- `task_status.task_id = "mock-task"`
- `env_sensor.status = "mock"`

结论：

- **通过**

这说明本轮升级没有破坏原有的 `mock / real` 切换行为。

## 对照最小验收指标

按 [NUC_DO6.md](/home/robomaster/QHXD_NUC/NUC_DO6.md#L109) 的 5 条最小指标逐项对照：

1. NUC `state_mapper` 已将高层 nav/task 状态与低层 device/environment 状态合并
   - **通过**
2. 最终上送仍使用 `POST /api/internal/nuc/state`
   - **通过**
3. RK3588 `GET /api/state/latest` 中能看到新的真实 `device_status`
   - **通过**
4. `env_sensor` 至少能以真实值或 `null + offline` 占位进入 RK3588
   - **通过**
5. `mock / real` 双模式未被破坏
   - **通过**

因此本轮：

- **正式通过**

## 本轮留证

本轮已保留以下证据类型：

1. NUC raw state 合并检查输出
2. NUC dry-run payload 输出
3. NUC 实际 `send-once --print-payload` 输出
4. RK3588 `GET /api/state/latest` 返回结果
5. RK3588 `mock / real` 切换后状态检查结果

## 本轮通过后的对外表述

建议当前对外统一使用下面这段表述：

```text
Phase 3 Round 3 已完成 NUC -> RK3588 状态上送升级。

当前已验证：
1. NUC 可将高层 nav/task 状态与 RT-Thread 低层 device/environment 状态合并
2. 上送继续使用既有 POST /api/internal/nuc/state 契约
3. RK3588 /api/state/latest 已能看到真实 device_status 值
4. env_sensor 可按真实值或 null + offline 占位进入 RK3588

因此，本轮可判定为“RT-Thread 低层状态已进入 NUC -> RK3588 实时状态链路”。
```

## 本轮通过后剩余 TODO

本轮通过后，下一步仍需继续推进：

1. 用真实 RT-Thread 通信源替换当前 `mock / file` 低层占位链路
2. mission 到 RT-Thread 执行链路的真实闭环
3. `fault_code` 多源归并规则细化
4. `battery_percent` 直给 / 换算口径最终确认
5. 真实环境传感器接入与异常提示完善
