# FORMAL_ACCEPTANCE

## 文档目的

本文件用于补齐 NUC 侧本轮 `NUC -> RK3588` 状态上发的正式验收证据。

它和 [DONE.md](/home/robomaster/QHXD_NUC/DONE.md) 的分工不同：

- `DONE.md`
  - 记录已完成的开发内容和需要同步给 RK3588 端的信息
- `FORMAL_ACCEPTANCE.md`
  - 记录正式验收时要执行的命令、要收集的证据、通过标准和交付材料
- 本轮实际执行结果见 [ROUND3验收.md](/home/robomaster/QHXD_NUC/ROUND3验收.md#L1)

## 本轮正式验收目标

按 [NUC_DO.md](/home/robomaster/QHXD_NUC/NUC_DO.md) 当前阶段要求，至少补齐以下 4 类实物证据：

1. `POST /api/internal/nuc/state` 返回 `accepted: true`
2. `GET /api/state/latest` 反映 NUC 刚刚上送的数据
3. `WS /ws/state` 能收到变化中的 real 状态
4. Dashboard 页面能看到 real 状态变化

## 联调固定信息

本次网口联调固定地址如下：

- NUC 有线网口 `enp89s0`: 保留激光雷达地址 `192.168.1.50/24`，并额外挂 `192.168.10.3/24`
- RK3588 有线网口: `192.168.10.2/24`

NUC 发送程序正式联调时应使用：

```text
http://192.168.10.2:8000
```

如果 NUC 环境里设置了 `http_proxy`、`https_proxy`、`all_proxy`，命令行验证时一律建议加：

```bash
--noproxy '*'
```

## 验收前准备

### 1. RK3588 侧准备

确认 RK3588 后端已启动，并监听 `0.0.0.0:8000`。

建议保留 RK3588 后端启动终端，用于截图或查看联调日志。

### 2. NUC 侧准备

进入目录：

```bash
cd /home/robomaster/QHXD_NUC
```

确认程序仍可运行：

```bash
python3 -m unittest discover -s tests
python3 -m nuc_state_uploader.main send-once --dry-run --print-payload
```

### 3. 配置正式联调地址

将 [default_config.json](/home/robomaster/QHXD_NUC/configs/default_config.json) 中的：

```json
{
  "rk3588": {
    "base_url": "http://127.0.0.1:8000"
  }
}
```

改为：

```json
{
  "rk3588": {
    "base_url": "http://192.168.10.2:8000"
  }
}
```

## 正式验收步骤

### 步骤 1. 检查 RK3588 服务可访问

在 NUC 上执行：

```bash
curl --noproxy '*' -v http://192.168.10.2:8000/docs
curl --noproxy '*' -v http://192.168.10.2:8000/openapi.json
```

通过标准：

- 两条命令都返回 `HTTP/1.1 200 OK`

建议留证：

- 保存终端截图
- 或将输出重定向为文本文件

示例：

```bash
curl --noproxy '*' -v http://192.168.10.2:8000/docs > evidence/01_docs.txt 2>&1
curl --noproxy '*' -v http://192.168.10.2:8000/openapi.json > evidence/02_openapi.txt 2>&1
```

### 步骤 2. 切换 RK3588 到 real 模式

在 NUC 上执行：

```bash
curl --noproxy '*' -X POST http://192.168.10.2:8000/api/system/mode/switch \
  -H 'Content-Type: application/json' \
  -d '{"mode":"real","source":"integration-test","requested_by":"operator"}'
```

通过标准：

- 返回 JSON 中 `success=true`
- `data.system_mode.mode=real`

建议留证：

```bash
curl --noproxy '*' -X POST http://192.168.10.2:8000/api/system/mode/switch \
  -H 'Content-Type: application/json' \
  -d '{"mode":"real","source":"integration-test","requested_by":"operator"}' \
  > evidence/03_switch_mode_real.json
```

### 步骤 3. NUC 单次真实上发

在 NUC 上执行：

```bash
python3 -m nuc_state_uploader.main send-once --print-payload
```

通过标准：

- 终端中能看到最终上送 JSON
- 日志中无异常退出
- 如果程序打印了返回状态，应看到发送成功

建议留证：

```bash
python3 -m nuc_state_uploader.main send-once --print-payload \
  > evidence/04_send_once.log 2>&1
```

### 步骤 4. 验证 `POST /api/internal/nuc/state` 已被接受

如果上一步程序日志里已经直接打印了返回体，保留该日志即可。

如果需要更强验证，可直接使用文件模式固定一包 JSON，再手工 POST 一次：

```bash
curl --noproxy '*' -X POST http://192.168.10.2:8000/api/internal/nuc/state \
  -H 'Content-Type: application/json' \
  --data @examples/internal_state.sample.json
```

通过标准：

- 返回 JSON 中 `success=true`
- `data.accepted=true`
- `data.state_updated=true`
- `data.system_mode.mode=real`

建议留证：

```bash
curl --noproxy '*' -X POST http://192.168.10.2:8000/api/internal/nuc/state \
  -H 'Content-Type: application/json' \
  --data @examples/internal_state.sample.json \
  > evidence/05_post_nuc_state.json
```

### 步骤 5. 验证 `GET /api/state/latest`

在 NUC 上执行：

```bash
curl --noproxy '*' http://192.168.10.2:8000/api/state/latest
```

通过标准：

- 返回 JSON 中 `system_mode.mode=real`
- `task_status.source=nuc`
- `robot_pose`、`current_goal`、`battery_percent` 等字段与刚才发送值一致

建议留证：

```bash
curl --noproxy '*' http://192.168.10.2:8000/api/state/latest \
  > evidence/06_state_latest.json
```

### 步骤 6. 验证 `GET /api/alerts`

如果本次发送包含告警，再执行：

```bash
curl --noproxy '*' http://192.168.10.2:8000/api/alerts
```

通过标准：

- 返回结果中能看到 NUC 上送的 `alerts`

建议留证：

```bash
curl --noproxy '*' http://192.168.10.2:8000/api/alerts \
  > evidence/07_alerts.json
```

### 步骤 7. 验证 WebSocket

目标是证明 `WS /ws/state` 能收到 real 状态变化。

推荐两种留证方式，任选一种：

方式 A：打开 RK3588 Dashboard 页面并截图浏览器控制台或实时状态变化  
方式 B：单独写一个最小 WebSocket 客户端接收消息并保存日志

最小 Python WebSocket 验证脚本示例：

```python
import asyncio
import websockets


async def main():
    uri = "ws://192.168.10.2:8000/ws/state"
    async with websockets.connect(uri) as ws:
        for _ in range(3):
            message = await ws.recv()
            print(message)


asyncio.run(main())
```

通过标准：

- 在 NUC 周期发送时，WebSocket 客户端能收到新消息
- 消息内容反映 `real` 模式下的最新状态

建议留证：

- `evidence/08_ws_state.log`
- 或浏览器截图

### 步骤 8. 验证 Dashboard

打开 RK3588 Dashboard 页面，观察：

- 页面已显示 `real` 模式
- 当前任务、目标点、电量、位姿等字段会更新
- 有告警时页面能反映告警变化

通过标准：

- 页面状态与 `GET /api/state/latest` 一致
- 页面会随着 NUC 上发变化而刷新

建议留证：

- `evidence/09_dashboard.png`
- 如果页面有多个关键区域，建议保存 2~3 张截图

## 推荐证据目录结构

建议在 NUC 目录下新建：

```text
evidence/
```

建议最终至少提交这些文件：

- `evidence/01_docs.txt`
- `evidence/02_openapi.txt`
- `evidence/03_switch_mode_real.json`
- `evidence/04_send_once.log`
- `evidence/05_post_nuc_state.json`
- `evidence/06_state_latest.json`
- `evidence/07_alerts.json`
- `evidence/08_ws_state.log`
- `evidence/09_dashboard.png`

## 最小正式通过标准

如果以下条件全部满足，则可以认为本轮 NUC 状态上发“正式验收通过”：

1. RK3588 已切到 `real` 模式
2. `POST /api/internal/nuc/state` 返回 `accepted=true`
3. `GET /api/state/latest` 中能看到 NUC 刚上送的数据
4. WebSocket 能收到 real 状态变化
5. Dashboard 页面能反映该变化

## 本轮执行结果

本轮已经完成除 Dashboard 截图外的关键验收项实测。

实际通过结果见：

- [ROUND3验收.md](/home/robomaster/QHXD_NUC/ROUND3验收.md#L1)

## 如果验收失败，优先排查这些点

### 1. `accepted=false`

优先检查：

- RK3588 是否仍处于 `mock` 模式
- `mode/switch` 是否真的成功

### 2. `422 Validation Error`

优先检查：

- JSON 字段名是否和 OpenAPI 契约一致
- 枚举值是否超出允许范围
- 时间字段是否为 ISO 8601 UTC 格式

### 3. `GET /api/state/latest` 没变化

优先检查：

- `POST /api/internal/nuc/state` 是否真的返回 `state_updated=true`
- 是否发到了错误地址
- 是否仍走了代理而不是网口地址

### 4. WebSocket 或 Dashboard 不更新

优先检查：

- RK3588 后端是否已把最新状态写入 `state_store`
- 前端连接的是否为同一个 RK3588 实例
- 页面是否仍缓存旧连接

## 建议对外同步的正式表述

如果证据收集完成，可以对外统一表述为：

```text
NUC 侧状态上发工程已完成，并已在有线网口地址
RK3588 192.168.10.2 <-> NUC 192.168.10.3
的联调环境下完成正式验收。

已验证：
1. RK3588 已切换至 real 模式
2. POST /api/internal/nuc/state 返回 accepted=true
3. GET /api/state/latest 正确反映 NUC 上送数据
4. WS /ws/state 能收到 real 状态变化
5. Dashboard 页面能同步显示更新结果
```
