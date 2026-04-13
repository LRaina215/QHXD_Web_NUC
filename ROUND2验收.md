# Round3 验收

## 实测时间

- 本次正式复核时间：`2026-04-14`（Asia/Shanghai）

## 最终判定

本次已经在 **纯有线 `192.168.10.2 <-> 192.168.10.3`** 链路下，实际完成了 NUC 状态上发的关键验收项验证。

当前可以给出的准确结论是：

- **NUC 侧状态上发工程验收通过**
- **纯有线网口链路验收通过**
- **应用层接口联调验收通过**

当前唯一没有在本终端环境内直接完成截图留证的是：

- Dashboard 页面可视化刷新截图

但从接口与 WebSocket 的实际结果看，Dashboard 所依赖的数据链路已经成立。

## 本次测试前提

当前网络前提如下：

- NUC 激光雷达通信网口：`enp89s0 = 192.168.1.50/24`
- 为本次 RK3588 纯有线联调，在同一张 `enp89s0` 上额外存在第二地址：
  - `192.168.10.3/24`
- RK3588 有线网口已配置：
  - `192.168.10.2/24`

本次验证时，我没有删除或替换激光雷达原地址 `192.168.1.50/24`。

## 本次实际完成的验证项

### 1. 纯有线链路连通

已实际验证：

```bash
ping -c 2 192.168.10.2
```

结果：

- `2/2` 收到响应
- RTT 约 `0.6~0.7 ms`

这说明 NUC 到 RK3588 的纯有线路径已经连通。

### 2. `GET /docs`

已实际验证：

```bash
curl --noproxy '*' -D - http://192.168.10.2:8000/docs
```

结果：

- `HTTP/1.1 200 OK`

这说明 RK3588 后端在纯有线路径下可正常访问。

### 3. `POST /api/system/mode/switch`

已实际验证：

```bash
curl --noproxy '*' -D - \
  -H 'Content-Type: application/json' \
  -d '{"mode":"real","source":"integration-test","requested_by":"operator"}' \
  http://192.168.10.2:8000/api/system/mode/switch
```

结果：

- `HTTP/1.1 200 OK`
- `accepted=true`
- `system_mode.mode=real`

这说明 RK3588 已切换到 `real` 模式。

### 4. `POST /api/internal/nuc/state`

已通过 NUC 发送器内部模块，向纯有线地址 `http://192.168.10.2:8000` 实际上送状态。

第一次上送的关键状态字段为：

- `robot_pose.x=2.54`
- `robot_pose.y=1.841`
- `robot_pose.yaw=0.75`
- `nav_status.current_goal=wp-2`
- `task_status.task_id=task-live-001`
- `task_status.progress=30`
- `task_status.source=nuc`
- `device_status.battery_percent=94`

接口返回：

- `ok=True`
- `status_code=200`
- `accepted=true`
- `state_updated=true`

随后我又发送了一包更容易识别的“验收标记值”状态：

- `robot_pose.x=88.801`
- `robot_pose.y=66.602`
- `robot_pose.yaw=1.234`
- `nav_status.mode=manual`
- `nav_status.state=paused`
- `nav_status.current_goal=wired-acceptance-goal`
- `task_status.task_id=wired-acceptance-task`
- `task_status.progress=42`
- `task_status.source=nuc`
- `device_status.battery_percent=55`
- `device_status.fault_code=acceptance-check`
- `env_sensor.temperature_c=11.1`
- `env_sensor.humidity_percent=22.2`
- `env_sensor.status=warning`

该次接口返回同样为：

- `accepted=true`
- `state_updated=true`

这说明：

- RK3588 已在纯有线路径下接收并写入 NUC 实时状态

### 5. `GET /api/state/latest`

我对 `http://192.168.10.2:8000/api/state/latest` 做了连续抓取。

结果如下：

- 第一次抓取看到的是上一包 NUC 状态
- 随后第二次、第三次抓取，已经稳定反映“验收标记值”状态

最终确认到的关键字段为：

- `system_mode.mode=real`
- `task_status.source=nuc`
- `robot_pose.x=88.801`
- `robot_pose.y=66.602`
- `robot_pose.yaw=1.234`
- `nav_status.mode=manual`
- `nav_status.state=paused`
- `nav_status.current_goal=wired-acceptance-goal`
- `task_status.task_id=wired-acceptance-task`
- `task_status.progress=42`
- `device_status.battery_percent=55`
- `device_status.fault_code=acceptance-check`
- `env_sensor.temperature_c=11.1`
- `env_sensor.humidity_percent=22.2`
- `env_sensor.status=warning`

这说明：

- `GET /api/state/latest` 已正确反映 NUC 刚上送的数据

### 6. `GET /api/alerts`

已验证接口可正常访问。

说明：

- 本次验收上送未附带新的 NUC 告警
- 当前返回内容中仍可看到 RK3588 侧已有的 mock-state-generator 历史告警

该项不影响本次状态上发主链路通过结论。

### 7. `WS /ws/state`

我已在纯有线地址 `http://192.168.10.2:8000/ws/state` 上实际完成 WebSocket 验证。

使用原始升级请求后，得到：

- `HTTP/1.1 101 Switching Protocols`
- 至少收到两帧 `robot_state`

其中第二帧已经明确反映“验收标记值”状态：

- `robot_pose.x=88.801`
- `robot_pose.y=66.602`
- `nav_status.current_goal=wired-acceptance-goal`
- `task_status.task_id=wired-acceptance-task`
- `task_status.source=nuc`
- `device_status.fault_code=acceptance-check`
- `env_sensor.status=warning`

这说明：

- `WS /ws/state` 已在纯有线链路下正确推送 real 状态变化

## 对照验收项的结论

按当前阶段核心验收项逐项对照：

1. RK3588 已切到 `real` 模式
   - **通过**
2. `POST /api/internal/nuc/state` 返回 `accepted=true`
   - **通过**
3. `GET /api/state/latest` 中能看到 NUC 最新状态
   - **通过**
4. `WS /ws/state` 能收到变化中的 real 状态
   - **通过**

从 NUC 侧和后端接口联调角度，这一轮已经满足正式通过条件。

## 当前仍未直接补齐的留证项

当前仍未在本终端环境内直接补齐的是：

1. Dashboard 页面截图或录屏证据

原因是：

- 当前测试环境是终端环境
- 我已完成接口和 WebSocket 验证，但无法在此直接生成浏览器页面截图

不过由于：

- `GET /api/state/latest` 已正确更新
- `WS /ws/state` 已收到对应的 real 状态推送

所以 Dashboard 所依赖的数据基础已经满足。

## 当前可对外使用的正式表述

建议当前对外统一使用下面这段表述：

```text
NUC -> RK3588 状态上发工程已完成，并已在纯有线地址
RK3588 192.168.10.2 <-> NUC 192.168.10.3
链路下完成正式联调验证。

已实际验证：
1. POST /api/system/mode/switch 成功切换到 real
2. POST /api/internal/nuc/state 返回 accepted=true 且 state_updated=true
3. GET /api/state/latest 正确反映 NUC 上送数据
4. WS /ws/state 成功推送 real 状态变化

因此，NUC 侧状态上发实现与有线链路联调均可验收通过。

当前仅剩 Dashboard 页面截图证据可按需要补充留档，但不影响本轮接口与链路通过结论。
```
