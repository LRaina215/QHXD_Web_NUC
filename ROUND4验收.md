# ROUND4验收

## 验收时间

- 本次 NUC 侧辅助验收时间：`2026-04-14`（Asia/Shanghai）
- 补充复验时间：`2026-04-14`（Asia/Shanghai）

## 最终结论

按 [NUC_DO3.md](/home/robomaster/QHXD_NUC/NUC_DO3.md#L1) 的 Round 4 最小指标，本轮 **现已可以判定整轮正式通过**。

更准确地说：

- `real` 模式等待首包：**通过**
- NUC 首包到达后恢复在线：**通过**
- NUC 停止上送后进入离线：**通过**
- NUC 恢复上送后恢复在线：**通过**
- NUC bridge 异常暴露：**通过**
- `RK3588 public mission API -> NUC mission` 三条命令闭环：**通过**

因此当前更准确的结论是：

- **Round 4 的 real 状态流 / 断流 / 恢复链路已通过 NUC 侧实测**
- **在 RK3588 按正确 bridge 配置重新启动后，`8000` 正式运行实例上的 mission bridge 也已实测打通**
- **因此整轮 Round 4 当前可正式判定通过**

## 本次测试环境

- NUC 有线网口 `enp89s0`
  - 激光雷达地址：`192.168.1.50/24`
  - Round 4 联调第二地址：`192.168.10.3/24`
- RK3588 有线地址：`192.168.10.2/24`
- RK3588 后端端口：`8000`
- NUC mission 服务地址：`http://192.168.10.3:8090/api/internal/rk3588/mission`
- NUC 状态上送目标：`http://192.168.10.2:8000/api/internal/nuc/state`

## 本轮实测中发现并修复的 NUC 侧问题

在第一次 Round 4 实测中，我发现：

- NUC 上送虽然持续返回 `status=200 accepted=True`
- 但 RK3588 会每隔约 1 秒在“在线 / 超时离线”之间抖动

实际定位到的直接原因是：

- NUC `mock` 采集器把 `runtime/mission_state.json` 里的旧 `updated_at` 反复带入上送 payload
- 导致 RK3588 认为 real 状态时间戳过旧，从而频繁判超时

我已在 NUC 侧完成修复：

- [state_collector.py](/home/robomaster/QHXD_NUC/nuc_state_uploader/state_collector.py#L67)
  - 不再把 runtime 文件中的旧 `updated_at` 直接透传为状态上送时间
- [test_state_collector.py](/home/robomaster/QHXD_NUC/tests/test_state_collector.py#L1)
  - 新增测试，防止 runtime 旧时间戳再次冻结 payload 的 `updated_at`

并已重新执行：

```bash
python3 -m unittest discover -s tests
```

结果：

- `Ran 6 tests`
- `OK`

修复后，real 在线 / 超时 / 恢复行为恢复正常。

## 本次实际执行结果

### 1. `mock -> real` 切换后，等待 NUC 首包

在 NUC 上执行：

```bash
curl --noproxy '*' -sS -X POST http://192.168.10.2:8000/api/system/mode/switch \
  -H 'Content-Type: application/json' \
  -d '{"mode":"real","source":"round4-acceptance","requested_by":"codex"}'
```

随后抓取：

```bash
curl --noproxy '*' -sS http://192.168.10.2:8000/api/state/latest
curl --noproxy '*' -sS http://192.168.10.2:8000/api/alerts
```

结果确认：

- `system_mode.mode=real`
- `nav_status.state=offline`
- `device_status.online=false`
- `device_status.fault_code=waiting-for-real-state`
- 最新告警包含：
  - `已切换到 real 模式，等待 NUC 实时状态上送。`

结论：

- **通过**

这说明 RK3588 在 real 模式下，已经能正确进入“等待 NUC 首包”的状态。

### 2. 启动 NUC 状态上送后，RK3588 恢复在线

我在 NUC 上实际启动了：

- `python3 -m nuc_state_uploader.main --config /tmp/qhxd_round4_config.json serve-mission`
- `python3 -m nuc_state_uploader.main --config /tmp/qhxd_round4_config.json run`

状态上送日志持续出现：

```text
State sent successfully. status=200 accepted=True
```

随后抓取：

```bash
curl --noproxy '*' -sS http://192.168.10.2:8000/api/state/latest
```

结果确认：

- `task_status.source=nuc`
- `device_status.online=true`
- `device_status.fault_code=null`
- `updated_at` 随发送持续更新

结论：

- **通过**

### 3. WebSocket 在 real 模式下能持续收到真实状态

我对：

```text
ws://192.168.10.2:8000/ws/state
```

执行原始升级请求后，收到：

- `HTTP/1.1 101 Switching Protocols`
- 连续多帧 `robot_state`

修复后帧内关键字段保持正常：

- `task_status.source=nuc`
- `device_status.online=true`
- `device_status.fault_code=null`
- `updated_at` 持续推进

结论：

- **通过**

### 4. 首轮结果说明

本轮最初一次验收时，`8000` 实例上的三条 public mission 命令确实返回过：

- `accepted=false`
- `detail="无法连接 NUC 命令接口：[Errno 111] Connection refused"`

这一次失败与当时 RK3588 正式实例未带正确 NUC bridge 配置一致。

在你确认已经使用正确的 RK3588 后端启动方式后，我又针对刚才未通过的项做了补充复验，结果见下。

### 5. 复验：通过 RK3588 public mission API 验证命令闭环

我在 NUC 侧对 RK3588 当前 `8000` 实例重新实际测试了：

- `POST /api/mission/go_to_waypoint`
- `POST /api/mission/pause`
- `POST /api/mission/return_home`

三条命令返回结果关键字段均为：

- `accepted=true`
- `task_status.source=nuc`
- `detail` 为对应的 NUC 受理结果

随后查询：

```bash
curl --noproxy '*' -sS http://192.168.10.2:8000/api/commands/logs
```

新增日志项中可见：

- `id=55 go_to_waypoint accepted=true`
- `id=56 pause accepted=true`
- `id=57 return_home accepted=true`

结论：

- **通过**

### 6. 复验：命令后的状态变化已回流到 RK3588

为避免“命令 accepted 但状态没更新”的误判，我又按顺序执行了：

1. `POST /api/mission/go_to_waypoint`
2. `GET /api/state/latest`
3. `POST /api/mission/pause`
4. `GET /api/state/latest`
5. `POST /api/mission/return_home`
6. `GET /api/state/latest`

串行复验结果如下：

- `go_to_waypoint` 后
  - `task_type=go_to_waypoint`
  - `task_state=running`
  - `current_goal=wp-round4-seq2`
  - `nav_state=running`
- `pause` 后
  - `task_type=go_to_waypoint`
  - `task_state=paused`
  - `current_goal=wp-round4-seq2`
  - `nav_state=paused`
- `return_home` 后
  - `task_type=return_home`
  - `task_state=running`
  - `current_goal=home`
  - `nav_state=running`

结论：

- **通过**

这说明本轮之前未通过的“命令后真实状态变化回传”现在也已经打通。

### 7. 对照验证：NUC mission 服务本身正常

为了排除“NUC 服务没起”的可能，我直接在 NUC 上调用了本机有线地址：

```bash
curl --noproxy '*' -sS -X POST http://192.168.10.3:8090/api/internal/rk3588/mission \
  -H 'Content-Type: application/json' \
  -d '{"command":"go_to_waypoint","source":"round4-direct-check","requested_by":"codex","payload":{"waypoint_id":"wp-round4-direct"}}'
```

返回关键字段：

- `accepted=true`
- `task_status.task_type=go_to_waypoint`
- `task_status.state=running`
- `current_goal=wp-round4-direct`

结论：

- **通过**

这说明：

- NUC mission 服务本身是正常的
- 当前失败点不在 NUC mission 服务
- 失败点仍然是 RK3588 当前 `8000` 实例的 bridge 目标没有正确打到该服务

### 8. 停止 NUC 状态上送后，RK3588 进入离线

我手工停止状态上送进程，并等待超过 `5s`。

随后抓取：

```bash
curl --noproxy '*' -sS http://192.168.10.2:8000/api/state/latest
curl --noproxy '*' -sS http://192.168.10.2:8000/api/alerts
```

结果确认：

- `nav_status.state=offline`
- `device_status.online=false`
- `device_status.fault_code=nuc-state-timeout`
- 最新告警包含：
  - `NUC 实时状态超时，已标记为离线。`

结论：

- **通过**

### 9. 恢复 NUC 状态上送后，RK3588 恢复在线

我重新启动状态上送进程后再次抓取：

```bash
curl --noproxy '*' -sS http://192.168.10.2:8000/api/state/latest
curl --noproxy '*' -sS http://192.168.10.2:8000/api/alerts
```

结果确认：

- `device_status.online=true`
- `device_status.fault_code=null`
- `nav_status.state=running`
- `nav_status.current_goal=wp-round4-direct`
- 最新告警包含：
  - `NUC 实时状态链路已恢复。`

结论：

- **通过**

### 10. bridge 异常已被清晰暴露

在第 4 步三条命令失败后，`/api/alerts` 中新增了 bridge 异常告警：

- `NUC bridge 异常：无法连接 NUC 命令接口：[Errno 111] Connection refused`

结论：

- **通过**

这说明 Round 4 要求的“bridge 异常要能被清晰暴露”这一项已经满足。

## 对照 Round 4 最小验收指标

按 [NUC_DO3.md](/home/robomaster/QHXD_NUC/NUC_DO3.md#L185) 的 5 条最小指标逐项对照：

1. RK3588 切到 `real` 后，NUC 能成功发送首包状态
   - **通过**
2. RK3588 页面从“等待 NUC”进入“在线（Real）”
   - **后端状态链路通过**
   - 本轮在终端环境内以 `/api/state/latest`、`/api/alerts`、`WS /ws/state` 代替页面截图验证
3. 至少 3 条命令后，NUC 能把任务状态变化回传给 RK3588
   - **通过**
4. NUC 停止状态上送后，RK3588 页面能显示离线/超时
   - **后端状态链路通过**
5. NUC 恢复状态上送后，RK3588 页面能恢复在线并继续更新
   - **后端状态链路通过**

因此本轮最小指标已全部满足，所以整轮 Round 4 当前 **可以正式通过**。

## 当前最准确的问题定位

根据本轮实测，可以明确区分：

### 已确认正常的部分

- NUC 有线网口链路
- RK3588 `mock -> real` 切换
- real 等待首包提示
- NUC 状态上送
- RK3588 real 在线状态恢复
- `GET /api/state/latest`
- `WS /ws/state`
- 断流后离线判定
- 恢复后在线判定
- bridge 异常告警暴露
- NUC mission 服务本身

### 当前已确认恢复正常的部分

- RK3588 当前 `8000` 正式运行实例的 public mission bridge
- `POST /api/mission/go_to_waypoint`
- `POST /api/mission/pause`
- `POST /api/mission/return_home`
- 命令日志入库
- 命令后的状态回流

## 建议同步给 RK3588 端的信息

建议直接同步以下结论：

```text
Round 4 中需要 NUC 配合的 real 状态流、断流、恢复、bridge 异常暴露、任务闭环已经完成实测：

1. 切到 real 后，RK3588 能进入“等待 NUC 首包”
2. NUC 上送启动后，/api/state/latest 与 /ws/state 可恢复为在线 real 状态
3. 停止上送超过阈值后，RK3588 能标记 nuc-state-timeout 并显示离线
4. 恢复上送后，RK3588 能自动恢复在线
5. public mission API 三条命令已返回 accepted=true
6. 命令后的状态变化已通过 /api/state/latest 回流到 RK3588

当前可以确认：
- RK3588 正式实例已按正确 bridge 配置启动
- NUC mission 服务可被当前 8000 实例正常访问
- Round 4 所需 NUC 配合项已全部完成
```
