# NUC_DO6_1.md

## 1. 本轮任务定位

本文件不是上一轮 `NUC_DO6.md` 的重复版。

`NUC_DO6.md` 解决的是：

- **RT-Thread 派生字段已经能通过 NUC 合并进 RK3588 状态上送链路**

但那一轮允许使用：

- `mock`
- `file`
- 占位低层采集器

所以它证明的是：

- **软件链路打通**

而不是：

- **RT-Thread（C 板）真实实机接入完成**

本文件 `NUC_DO6_1.md` 的目标就是补上这件事：

- **要求 NUC 开始真正接收来自 C 板 / RT-Thread 的真实低层状态**
- **并把真实值送到 RK3588**

---

## 2. 本轮目标

完成一轮“RT-Thread（C 板）真实接入”的最小闭环验证。

目标不是做所有高级能力，而是证明下面这条链路已经真实成立：

```text
C 板 / RT-Thread 真实状态
-> NUC rtt_state_collector
-> NUC state_mapper
-> POST /api/internal/nuc/state
-> RK3588 /api/state/latest
-> Dashboard / WebSocket 可见
```

---

## 3. 本轮必须来自 C 板真实数据的字段

本轮至少要让下面 4 个字段中的大部分，来自 **真实 C 板 / RT-Thread**，而不是 mock/file：

- `device_status.battery_percent`
- `device_status.emergency_stop`
- `device_status.fault_code`
- `device_status.online`

如果当前环境条件允许，建议同步补：

- `env_sensor.temperature_c`
- `env_sensor.humidity_percent`

如果环境传感器仍未接入，可继续接受：

- `temperature_c = null`
- `humidity_percent = null`
- `status = "offline"`

但 `device_status` 这 4 个字段，至少要优先改成真实来源。

---

## 4. 本轮 NUC 需要完成什么

### A. 接上真实 C 板通信链路

NUC 需要让 `rtt_state_collector` 不再只依赖：

- `mock`
- `file`

而是增加一个真实采集来源，例如：

- 串口
- USB CDC
- UART
- CAN
- 或你们当前已经存在的任一稳定链路

要求：

- 采集逻辑仍然放在 `rtt_state_collector`
- 不要把这部分逻辑塞回导航主流程
- 不要为了这轮重写整套 NUC 主程序

### B. 归一化真实字段

无论 C 板原始协议长什么样，最终都要在 NUC 里归一化成：

- `battery_percent`
- `emergency_stop`
- `fault_code`
- `online`
- `velocity_mps`（如果已有）

要求：

- 统一从 `rtt_state_collector` 输出标准对象
- 再由 `state_mapper` 合并到公开 payload

### C. 保持 RK3588 契约不变

要求继续保持：

- `POST /api/internal/nuc/state`
- 既有 `device_status`
- 既有 `env_sensor`

不要新增：

- `rtt_device_status`
- `low_level_status`
- `cboard_status`

这类平行公开字段。

### D. 留下“真实来源证据”

本轮重点不是“我说我接上了”，而是：

- **要能证明这些值真的来自 C 板**

所以至少要留下一组：

- 原始采集日志
- 或原始报文样例
- 或解析前后的对照记录

来证明字段不是 mock/file 造出来的。

---

## 5. 推荐执行步骤

### Step 1：确认 C 板通信方式

NUC 先明确：

- 当前 C 板通过什么方式与 NUC 通信
- 真实状态数据从哪里拿

建议在验收说明中至少写清：

- 接口类型：串口 / CAN / USB CDC / 其他
- 设备名或链路名
- 轮询还是订阅

### Step 2：在 NUC 本地打印一次真实低层对象

目标：

- 证明 `rtt_state_collector` 已经拿到真实值

建议至少打印：

- `battery_percent`
- `emergency_stop`
- `fault_code`
- `online`
- `velocity_mps`（如果有）

同时注明：

- 这些值来自真实 C 板
- 不是 mock/file

### Step 3：做一次 NUC 本地 raw state 检查

目标：

- 确认主 `state_collector` 输出中，`device / environment` 已来自真实低层链路

建议至少展示：

- `device.source = "rtt"` 或等价可辨识来源
- 实际字段值

### Step 4：做一次 dry-run payload 检查

执行：

```bash
cd /home/robomaster/QHXD_Web_NUC
python3 -m nuc_state_uploader.main --config <你的真实配置文件> send-once --dry-run --print-payload
```

预期：

- payload 里能看到真实 `device_status`
- 字段值与 C 板采集结果一致

### Step 5：切 RK3588 到 real 模式

在 RK3588 上执行：

```bash
curl -X POST http://127.0.0.1:8000/api/system/mode/switch \
  -H 'Content-Type: application/json' \
  -d '{"mode":"real","source":"phase3-round3-real-rtt","requested_by":"operator"}'
```

### Step 6：执行一次真实 send-once

在 NUC 上执行：

```bash
cd /home/robomaster/QHXD_Web_NUC
python3 -m nuc_state_uploader.main --config <你的真实配置文件> send-once --print-payload
```

要求：

- 这次 payload 必须来自真实 C 板数据
- 不是 file 样例

### Step 7：在 RK3588 上检查 latest state

执行：

```bash
curl --noproxy '*' -sS http://<RK3588_IP>:8000/api/state/latest
```

至少确认：

- `device_status.battery_percent`
- `device_status.emergency_stop`
- `device_status.fault_code`
- `device_status.online`

与 NUC 本地采集结果一致。

### Step 8：做一组“真实变化”验证

这一项很关键。

至少要证明**值会随着 C 板真实状态变化而变化**，而不是静态假值。

建议至少做其中一项：

1. 改变真实供电状态 / 电量读数模拟输入
2. 触发一次急停开关
3. 让底层通信断开再恢复
4. 让 fault_code 发生一次可控变化

然后确认：

- NUC 本地采集值变化
- send-once / run 的 payload 变化
- RK3588 `/api/state/latest` 变化

---

## 6. 本轮最小验收指标

如果要判定“真实 C 板接入”通过，至少满足以下 6 条：

1. 说明清楚 C 板与 NUC 的真实通信方式
2. 至少 4 个关键低层字段来自真实 C 板：
   - `battery_percent`
   - `emergency_stop`
   - `fault_code`
   - `online`
3. NUC 本地能打印真实低层标准化对象
4. NUC 上送到 RK3588 的 payload 中能看到这些真实字段
5. RK3588 `/api/state/latest` 中能看到这些真实字段
6. 至少一项真实状态变化能从 C 板传到 RK3588

满足这 6 条，才建议表述为：

- **RT-Thread（C 板）真实状态已接入 NUC -> RK3588 实时状态链路**

---

## 7. 哪些情况不算通过

出现以下任一情况，不建议判通过：

1. 低层字段仍然主要来自 mock/file
2. 只能打印 payload，但说不清 C 板真实来源
3. RK3588 能看到值，但这些值其实是手工文件伪造
4. 没有任何一项真实状态变化验证
5. 只能证明“软件结构存在”，不能证明“真实 C 板数据已经跑通”

---

## 8. 建议留证

建议至少保留以下证据：

1. C 板通信方式说明
2. NUC 本地真实低层对象打印结果
3. NUC 实际上送 payload
4. RK3588 `/api/state/latest` 返回结果
5. 至少一组真实变化前后对比记录

如果能补充更好：

6. WebSocket 帧样例
7. Dashboard 截图

---

## 9. 本轮通过后的对外表述

建议通过后统一用这段：

```text
Phase 3 Round 3.1 已完成 RT-Thread（C板）真实状态接入验证。

当前已验证：
1. C 板真实低层状态可被 NUC 采集
2. battery_percent / emergency_stop / fault_code / online 已从真实链路进入 NUC 标准化对象
3. NUC 可继续通过既有 POST /api/internal/nuc/state 将这些值上送给 RK3588
4. RK3588 /api/state/latest 已能看到真实低层状态
5. 至少一项真实状态变化已从 C 板传递到 RK3588

因此，当前可判定 RT-Thread（C板）真实状态已进入三机实时状态链路。
```

---

## 10. 本轮通过后剩余 TODO

本轮通过后，后续仍需继续推进：

1. mission 到 RT-Thread 执行链路的真实闭环
2. 多源 `fault_code` 归并规则正式化
3. `battery_percent` 直给 / 换算口径最终冻结
4. 真实环境传感器接入
5. 异常提示与告警分类细化
