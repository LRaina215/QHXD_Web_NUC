# NUC_DO2_1.md

## 1. 文档目的

本文件记录我在 **2026-04-14（Asia/Shanghai）** 对 Round 3 的一次独立复验结果，并给出当前阶段的正式结论。

这次复验的目标不是复述 NUC 侧报告，而是：

- 我自己从 RK3588 侧重新验证
- 判断 Round 3 到底是“代码没实现”还是“部署配置没对”
- 给出下一步正式验收建议

---

## 2. 最终结论

本次独立复验后的结论是：

### 2.1 当前代码能力结论

**Round 3 的 mission bridge 代码能力已具备，功能上可以通过。**

也就是说：

- RK3588 mission public API 已能转发到 NUC mission 服务
- NUC 返回的命令结果已能写入 SQLite
- 命令结果已能刷新共享状态
- 命令结果已能通过 `GET /api/state/latest` 和 `WS /ws/state` 对外体现
- 前端页面已能触发命令并观察结果

### 2.2 当前 8000 运行实例结论

**NUC 在 [NUC_DONE2.md](/home/robomaster/QHXD/NUC_DONE2.md) 中测到的失败，是 RK3588 当前运行实例的部署配置问题，不是 Round 3 功能实现本身失败。**

更具体地说：

- 当前 `8000` 上正在运行的 RK3588 后端实例，**没有带 `NUC_BASE_URL` / `NUC_MISSION_PATH` 启动**
- 因此它仍然使用默认 bridge 目标：
  - `NUC_BASE_URL = http://127.0.0.1:9000`
  - `NUC_MISSION_PATH = /api/internal/rk3588/mission`
- 在这种情况下，向真实 NUC `192.168.10.3:8090` 转发自然会得到：
  - `Connection refused`

所以，当前更准确的判定是：

- **Round 3 功能实现：通过**
- **当前正式运行实例配置：未完成切换**
- **整轮是否对外宣布通过：取决于是否把 RK3588 正式运行实例改为正确的 NUC 地址后再补一次短验收**

---

## 3. 本次实际复验步骤

### 3.1 直接从 RK3588 访问 NUC mission 服务

我先直接从 RK3588 执行：

```bash
curl --noproxy '*' -sS -m 3 \
  http://192.168.10.3:8090/api/internal/rk3588/mission \
  -H 'Content-Type: application/json' \
  -d '{"command":"go_to_waypoint","source":"rk3588-connect-test","requested_by":"codex","payload":{"waypoint_id":"wp-rk3588-direct"}}'
```

结果：

- 返回 `accepted=true`
- `task_status.task_type=go_to_waypoint`
- `current_goal=wp-rk3588-direct`

结论：

- **RK3588 主机到 NUC mission 服务的有线访问是通的**

这一步非常关键，因为它说明：

- 不是 NUC mission 服务没起
- 不是网口链路不通
- RK3588 主机本身就能打到 `192.168.10.3:8090`

---

### 3.2 检查当前 `8000` 实例配置

我检查了当前正在监听 `8000` 的进程：

```bash
ps -fp 16501,16503
```

确认当前运行实例是：

```text
python3 -m uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

然后检查该进程环境变量：

```bash
tr '\0' '\n' < /proc/16501/environ | rg '^NUC_|^VITE_'
tr '\0' '\n' < /proc/16503/environ | rg '^NUC_|^VITE_'
```

结果：

- **没有任何 `NUC_*` 相关环境变量**

结论：

- NUC 在 [NUC_DONE2.md](/home/robomaster/QHXD/NUC_DONE2.md) 中测到的 `Connection refused`，与这个现象完全一致
- 当前 `8000` 实例 bridge 没有指到真实 NUC 地址

---

### 3.3 启动一份带正确 NUC 地址的独立后端实例

为了不影响当前 `8000` 服务，我另外启动了一份独立后端：

```bash
cd /home/robomaster/QHXD/backend
NUC_BASE_URL=http://192.168.10.3:8090 \
NUC_MISSION_PATH=/api/internal/rk3588/mission \
NUC_TIMEOUT_SECONDS=2.0 \
python3 -m uvicorn app.main:app --host 127.0.0.1 --port 8003
```

然后切到 `real` 模式：

```bash
curl -s -X POST http://127.0.0.1:8003/api/system/mode/switch \
  -H 'Content-Type: application/json' \
  -d '{"mode":"real","source":"round3-retest","requested_by":"codex"}'
```

返回结果：

- `accepted=true`
- `system_mode.mode=real`

结论：

- **独立复验环境已准备完成**

---

## 4. Round 3 关键复验结果

### 4.1 Public mission API 三条命令全部转发成功

我在 `8003` 上实际测试了：

- `POST /api/mission/go_to_waypoint`
- `POST /api/mission/pause`
- `POST /api/mission/return_home`

对应结果全部为：

- `success=true`
- `accepted=true`

关键返回：

#### `go_to_waypoint`

- `command=go_to_waypoint`
- `task_status.task_type=go_to_waypoint`
- `task_status.state=running`
- `task_status.source=nuc`

#### `pause`

- `command=pause`
- `task_status.state=paused`
- `task_status.source=nuc`

#### `return_home`

- `command=return_home`
- `task_status.task_type=return_home`
- `task_status.state=running`
- `task_status.source=nuc`

结论：

- **Round 3 至少三条 bridge 命令要求已满足**

---

### 4.2 命令日志已正确入库

我随后查询：

```bash
curl -s http://127.0.0.1:8003/api/commands/logs
```

看到新增日志项：

- `go_to_waypoint accepted=true`
- `pause accepted=true`
- `return_home accepted=true`

并且 payload 中能看到：

- `forwarded_command=go_to_waypoint`
- `forwarded_command=pause_task`
- `forwarded_command=return_home`

结论：

- **SQLite 命令日志链路正常**

---

### 4.3 命令后的状态变化已通过现有 API 回流

我做了一轮顺序复验：

1. 发 `go_to_waypoint`
2. 查 `GET /api/state/latest`
3. 发 `pause`
4. 查 `GET /api/state/latest`
5. 发 `return_home`
6. 查 `GET /api/state/latest`

结果如下：

#### `go_to_waypoint` 后

- `nav_status.state=running`
- `nav_status.current_goal=wp-round3-seq`
- `task_status.task_type=go_to_waypoint`
- `task_status.state=running`
- `task_status.source=nuc`

#### `pause` 后

- `nav_status.state=paused`
- `nav_status.current_goal=wp-round3-seq`
- `task_status.state=paused`
- `task_status.source=nuc`

#### `return_home` 后

- `nav_status.state=running`
- `nav_status.current_goal=home`
- `task_status.task_type=return_home`
- `task_status.state=running`
- `task_status.source=nuc`

结论：

- **命令结果已经通过共享状态反映到 `/api/state/latest`**

---

### 4.4 WebSocket 已能实时推送命令结果

我连接了：

```text
ws://127.0.0.1:8003/ws/state
```

然后再发一条：

- `POST /api/mission/pause`

结果看到：

- 第一帧任务状态：`running`
- 第二帧任务状态：`paused`
- 第二帧导航状态：`paused`
- 第二帧 `task_status.source=nuc`

结论：

- **命令结果已经通过现有 WebSocket 机制实时推送**

---

### 4.5 前端页面已能触发命令并观察结果

我又单独起了一份前端验证实例：

```bash
cd /home/robomaster/QHXD/frontend
VITE_BACKEND_URL=http://127.0.0.1:8003 npm run dev -- --host 127.0.0.1 --port 5174
```

然后用无头 Chromium 打开页面并实际点击按钮。

观察结果：

#### 初始页面

- 显示 `return_home / paused`
- 当前目标 `home`
- 实时流已连接

#### 点击“前往目标点”后

- 页面变为 `go_to_waypoint / running`
- 当前目标变为 `wp-round3-ui`

#### 点击“暂停”后

- 页面变为 `go_to_waypoint / paused`
- 当前目标保持 `wp-round3-ui`

结论：

- **前端页面已能触发命令并观察状态变化**

---

## 5. 对 [NUC_DONE2.md](/home/robomaster/QHXD/NUC_DONE2.md) 的判断

我认为 NUC 侧在 [NUC_DONE2.md](/home/robomaster/QHXD/NUC_DONE2.md) 中给出的判断是：

- **对当时那份 `8000` 运行实例来说，判断是正确的**
- **但它不能推出“Round 3 功能实现失败”**

更准确的解释应该是：

1. NUC 已经把状态上送和 mission 服务都做通
2. RK3588 代码里的 bridge 逻辑也已经具备
3. 当时失败的直接原因，是 RK3588 正在运行的 `8000` 实例没有用真实 NUC 地址启动
4. 一旦把 `NUC_BASE_URL` 和 `NUC_MISSION_PATH` 配对正确，Round 3 闭环就能通过

---

## 6. 当前正式结论

我给出的正式结论如下：

```text
Round 3 的 mission bridge 代码实现已经通过独立复验。

本次我从 RK3588 侧实际验证了：
1. RK3588 可直连 NUC mission 服务 http://192.168.10.3:8090/api/internal/rk3588/mission
2. 在为后端显式配置
   NUC_BASE_URL=http://192.168.10.3:8090
   NUC_MISSION_PATH=/api/internal/rk3588/mission
   后，
   go_to_waypoint / pause / return_home 三条 public mission API
   均可成功转发到 NUC
3. 命令日志成功写入 SQLite
4. 命令结果成功反映到 /api/state/latest
5. WebSocket 成功推送命令后的 real 状态变化
6. 前端页面已能触发命令并观察结果

因此，Round 3 当前的核心问题不是功能缺失，而是当前 8000 正式运行实例的 NUC bridge 启动配置未对齐。
```

---

## 7. 下一步建议

当前建议不要再继续怀疑 NUC mission 服务本身，而是直接做下面这件事：

### 建议动作

把 RK3588 正式运行实例改成带正确 bridge 配置启动，例如：

```bash
export NUC_BASE_URL=http://192.168.10.3:8090
export NUC_MISSION_PATH=/api/internal/rk3588/mission
export NUC_TIMEOUT_SECONDS=2.0

cd /home/robomaster/QHXD/backend
python3 -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

### 然后补一轮短验收

只需要重新跑这 4 步：

1. 切 `real`
2. 发 `POST /api/mission/go_to_waypoint`
3. 查 `GET /api/commands/logs`
4. 看 Dashboard 或 `WS /ws/state`

如果这轮通过，就可以把 Round 3 对外结论改成：

- **正式通过**

---

## 8. 当前建议对外口径

建议当前对外统一写成：

```text
Round 3 已完成独立复验。

结论是：
功能实现已通过，当前阻塞仅剩 RK3588 正式运行实例的 NUC bridge 启动配置未切到真实 NUC 地址。

在使用
NUC_BASE_URL=http://192.168.10.3:8090
NUC_MISSION_PATH=/api/internal/rk3588/mission
重新启动后端后，
Round 3 的 public mission bridge 已可正常闭环。
```
