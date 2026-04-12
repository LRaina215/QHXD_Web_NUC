# NUC_DO.md

## 1. 文档目的

本文件用于明确 **Phase 2 当前阶段** 里，NUC 侧需要先实现什么、暂时不用实现什么、如何向 RK3588 上送状态，以及联调时如何验收。

当前 RK3588 已完成的能力是：

- 提供 NUC 状态接入入口：`POST /api/internal/nuc/state`
- 提供模式切换入口：`POST /api/system/mode/switch`
- 将 real 状态写入共享 `state_store`
- 通过 `GET /api/state/latest`、`GET /api/tasks/current`、`GET /api/alerts` 对外输出
- 通过 `WS /ws/state` 向前端实时推送状态

所以，**NUC 现在可以先实现“真实状态上送”**，不需要等任务桥接完成后再开始。

---

## 2. 当前阶段边界

### 2.1 NUC 现在要做的

- 从 NUC 当前导航/任务系统中整理出最新状态
- 按 RK3588 已冻结的 JSON 契约组包
- 周期性向 RK3588 发送 `POST /api/internal/nuc/state`
- 在发送失败时做最小重试和日志打印
- 在有告警时把告警一起上送

### 2.2 NUC 现在不用做的

- 不用实现 RK3588 -> NUC 的任务接收接口
- 不用实现 RT-Thread 直连改造
- 不用实现视频、语音、视觉检测扩展
- 不用实现复杂认证、消息队列或高可用机制

### 2.3 一个重要前提

RK3588 必须先切到 `real` 模式，否则 NUC 上送的数据会被后端忽略。

切换接口：

```http
POST /api/system/mode/switch
Content-Type: application/json
```

示例请求：

```json
{
  "mode": "real",
  "source": "integration-test",
  "requested_by": "operator"
}
```

如果 RK3588 仍处于 `mock` 模式，`POST /api/internal/nuc/state` 会返回：

```json
{
  "success": true,
  "data": {
    "accepted": false,
    "system_mode": {
      "mode": "mock",
      "updated_at": "2026-04-12T15:00:00Z"
    },
    "state_updated": false,
    "received_at": "2026-04-12T15:00:01Z",
    "detail": "当前系统处于 mock 模式，已忽略 NUC 实时状态输入。"
  }
}
```

---

## 3. NUC 侧建议实现方案

### 3.1 最小模块划分

建议 NUC 侧最少拆成这 3 个逻辑块：

1. `state_collector`
   - 从 NUC 内部导航、任务、设备状态来源拿到最新值
   - 整理成 RK3588 需要的统一结构

2. `state_mapper`
   - 把 NUC 内部字段映射为 RK3588 Phase 2 契约字段
   - 负责枚举值归一化、时间戳格式化、空值处理

3. `rk3588_sender`
   - 周期性 POST 到 RK3588
   - 记录发送成功/失败
   - 处理超时、简单重试、网络异常

### 3.2 建议发送频率

- 正常状态：`1 Hz`
- 如果内部状态变化频繁，也可以升到 `2 Hz`
- 当前阶段不建议一开始就上到很高频率

Phase 2 当前目标是联调打通，不是极限性能压测。

### 3.3 建议最小容错

- HTTP 超时：`2s`
- 失败后简单重试：`1~3` 次
- 连续失败时打印本地日志
- 不因为一次发送失败阻塞 NUC 主业务线程

### 3.4 建议时间格式

统一使用 UTC ISO 8601，例如：

```text
2026-04-12T15:20:30Z
```

---

## 4. NUC 需要上送的最小字段

当前 RK3588 `POST /api/internal/nuc/state` 需要这些字段：

- `robot_pose`
- `nav_status`
- `task_status`
- `device_status`
- `env_sensor`
- `alerts`
- `updated_at`

### 4.1 字段说明

#### `robot_pose`

- `x`: 机器人当前 x 坐标，单位 m
- `y`: 机器人当前 y 坐标，单位 m
- `yaw`: 朝向角，单位 rad
- `frame_id`: 坐标系，建议先固定为 `map`
- `timestamp`: 位姿采样时间

#### `nav_status`

- `mode`: `auto` 或 `manual`
- `state`: `idle` / `running` / `paused` / `completed` / `failed` / `offline`
- `current_goal`: 当前目标点 ID，没有就传 `null`
- `remaining_distance`: 剩余距离，没有就传 `null`

#### `task_status`

- `task_id`: 当前任务 ID
- `task_type`: `placeholder` / `go_to_waypoint` / `start_patrol` / `return_home`
- `state`: `idle` / `pending` / `running` / `paused` / `completed` / `failed` / `cancelled`
- `progress`: `0-100`
- `source`: 建议当前先固定传 `nuc`

#### `device_status`

- `battery_percent`: `0-100`
- `emergency_stop`: 急停状态
- `fault_code`: 没有故障时传 `null`
- `online`: NUC 所认为的设备在线状态

#### `env_sensor`

- `temperature_c`: 温度
- `humidity_percent`: 湿度
- `status`: `mock` / `nominal` / `warning` / `fault` / `offline`

NUC 如果当前没有真实环境传感器，也可以先固定传：

- `temperature_c: null`
- `humidity_percent: null`
- `status: "offline"`

#### `alerts`

是一个数组，可以为空列表 `[]`。

每条告警结构：

- `alert_id`
- `level`: `info` / `warning` / `error` / `critical`
- `message`
- `source`
- `timestamp`
- `acknowledged`

#### `updated_at`

表示本次整包状态的更新时间。

---

## 5. 最小 JSON 示例

这是 NUC 侧现在可以直接对接的最小有效 JSON：

```json
{
  "robot_pose": {
    "x": 12.3,
    "y": 4.5,
    "yaw": 1.2,
    "frame_id": "map",
    "timestamp": "2026-04-12T15:20:30Z"
  },
  "nav_status": {
    "mode": "auto",
    "state": "running",
    "current_goal": "wp-real-001",
    "remaining_distance": 2.5
  },
  "task_status": {
    "task_id": "task-real-001",
    "task_type": "go_to_waypoint",
    "state": "running",
    "progress": 35,
    "source": "nuc"
  },
  "device_status": {
    "battery_percent": 77,
    "emergency_stop": false,
    "fault_code": null,
    "online": true
  },
  "env_sensor": {
    "temperature_c": 26.5,
    "humidity_percent": 48.2,
    "status": "nominal"
  },
  "alerts": [
    {
      "alert_id": "alert-real-001",
      "level": "warning",
      "message": "前方通道临时拥堵，导航速度已降低。",
      "source": "nuc-nav",
      "timestamp": "2026-04-12T15:20:30Z",
      "acknowledged": false
    }
  ],
  "updated_at": "2026-04-12T15:20:30Z"
}
```

最小无告警版本也可以：

```json
{
  "robot_pose": {
    "x": 0.0,
    "y": 0.0,
    "yaw": 0.0,
    "frame_id": "map",
    "timestamp": "2026-04-12T15:20:30Z"
  },
  "nav_status": {
    "mode": "auto",
    "state": "idle",
    "current_goal": null,
    "remaining_distance": null
  },
  "task_status": {
    "task_id": "task-idle",
    "task_type": "placeholder",
    "state": "idle",
    "progress": 0,
    "source": "nuc"
  },
  "device_status": {
    "battery_percent": 100,
    "emergency_stop": false,
    "fault_code": null,
    "online": true
  },
  "env_sensor": {
    "temperature_c": null,
    "humidity_percent": null,
    "status": "offline"
  },
  "alerts": [],
  "updated_at": "2026-04-12T15:20:30Z"
}
```

---

## 6. Python 发送示例

下面给一个 **零额外依赖** 的 Python 示例，使用标准库 `urllib.request` 即可发送。

### 6.1 单次发送示例

```python
import json
from urllib import request, error

RK3588_URL = "http://127.0.0.1:8000/api/internal/nuc/state"

payload = {
    "robot_pose": {
        "x": 12.3,
        "y": 4.5,
        "yaw": 1.2,
        "frame_id": "map",
        "timestamp": "2026-04-12T15:20:30Z",
    },
    "nav_status": {
        "mode": "auto",
        "state": "running",
        "current_goal": "wp-real-001",
        "remaining_distance": 2.5,
    },
    "task_status": {
        "task_id": "task-real-001",
        "task_type": "go_to_waypoint",
        "state": "running",
        "progress": 35,
        "source": "nuc",
    },
    "device_status": {
        "battery_percent": 77,
        "emergency_stop": False,
        "fault_code": None,
        "online": True,
    },
    "env_sensor": {
        "temperature_c": 26.5,
        "humidity_percent": 48.2,
        "status": "nominal",
    },
    "alerts": [],
    "updated_at": "2026-04-12T15:20:30Z",
}

data = json.dumps(payload).encode("utf-8")

req = request.Request(
    RK3588_URL,
    data=data,
    headers={"Content-Type": "application/json"},
    method="POST",
)

try:
    with request.urlopen(req, timeout=2) as resp:
        body = resp.read().decode("utf-8")
        print("status:", resp.status)
        print("body:", body)
except error.HTTPError as exc:
    print("http error:", exc.code, exc.read().decode("utf-8", errors="ignore"))
except error.URLError as exc:
    print("url error:", exc)
```

### 6.2 周期发送示例

```python
import json
import math
import time
from datetime import datetime, timezone
from urllib import request, error

RK3588_URL = "http://127.0.0.1:8000/api/internal/nuc/state"


def now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def build_payload(seq: int) -> dict:
    return {
        "robot_pose": {
            "x": round(2.0 + math.cos(seq / 5.0), 3),
            "y": round(1.0 + math.sin(seq / 5.0), 3),
            "yaw": round((seq / 10.0) % 6.283, 3),
            "frame_id": "map",
            "timestamp": now_iso(),
        },
        "nav_status": {
            "mode": "auto",
            "state": "running",
            "current_goal": f"wp-{seq % 3}",
            "remaining_distance": round(max(0.0, 5.0 - seq * 0.2), 2),
        },
        "task_status": {
            "task_id": "task-live-001",
            "task_type": "go_to_waypoint",
            "state": "running" if seq < 20 else "completed",
            "progress": min(seq * 5, 100),
            "source": "nuc",
        },
        "device_status": {
            "battery_percent": max(30, 90 - seq),
            "emergency_stop": False,
            "fault_code": None,
            "online": True,
        },
        "env_sensor": {
            "temperature_c": 25.0,
            "humidity_percent": 50.0,
            "status": "nominal",
        },
        "alerts": [],
        "updated_at": now_iso(),
    }


def post_json(payload: dict) -> None:
    data = json.dumps(payload).encode("utf-8")
    req = request.Request(
        RK3588_URL,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with request.urlopen(req, timeout=2) as resp:
        print(resp.status, resp.read().decode("utf-8"))


def main() -> None:
    seq = 0
    while True:
        payload = build_payload(seq)
        try:
            post_json(payload)
        except error.HTTPError as exc:
            print("http error:", exc.code, exc.read().decode("utf-8", errors="ignore"))
        except error.URLError as exc:
            print("url error:", exc)
        except Exception as exc:
            print("unexpected error:", exc)

        seq += 1
        time.sleep(1.0)


if __name__ == "__main__":
    main()
```

---

## 7. 建议的 NUC 开发任务清单

NUC 侧建议按下面顺序做，最稳。

### T1：先能发固定 JSON

目标：

- 手工或脚本发一包固定 JSON 给 RK3588
- 确认 RK3588 返回 `accepted: true`

完成标准：

- `POST /api/internal/nuc/state` 返回成功
- RK3588 的 `GET /api/state/latest` 中能看到这包数据

### T2：接入真实位姿 / 任务 / 导航状态

目标：

- 不再发固定值，而是从 NUC 当前真实模块取值

完成标准：

- `robot_pose` 会变化
- `nav_status.current_goal` 会跟任务变化
- `task_status.state` 会随任务变化

### T3：做周期上送

目标：

- 每 `1s` 上送一次最新状态

完成标准：

- RK3588 Dashboard 能持续刷新
- RK3588 WebSocket 能收到变化中的状态

### T4：补最小异常处理

目标：

- 网络断开、RK3588 未启动、超时等情况不让 NUC 程序崩掉

完成标准：

- 失败时有日志
- 重试后可恢复

### T5：补告警上送

目标：

- 把 NUC 当前已知的任务/导航告警按 `alerts` 一并上送

完成标准：

- RK3588 `GET /api/alerts` 中能看到 NUC 告警

---

## 8. 联调验收标准

### 8.1 最小通过标准

满足以下全部条件即可认为 NUC 状态上送链路通过：

1. RK3588 已切到 `real` 模式
2. NUC 能成功调用 `POST /api/internal/nuc/state`
3. RK3588 返回 `accepted: true`
4. `GET /api/state/latest` 中能看到 NUC 最新状态
5. Dashboard 页面状态会更新
6. `WS /ws/state` 能收到变化中的 real 状态

### 8.2 推荐验收步骤

#### 步骤 1：启动 RK3588 后端

```bash
cd /home/robomaster/QHXD/backend
python3 -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

#### 步骤 2：切到 real 模式

```bash
curl -X POST http://127.0.0.1:8000/api/system/mode/switch \
  -H 'Content-Type: application/json' \
  -d '{"mode":"real","source":"integration-test","requested_by":"operator"}'
```

期待结果：

- 返回 JSON 中 `system_mode.mode` 为 `real`

#### 步骤 3：在 NUC 上运行发送脚本

期待结果：

- `POST /api/internal/nuc/state` 返回 `accepted: true`

#### 步骤 4：检查 RK3588 最新状态

```bash
curl http://127.0.0.1:8000/api/state/latest
```

期待结果：

- `task_status.source` 为 `nuc`
- `system_mode.mode` 为 `real`
- `current_goal`、`battery_percent`、`robot_pose` 等字段与 NUC 发送值一致

#### 步骤 5：检查前端页面

期待结果：

- 页面显示为 real 模式
- 当前任务、目标点、电量、告警等会变化

#### 步骤 6：检查 WebSocket

可用浏览器打开 Dashboard，也可单独写一个 WS 客户端观察。

期待结果：

- 每次 NUC 上送新状态后，WebSocket 客户端能收到新一条 `robot_state`

---

## 9. 当前阶段不作为失败项的问题

如果只是下面这些问题，当前阶段一般不算失败：

- 页面样式还比较基础
- 环境传感器暂时传 `null`
- 告警内容还比较简单
- 发送频率先固定为 `1 Hz`
- 状态值暂时来自 NUC 内部占位模块，而不是最终完整导航栈

---

## 10. 当前阶段算失败的情况

出现以下任一情况，建议退回修正：

- RK3588 明明切到 `real` 模式，但 NUC 上送仍长期无法被接收
- JSON 字段名与冻结契约不一致，导致接口校验失败
- 状态能 POST 成功，但 `GET /api/state/latest` 看不到更新
- Dashboard 只能看到静态旧值，不能跟随 NUC 更新
- WebSocket 不反映 real 状态变化
- NUC 发送程序一遇到网络波动就崩溃退出

---

## 11. 下一步接口边界说明

当前这份 `NUC_DO.md` 只覆盖：

- **NUC -> RK3588 状态上送**

还不覆盖：

- **RK3588 -> NUC 任务桥接**

也就是说，你现在可以先把“状态回传”独立做完；等 RK3588 下一轮把任务桥接接口补齐后，再继续实现：

- NUC 接收任务命令
- NUC 返回命令受理结果
- NUC 用真实状态反映任务执行进度

这样并行推进是最省时间的。
