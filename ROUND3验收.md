# ROUND3验收

## 验收时间

- 本次实测时间：`2026-04-14`（Asia/Shanghai）
- RK3588 返回中的接口时间戳为 UTC，主要集中在 `2026-04-13T17:36Z ~ 2026-04-13T17:41Z`

## 最终结论

按 [NUC_DO2.md](/home/robomaster/QHXD_NUC/NUC_DO2.md#L1) 的 Round 3 验收标准，本轮 **暂不能判定整轮验收通过**。

更准确地说：

- `NUC -> RK3588` 状态上送链路：**通过**
- `NUC mission 服务本身`：**通过**
- `命令 -> 状态回流`（直接调用 NUC mission 接口）：**通过**
- `RK3588 public mission API -> NUC mission bridge`：**失败**

因此当前结论应为：

- **NUC 端 Round 3 代码功能基本具备**
- **但由于 RK3588 -> NUC 的任务桥接当前没有真正打通，所以整轮 Round 3 暂不通过**

## 本次测试环境

- NUC 有线网口 `enp89s0`
  - 激光雷达地址：`192.168.1.50/24`
  - Round 3 联调第二地址：`192.168.10.3/24`
- RK3588 有线地址：`192.168.10.2/24`
- RK3588 后端端口：`8000`
- NUC mission 服务端口：`8090`
- NUC mission 路径：`/api/internal/rk3588/mission`

## 本次实际执行结果

### 1. 纯有线链路

在 NUC 上执行：

```bash
ping -c 2 192.168.10.2
```

结果：

- `2/2` 收到响应
- RTT 约 `0.5 ms`

结论：

- **通过**

### 2. RK3588 后端可访问

在 NUC 上执行：

```bash
curl --noproxy '*' http://192.168.10.2:8000/health
```

结果：

```json
{"status":"ok"}
```

结论：

- **通过**

### 3. 切换到 `real` 模式

在 NUC 上执行：

```bash
curl --noproxy '*' -X POST \
  -H 'Content-Type: application/json' \
  -d '{"mode":"real","source":"integration-test","requested_by":"operator"}' \
  http://192.168.10.2:8000/api/system/mode/switch
```

结果关键字段：

- `success=true`
- `accepted=true`
- `system_mode.mode=real`

结论：

- **通过**

### 4. `NUC -> RK3588` 状态上送

我实际启动了 NUC 状态上送进程，并持续向：

```text
http://192.168.10.2:8000/api/internal/nuc/state
```

发送状态。

运行日志持续出现：

```text
State sent successfully. status=200 accepted=True
```

结论：

- **通过**

### 5. `GET /api/state/latest`

在 NUC 上执行：

```bash
curl --noproxy '*' http://192.168.10.2:8000/api/state/latest
```

结果确认：

- `system_mode.mode=real`
- `task_status.source=nuc`
- 返回内容随 NUC 上送持续变化

结论：

- **通过**

### 6. `WS /ws/state`

在 NUC 上执行原始 WebSocket 升级请求后，收到：

- `HTTP/1.1 101 Switching Protocols`
- 持续收到 `robot_state` 帧

且消息中可见：

- `task_status.source=nuc`
- `nav_status.current_goal=home`
- `task_status.task_type=return_home`

结论：

- **通过**

## NUC mission 服务验证

我已经实际启动了 NUC mission 服务：

```text
http://0.0.0.0:8090/api/internal/rk3588/mission
```

并直接调用该接口进行了命令测试。

### 1. `go_to_waypoint`

请求：

```json
{
  "command": "go_to_waypoint",
  "source": "validation",
  "requested_by": "codex",
  "payload": {
    "waypoint_id": "wp-round3-direct"
  }
}
```

返回关键字段：

- `accepted=true`
- `task_status.task_type=go_to_waypoint`
- `task_status.state=running`
- `current_goal=wp-round3-direct`
- `nav_state=running`

随后 `GET /api/state/latest` 中实际看到：

- `task_status.task_type=go_to_waypoint`
- `task_status.state=running`
- `nav_status.current_goal=wp-round3-direct`

结论：

- **通过**

### 2. `pause_task`

请求：

```json
{
  "command": "pause_task",
  "source": "validation",
  "requested_by": "codex",
  "payload": {}
}
```

返回关键字段：

- `accepted=true`
- `task_status.state=paused`
- `nav_state=paused`

随后 `GET /api/state/latest` 中实际看到：

- `task_status.state=paused`
- `nav_status.state=paused`
- `nav_status.current_goal=wp-round3-direct`

结论：

- **通过**

### 3. `return_home`

请求：

```json
{
  "command": "return_home",
  "source": "validation",
  "requested_by": "codex",
  "payload": {}
}
```

返回关键字段：

- `accepted=true`
- `task_status.task_type=return_home`
- `task_status.state=running`
- `current_goal=home`
- `nav_state=running`

随后 `GET /api/state/latest` 中实际看到：

- `task_status.task_type=return_home`
- `task_status.state=running`
- `nav_status.current_goal=home`
- `nav_status.state=running`

结论：

- **通过**

## RK3588 public mission API 桥接验证

按 [NUC_DO2.md](/home/robomaster/QHXD_NUC/NUC_DO2.md#L261) 要求，我又通过 RK3588 public mission API 实测了三条命令：

- `POST /api/mission/go_to_waypoint`
- `POST /api/mission/pause`
- `POST /api/mission/return_home`

### 1. `POST /api/mission/go_to_waypoint`

返回结果关键字段：

- `accepted=false`
- `detail="无法连接 NUC 命令接口：[Errno 111] Connection refused"`

### 2. `POST /api/mission/pause`

返回结果关键字段：

- `accepted=false`
- `detail="无法连接 NUC 命令接口：[Errno 111] Connection refused"`

### 3. `POST /api/mission/return_home`

返回结果关键字段：

- `accepted=false`
- `detail="无法连接 NUC 命令接口：[Errno 111] Connection refused"`

### 4. `GET /api/commands/logs`

我随后抓取了：

```bash
curl --noproxy '*' http://192.168.10.2:8000/api/commands/logs
```

新增日志项中可以看到：

- `id=38` `command=go_to_waypoint` `accepted=false`
- `id=39` `command=pause` `accepted=false`
- `id=40` `command=return_home` `accepted=false`

并且三条日志的共同错误信息都是：

```text
无法连接 NUC 命令接口：[Errno 111] Connection refused
```

结论：

- **失败**

## 为什么当前不能正式通过

按 [NUC_DO2.md](/home/robomaster/QHXD_NUC/NUC_DO2.md#L346) 的最低验收指标，Round 3 需要满足：

1. 纯有线链路可达
2. RK3588 已切到 `real`
3. NUC 能成功上送状态
4. `GET /api/state/latest` 能读到 NUC 最新状态
5. `WS /ws/state` 或 Dashboard 能看到 real 状态变化
6. NUC 已提供 `POST /api/internal/rk3588/mission`
7. 至少 3 条命令被 RK3588 转发到 NUC 成功
8. `GET /api/commands/logs` 能看到命令日志
9. 命令后的状态变化能从 NUC 回流到 RK3588

本次结果中：

- 第 1~6 项：**通过**
- 第 8 项：**部分通过**
  - 有命令日志，但日志结果为 `accepted=false`
- 第 7 项：**失败**
  - 3 条 public mission 命令都没有成功转发到当前 NUC mission 服务
- 第 9 项：**失败**
  - 因为 RK3588 public mission bridge 没有打到 NUC，所以无法证明“经 RK 转发的命令结果”已经从 NUC 回流

因此整轮 Round 3：

- **暂不能正式通过**

## 当前最准确的问题定位

根据本次实测，可以明确区分：

### 已确认正常的部分

- NUC 有线网络
- RK3588 后端可访问
- RK3588 real 模式切换
- NUC 状态上送
- RK3588 状态接收
- `GET /api/state/latest`
- `WS /ws/state`
- NUC mission 服务本身
- 命令 -> 状态回流（直接调用 NUC mission 服务时）

### 当前失败的部分

- RK3588 public mission bridge 到 NUC mission 服务的实际转发

### 最可能的直接原因

RK3588 当前用于任务桥接的：

- `NUC_BASE_URL`
- 或 `NUC_MISSION_PATH`
- 或对应目标端口

没有正确指向当前实际可用的 NUC mission 服务：

```text
http://192.168.10.3:8090/api/internal/rk3588/mission
```

因为如果 bridge 目标正确，那么本次通过 public mission API 发出的三条命令，不应全部返回：

```text
[Errno 111] Connection refused
```

## 建议同步给 RK3588 端的信息

建议直接同步以下结论：

```text
NUC 侧 Round 3 状态上送链路已通过：
- 纯有线网络可达
- POST /api/internal/nuc/state 正常
- GET /api/state/latest 正常
- WS /ws/state 正常

NUC mission 服务本身也已就绪，并可在
http://192.168.10.3:8090/api/internal/rk3588/mission
直接接收 go_to_waypoint / pause_task / return_home，
且命令结果能继续通过状态上送回流到 RK3588。

当前 Round 3 未通过的唯一核心阻塞是：
RK3588 public mission bridge 仍未正确连到上述 NUC mission 地址。

证据是：
POST /api/mission/go_to_waypoint
POST /api/mission/pause
POST /api/mission/return_home
三条命令均返回 accepted=false，
detail 均为：
无法连接 NUC 命令接口：[Errno 111] Connection refused
```

## 当前建议结论

当前建议对外结论写成：

```text
NUC Round 3 当前完成度如下：

1. NUC -> RK3588 状态上送链路已通过实机有线验收
2. NUC mission 接口已实现并可直接工作
3. 命令到状态回流逻辑已具备
4. 但 RK3588 -> NUC public mission bridge 当前仍未打通

因此，本轮 Round 3 暂不判整体通过；
需要 RK3588 端修正 NUC bridge 目标地址/端口后，再补一次 bridge 闭环验收。
```
