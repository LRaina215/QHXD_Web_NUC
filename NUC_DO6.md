# NUC_DO6.md

## 1. 本轮目的

本轮对应 Phase 3 Round 3。

目标不是再改 RK3588 公开契约，而是让 NUC 侧真正把：

- NUC 高层状态
- RT-Thread 低层设备状态

合并成一份稳定的上送包，发送给 RK3588 的：

```text
POST /api/internal/nuc/state
```

当前 RK3588 侧已经确认：

- `device_status`
- `env_sensor`

会按 NUC 上送值进入共享状态，并继续通过：

- `GET /api/state/latest`
- `GET /api/tasks/current`
- `WS /ws/state`
- Dashboard

对外展示。

所以本轮真正要做的工作在 **NUC 侧 mapper/sender 路径**。

---

## 2. NUC 本轮需要完成什么

NUC 侧需要完成 3 件事。

### A. 在 state mapper 中完成状态合并

要求：

- `robot_pose` 继续来自 NUC 高层状态
- `nav_status` 继续来自 NUC 高层状态
- `task_status` 继续来自 NUC 高层状态
- `device_status` 改为优先使用 `rtt_state_collector` 产物
- `env_sensor` 改为优先使用 `rtt_state_collector` 产物或其占位值

冻结口径：

- 不新增 `rtt_*` 平行公开字段
- 不改 RK3588 的现有 JSON 契约结构

### B. 在 sender 路径中继续使用现有上送接口

要求：

- 继续上送到 `POST /api/internal/nuc/state`
- 不新增新的 RK3588 endpoint
- 不改上送主流程，只升级 payload 内容

### C. 用最小方式证明“真实低层状态已进入 payload”

至少要证明这些字段已经来自 RT-Thread 低层采集或其占位链路：

- `device_status.battery_percent`
- `device_status.emergency_stop`
- `device_status.fault_code`
- `device_status.online`
- `env_sensor.temperature_c`
- `env_sensor.humidity_percent`
- `env_sensor.status`

---

## 3. 推荐执行步骤

### Step 1：先在 NUC 本地检查 raw state 合并结果

目标：

- 确认主 `state_collector` 输出的原始状态中，`device` / `environment` 已包含低层状态

建议方式：

- 在单元测试或 dry-run 里打印 raw state
- 检查 `device` 字段是否来自 `rtt_state_collector`

### Step 2：检查最终 payload

执行：

```bash
cd /home/robomaster/QHXD_Web_NUC
python3 -m nuc_state_uploader.main --config configs/default_config.json send-once --dry-run --print-payload
```

预期至少确认：

- `device_status.battery_percent`
- `device_status.emergency_stop`
- `device_status.fault_code`
- `device_status.online`
- `env_sensor.status`

### Step 3：切 RK3588 到 real 模式

在 RK3588 上：

```bash
curl -X POST http://127.0.0.1:8000/api/system/mode/switch \
  -H 'Content-Type: application/json' \
  -d '{"mode":"real","source":"phase3-round3","requested_by":"operator"}'
```

### Step 4：从 NUC 实际上送一次状态

执行：

```bash
cd /home/robomaster/QHXD_Web_NUC
python3 -m nuc_state_uploader.main --config configs/default_config.json send-once --print-payload
```

### Step 5：在 RK3588 上检查 latest state

执行：

```bash
curl http://127.0.0.1:8000/api/state/latest
```

至少应看到：

- `device_status.battery_percent` 为 NUC 当前上送值
- `device_status.emergency_stop` 为 NUC 当前上送值
- `device_status.fault_code` 为 NUC 当前上送值
- `device_status.online` 为 NUC 当前上送值
- `env_sensor.status` 为 NUC 当前上送值

### Step 6：可选检查 Dashboard / WebSocket

如果当前前端已启动，可额外确认：

- Dashboard 上电量、急停、在线状态、环境占位值变化可见

或通过：

```bash
ws://<RK3588_IP>:8000/ws/state
```

确认最新状态帧已包含新的 `device_status / env_sensor`。

---

## 4. 本轮最小验收指标

如果要判定 Phase 3 Round 3 通过，至少满足下面 5 条：

1. NUC `state_mapper` 已将高层 nav/task 状态与低层 device/environment 状态合并
2. 最终上送仍使用 `POST /api/internal/nuc/state`
3. RK3588 `GET /api/state/latest` 中能看到新的真实 `device_status`
4. `env_sensor` 至少能以真实值或 `null + offline` 占位进入 RK3588
5. mock / real 双模式未被破坏

满足这 5 条即可判定：

- **Phase 3 Round 3 的状态上送升级通过**

---

## 5. 本轮建议留证

建议 NUC 与 RK3588 两侧至少保留这些证据：

1. NUC dry-run payload 输出
2. NUC 实际 send-once 输出
3. RK3588 `GET /api/state/latest` 返回结果
4. 如有条件，再补一份 Dashboard 截图或 WebSocket 帧样例

---

## 6. 本轮不需要做的事

本轮不要求：

- 不要求修改 RK3588 前端布局
- 不要求新增 RK3588 endpoint
- 不要求 mission 到 RT-Thread 的真实闭环
- 不要求语音、图传、视觉增强

这些都属于后续 Round。

---

## 7. 本轮通过后的对外表述

建议通过后统一使用下面这段表述：

```text
Phase 3 Round 3 已完成 NUC -> RK3588 状态上送升级。

当前已验证：
1. NUC 可将高层 nav/task 状态与 RT-Thread 低层 device/environment 状态合并
2. 上送继续使用既有 POST /api/internal/nuc/state 契约
3. RK3588 /api/state/latest 已能看到真实 device_status 值
4. env_sensor 可按真实值或 null + offline 占位进入 RK3588

因此，本轮可判定为“RT-Thread 低层状态已进入 NUC -> RK3588 实时状态链路”。
```

---

## 8. 本轮通过后剩余 TODO

下一步仍需继续推进：

1. mission 到 RT-Thread 执行链路的真实闭环
2. `fault_code` 多源归并规则细化
3. `battery_percent` 直给/换算口径最终确认
4. 真实环境传感器接入与异常提示完善
