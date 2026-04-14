# ROUND4验收

## 验收时间

- 本次 NUC 侧辅助验收时间：`2026-04-14`（Asia/Shanghai）

## 最终结论

按 [NUC_DO3.md](/home/robomaster/QHXD_NUC/NUC_DO3.md#L1) 的 Round 4 最小指标，本轮 **暂不能判定整轮正式通过**。

更准确地说：

- `real` 模式等待首包：**通过**
- NUC 首包到达后恢复在线：**通过**
- NUC 停止上送后进入离线：**通过**
- NUC 恢复上送后恢复在线：**通过**
- NUC bridge 异常暴露：**通过**
- `RK3588 public mission API -> NUC mission` 三条命令闭环：**失败**

因此当前更准确的结论是：

- **Round 4 的 real 状态流 / 断流 / 恢复链路已通过 NUC 侧实测**
- **当前 `8000` 正式运行实例上的 mission bridge 仍未真正连到 NUC mission 服务**
- **所以整轮 Round 4 还不能正式判定通过**

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

### 4. 通过 RK3588 public mission API 验证命令闭环

我在 NUC 侧对 RK3588 当前 `8000` 实例实际测试了：

- `POST /api/mission/go_to_waypoint`
- `POST /api/mission/pause`
- `POST /api/mission/return_home`

三条命令返回结果关键字段均为：

- `accepted=false`
- `detail="无法连接 NUC 命令接口：[Errno 111] Connection refused"`

随后查询：

```bash
curl --noproxy '*' -sS http://192.168.10.2:8000/api/commands/logs
```

新增日志项中可见：

- `id=51 go_to_waypoint accepted=false`
- `id=52 return_home accepted=false`
- `id=53 pause accepted=false`

结论：

- **失败**

### 5. 对照验证：NUC mission 服务本身正常

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

### 6. 停止 NUC 状态上送后，RK3588 进入离线

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

### 7. 恢复 NUC 状态上送后，RK3588 恢复在线

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

### 8. bridge 异常已被清晰暴露

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
   - **失败**
4. NUC 停止状态上送后，RK3588 页面能显示离线/超时
   - **后端状态链路通过**
5. NUC 恢复状态上送后，RK3588 页面能恢复在线并继续更新
   - **后端状态链路通过**

因此本轮最小指标中，**第 3 条未满足**，所以整轮 Round 4 当前 **不能正式通过**。

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

### 当前失败的部分

- RK3588 当前 `8000` 正式运行实例的 public mission bridge 到 NUC mission 服务的实际转发

### 最可能的直接原因

当前 `8000` 运行实例用于 bridge 的：

- `NUC_BASE_URL`
- `NUC_MISSION_PATH`
- 或目标端口

仍然没有正确指向：

```text
http://192.168.10.3:8090/api/internal/rk3588/mission
```

因为如果当前实例 bridge 目标正确，那么本轮 3 条 public mission 命令不应全部返回：

```text
无法连接 NUC 命令接口：[Errno 111] Connection refused
```

## 建议同步给 RK3588 端的信息

建议直接同步以下结论：

```text
Round 4 中需要 NUC 配合的 real 状态流、断流、恢复、bridge 异常暴露已经完成实测：

1. 切到 real 后，RK3588 能进入“等待 NUC 首包”
2. NUC 上送启动后，/api/state/latest 与 /ws/state 可恢复为在线 real 状态
3. 停止上送超过阈值后，RK3588 能标记 nuc-state-timeout 并显示离线
4. 恢复上送后，RK3588 能自动恢复在线
5. public mission API 失败时，/api/alerts 能暴露 bridge 异常

但当前 8000 实例上的三条 public mission 命令：
- /api/mission/go_to_waypoint
- /api/mission/pause
- /api/mission/return_home
仍全部返回 accepted=false，
detail 为：
无法连接 NUC 命令接口：[Errno 111] Connection refused

而 NUC mission 服务本身
http://192.168.10.3:8090/api/internal/rk3588/mission
直打返回 accepted=true。

所以当前阻塞仍是 RK3588 正式实例的 bridge 目标地址/端口未对齐，而不是 NUC mission 服务不可用。
```
