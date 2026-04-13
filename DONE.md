# DONE

## 本次已完成

已完成 NUC 侧 `NUC -> RK3588` 最小网口状态上发实现，覆盖 `NUC_DO.md` 当前阶段要求。

完成内容如下：

- 新建可运行 Python 工程，入口为 `python3 -m nuc_state_uploader.main`
- 按要求拆分为 `state_collector / state_mapper / rk3588_sender`
- 实现 `mock` 状态采集器，支持本地无网线时先 dry-run、自测、看组包结果
- 实现 `file` 状态采集器，支持手工编辑 JSON 后反复发送联调
- 实现 RK3588 契约字段映射：
  - `robot_pose`
  - `nav_status`
  - `task_status`
  - `device_status`
  - `env_sensor`
  - `alerts`
  - `updated_at`
- 实现 HTTP POST 上发、超时、最小重试、日志输出
- 实现 `switch-mode` 命令，支持切换 RK3588 到 `real` 模式
- 处理 NUC 环境代理变量干扰问题
  - Python 发送器内部已禁用系统代理，避免局域网请求被 `http_proxy/socks5` 误劫持
- 补充 `README.md`
  - 记录启动流程
  - 记录使用方法
  - 记录调试方法
  - 记录后续如何接入真实导航状态
- 补充自动化测试，验证字段映射和 `file` 采集链路

## 已验证结果

本次已验证以下内容：

- `python3 -m unittest discover -s tests` 通过
- `python3 -m nuc_state_uploader.main send-once --dry-run --print-payload` 可正常输出 RK3588 所需 JSON
- 已完成 Round3 纯有线联调验收，结果见 [ROUND3验收.md](/home/robomaster/QHXD_NUC/ROUND3验收.md#L1)
- Round3 已实测通过：
  - `GET /docs`
  - `POST /api/system/mode/switch`
  - `POST /api/internal/nuc/state`
  - `GET /api/state/latest`
  - `WS /ws/state`

## 当前联调结论

应用层接口本身已经打通。

本次 Round3 联调使用有线网口固定 IPv4 方案：

- NUC 有线网口 `enp89s0`: 保留激光雷达地址 `192.168.1.50/24`，并额外挂载 `192.168.10.3/24`
- RK3588 有线网口: `192.168.10.2/24`

目前要区分 3 类地址：

- `127.0.0.1`
  - 只能表示“当前执行命令这台机器自己”
  - 不适合 NUC 上直接拿来访问 RK3588
- `100.x.x.x`
  - 属于 Tailscale 地址
  - 适合临时跨设备验证服务是否可达
  - 不建议作为最终“网口通信”的正式地址
- 有线网口 IPv4
  - 才是最终 NUC 与 RK3588 网口通信应使用的正式地址

当前结果：

- Round3 纯有线链路已经打通并完成验收
- `configs/default_config.json` 正式运行时应指向 RK3588 实际可达的有线 IP

## 需要告知 RK3588 端的信息

这部分建议同步给 RK3588 端同学。

### 1. NUC 侧已按冻结契约完成上发

NUC 当前上发字段固定为：

- `robot_pose`
- `nav_status`
- `task_status`
- `device_status`
- `env_sensor`
- `alerts`
- `updated_at`

字段命名和枚举值已经按当前 OpenAPI 契约对齐。

### 2. NUC 当前默认发送频率与容错

- 默认发送频率：`1 Hz`
- HTTP 超时：`2s`
- 失败后重试：`1` 次
- 上送失败不会直接导致程序崩溃退出

### 3. NUC 发送器默认支持的调试模式

- `mock`
  - 用占位状态做持续上发
- `file`
  - 读取本地 JSON 文件后上发

RK3588 端联调时可以先不依赖真实导航栈。

### 4. RK3588 端需要配合确认的事项

- `POST /api/system/mode/switch` 在最终联调前先切到 `real`
- `POST /api/internal/nuc/state` 继续保持当前 OpenAPI 契约不变
- `GET /api/state/latest` 用于验证上送结果
- 如果前端页面依赖 WebSocket，请确认 `WS /ws/state` 能反映最新 real 状态

### 5. 网络侧需要同步的重点

- 本次联调用于网口通信的固定地址为：
  - NUC `enp89s0` 保留 `192.168.1.50/24` 给激光雷达，并额外挂 `192.168.10.3/24`
  - RK3588 有线口 `= 192.168.10.2/24`
- 最终联调请使用 RK3588 有线网口 IPv4，不要使用 `127.0.0.1`
- 不建议把 Tailscale 地址作为正式运行配置
- 如果 RK3588 同时启用 Wi-Fi 和有线网口，建议区分用途，避免把 Wi-Fi 地址误当作网口地址

## 暂未完成

以下内容本次没有做，属于下一阶段：

- RK3588 -> NUC 任务桥接
- NUC 接收任务命令并返回受理结果
- 真实导航栈/任务栈数据接入
- 真实环境传感器接入
- 视频、语音、视觉检测扩展

## 备注

本次实现默认没有把正式运行地址写死成 Tailscale IP。

原因是：

- Tailscale 地址适合临时验证
- 最终需求是网口通信
- 正式配置应以 RK3588 的有线网口固定 IPv4 为准
