# NUC_DO2.md

## 1. 文档目的

本文件用于说明：在 **Phase 2 Round 3** 阶段，NUC 侧为了完成 **网口通信实机测试**，需要做哪些事情、如何执行、以及如何验收。

这份文档只关注两条实机链路：

- `NUC -> RK3588` 状态上送
- `RK3588 -> NUC` 任务桥接

也就是当前需要验证的完整闭环：

```text
Frontend -> RK3588 mission API -> NUC mission endpoint
NUC state uploader -> RK3588 state ingest -> WebSocket / Dashboard
```

---

## 2. 当前 RK3588 已提供的接口

### 2.1 RK3588 提供给 NUC 的接口

#### 模式切换

```http
POST /api/system/mode/switch
```

用途：

- 把 RK3588 切换到 `real` 模式

#### 状态接收

```http
POST /api/internal/nuc/state
```

用途：

- NUC 向 RK3588 上送最新状态

### 2.2 NUC 需要提供给 RK3588 的接口

#### 任务接收

```http
POST /api/internal/rk3588/mission
```

用途：

- RK3588 在 `real` 模式下，把任务命令通过 HTTP 转发给 NUC

当前会转发的命令：

- `go_to_waypoint`
- `start_patrol`
- `pause_task`
- `resume_task`
- `return_home`

---

## 3. 实机测试前提

### 3.1 网络要求

建议使用独立有线网口或同交换机网段，不要混用 `127.0.0.1` 作为跨设备地址。

推荐示例：

- RK3588：`192.168.10.2/24`
- NUC：`192.168.10.3/24`

要求：

- 两端能互相 `ping` 通
- RK3588 后端服务端口可从 NUC 访问
- NUC mission 服务端口可从 RK3588 访问

### 3.2 RK3588 侧前提

RK3588 侧需要满足：

- 后端已启动
- 前端可选启动，用于观察 Dashboard
- 已切换到 `real` 模式

### 3.3 NUC 侧前提

NUC 侧需要满足：

- 状态上送程序可运行
- mission 接口服务可运行
- 配置中使用 **RK3588 有线口 IPv4**
- 不依赖 `127.0.0.1` 跨机访问
- 如果系统配置了代理，发送器需禁用代理或绕过局域网地址

---

## 4. NUC 需要完成的具体功能

### F1：状态上送程序

NUC 需要有一个持续运行的状态上送程序，功能至少包括：

- 周期性采集当前位姿、导航状态、任务状态、设备状态、环境状态
- 按冻结契约组装 JSON
- `POST` 到 RK3588 的 `/api/internal/nuc/state`
- 发送失败时打印日志
- 发送失败时做最小重试
- 不因一次失败直接退出进程

建议频率：

- `1 Hz`

### F2：NUC 任务接收服务

NUC 需要提供一个 HTTP 服务，至少实现：

```http
POST /api/internal/rk3588/mission
```

这个服务至少要做到：

- 接收 RK3588 转发的结构化命令
- 解析 `command/source/requested_by/payload`
- 返回结构化 JSON
- 根据命令更新 NUC 当前任务上下文
- 后续由状态上送程序把任务状态变化继续回传给 RK3588

### F3：任务命令到状态回流

NUC 不能只“收命令然后回 200”，还需要做到：

- `go_to_waypoint` 后，状态上送里的 `task_status` 变为 `running`
- `pause_task` 后，状态上送里的 `task_status.state` 变为 `paused`
- `resume_task` 后，状态上送里的 `task_status.state` 回到 `running`
- `return_home` 后，状态上送里的 `nav_status.current_goal` 变为 `home` 或等价目标

也就是说，NUC 需要负责把“命令受理结果”体现在后续状态上送里。

### F4：最小日志能力

NUC 侧建议至少保留：

- 收到的命令日志
- 发出的状态上送日志
- HTTP 失败日志
- 超时或连接异常日志

当前阶段不要求复杂日志系统，终端日志或文本日志即可。

---

## 5. NUC mission 接口契约

### 5.1 请求格式

RK3588 会向 NUC 发送如下结构：

```json
{
  "command": "go_to_waypoint",
  "source": "web",
  "requested_by": "dashboard",
  "payload": {
    "waypoint_id": "wp-bridge-001"
  }
}
```

可能的 `command` 值：

- `go_to_waypoint`
- `start_patrol`
- `pause_task`
- `resume_task`
- `return_home`

### 5.2 返回格式

NUC 应返回如下结构：

```json
{
  "success": true,
  "data": {
    "accepted": true,
    "command": "go_to_waypoint",
    "task_status": {
      "task_id": "nuc-task-go-to-waypoint",
      "task_type": "go_to_waypoint",
      "state": "running",
      "progress": 10,
      "source": "nuc"
    },
    "current_goal": "wp-bridge-001",
    "nav_state": "running",
    "received_at": "2026-04-14T08:30:00Z",
    "detail": "NUC 已受理 go_to_waypoint 命令。"
  }
}
```

### 5.3 字段约束

#### `command`

必须与请求中的命令一致。

#### `task_status`

字段至少包括：

- `task_id`
- `task_type`
- `state`
- `progress`
- `source`

其中：

- `task_type` 只能是：
  - `placeholder`
  - `go_to_waypoint`
  - `start_patrol`
  - `return_home`
- `state` 只能是：
  - `idle`
  - `pending`
  - `running`
  - `paused`
  - `completed`
  - `failed`
  - `cancelled`
- `progress` 必须在 `0-100`
- `source` 建议固定为 `nuc`

#### `current_goal`

- 有目标点时传字符串
- 无目标时传 `null`

#### `nav_state`

只能是：

- `idle`
- `running`
- `paused`
- `completed`
- `failed`
- `offline`

---

## 6. 实机测试执行清单

建议按这个顺序做。

### Step 1：确认有线网络

在 NUC 上执行：

```bash
ping -c 2 192.168.10.2
```

通过标准：

- `2/2` 收到响应
- 时延稳定

### Step 2：确认 RK3588 后端可访问

在 NUC 上执行：

```bash
curl --noproxy '*' http://192.168.10.2:8000/health
```

通过标准：

- 返回 `{"status":"ok"}`

### Step 3：切 RK3588 到 real 模式

在 NUC 上执行：

```bash
curl --noproxy '*' -X POST \
  -H 'Content-Type: application/json' \
  -d '{"mode":"real","source":"integration-test","requested_by":"operator"}' \
  http://192.168.10.2:8000/api/system/mode/switch
```

通过标准：

- 返回 `success=true`
- 返回里 `system_mode.mode=real`

### Step 4：验证状态上送

让 NUC 状态上送程序向：

```text
http://192.168.10.2:8000/api/internal/nuc/state
```

发一包易识别状态，例如：

- `robot_pose.x=88.801`
- `robot_pose.y=66.602`
- `nav_status.current_goal=wired-acceptance-goal`
- `task_status.task_id=wired-acceptance-task`
- `task_status.source=nuc`

然后在 NUC 或 RK3588 上执行：

```bash
curl http://192.168.10.2:8000/api/state/latest
```

通过标准：

- `system_mode.mode=real`
- `task_status.source=nuc`
- 返回值能看到刚刚上送的“验收标记值”

### Step 5：验证 WebSocket 状态推送

验证：

- Dashboard 页面实时更新
或
- 独立 WS 客户端能收到 real 状态变化

通过标准：

- 至少看到一次 real 状态变化被推送出来

### Step 6：启动 NUC mission 接口

确保 NUC 提供：

```text
http://<NUC_IP>:<NUC_PORT>/api/internal/rk3588/mission
```

并确保 RK3588 侧 mission bridge 的目标地址已指向这个 NUC 服务。

当前 RK3588 侧默认使用环境变量：

- `NUC_BASE_URL`
- `NUC_MISSION_PATH`

NUC 同学需要和 RK3588 同学确认：

- NUC 服务监听地址
- 监听端口
- 路由路径是否保持 `/api/internal/rk3588/mission`

### Step 7：验证至少 3 条命令桥接

从前端 Dashboard 或直接 HTTP 调用以下命令：

- `go_to_waypoint`
- `pause`
- `return_home`

对应 RK3588 public API：

- `POST /api/mission/go_to_waypoint`
- `POST /api/mission/pause`
- `POST /api/mission/return_home`

通过标准：

- NUC mission 接口实际收到 3 条命令
- NUC 返回 `accepted=true`
- RK3588 `GET /api/commands/logs` 中看到对应日志
- RK3588 `GET /api/state/latest` 中看到命令结果导致的状态变化

### Step 8：验证任务结果回流

重点看命令后的状态是否真的从 NUC 回流出来：

- `go_to_waypoint` 后：
  - `task_status.task_type=go_to_waypoint`
  - `task_status.state=running`
  - `nav_status.current_goal=<目标点>`
- `pause_task` 后：
  - `task_status.state=paused`
  - `nav_status.state=paused`
- `return_home` 后：
  - `task_status.task_type=return_home`
  - `nav_status.current_goal=home`

通过标准：

- 不只是命令接口返回成功
- 后续状态上送也实际反映命令效果

---

## 7. 最低验收指标

满足以下全部条件，可以判定本轮 NUC 网口实机测试通过：

1. NUC 和 RK3588 纯有线链路可达
2. RK3588 已切到 `real` 模式
3. NUC 能成功向 `POST /api/internal/nuc/state` 上送状态
4. `GET /api/state/latest` 能读到 NUC 最新状态
5. `WS /ws/state` 或 Dashboard 能看到 real 状态变化
6. NUC 已提供 `POST /api/internal/rk3588/mission`
7. 至少 3 条命令被 RK3588 转发到 NUC 成功
8. `GET /api/commands/logs` 能看到命令日志
9. 命令之后的状态变化能从 NUC 回流到 RK3588

---

## 8. 推荐验收记录项

建议 NUC 侧在验收时保留以下证据：

- `ping` 结果
- `POST /api/system/mode/switch` 结果
- `POST /api/internal/nuc/state` 结果
- `GET /api/state/latest` 返回样例
- `WS /ws/state` 关键输出
- NUC mission 服务收到的 3 条命令日志
- `GET /api/commands/logs` 返回样例
- 如可行，补一张 Dashboard 页面截图

---

## 9. 不作为失败项的问题

如果只是以下问题，本轮一般不判失败：

- 页面样式普通
- 环境传感器先传 `null`
- 告警列表暂时没有新增 NUC 告警
- 任务状态仍来自 NUC 占位任务管理模块
- 发送频率先固定在 `1 Hz`

---

## 10. 直接判失败的情况

出现以下任一情况，建议退回修正：

- NUC 只能在 `127.0.0.1` 下自测，换成网口地址就不通
- RK3588 已切到 `real`，但 NUC 上送仍长期 `accepted=false`
- NUC mission 接口根本收不到 RK3588 命令
- 命令接口返回成功，但 `GET /api/state/latest` 完全不反映结果
- Dashboard 不能跟着 real 状态变化
- `GET /api/commands/logs` 没有新增 real-mode 命令日志
- 网络抖动时 NUC 发送器或 mission 服务直接崩掉

---

## 11. 本轮结束后的下一步

本轮通过后，说明已经完成：

- 有线网口实机通信
- 状态上送
- 任务桥接
- 日志记录
- 前后端基础闭环

下一步就可以进入：

- Round 4：离线态、错误态、重连、mode 管理细化
- 真实导航栈 / 真实任务管理模块接入
- 更完整的告警和任务生命周期回流
