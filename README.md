# QHXD_NUC

本目录实现了两条 NUC 侧联调能力：

- `NUC -> RK3588` 的网口状态上发链路
- `RK3588 -> NUC` 的 mission 接口接收服务
- `C板 / RT-Thread -> NUC` 的真实低层采集入口

目标是把真实状态回传和任务桥接都先打通。

当前实现满足 `NUC_DO.md` 里的这些要求：

- 按 `state_collector / state_mapper / rk3588_sender` 三层拆分
- 周期性向 `POST /api/internal/nuc/state` 上发状态
- 提供 `POST /api/internal/rk3588/mission` 本地服务
- 默认发送频率 `1 Hz`
- 默认超时 `2s`
- 默认失败重试 `1` 次
- 支持告警字段一并上送
- 支持 `dry-run`，方便在还没接网线时先本地验证

## 目录说明

- `nuc_state_uploader/state_collector.py`
  - 负责采集 NUC 内部状态
  - 当前提供 `mock` 和 `file` 两种采集器
- `nuc_state_uploader/state_mapper.py`
  - 负责把内部字段映射成 RK3588 Phase 2 JSON 契约
  - 负责默认值补齐、枚举归一化、数值裁剪
- `nuc_state_uploader/rtt_state_collector.py`
  - 负责采集 C 板 / RT-Thread 低层状态
  - 当前提供 `mock`、`file`、`ros2` 三种低层采集模式
- `nuc_state_uploader/rk3588_sender.py`
  - 负责 HTTP POST、超时、最小重试和返回日志
- `nuc_state_uploader/imu_bridge.py`
  - 负责把真实 IMU 话题归一化成可转发的 JSON 结构
- `nuc_state_uploader/main.py`
  - 命令行入口
- `nuc_state_uploader/mission_server.py`
  - 负责启动 NUC mission HTTP 服务
- `nuc_state_uploader/runtime_state.py`
  - 负责保存任务桥接后的运行态，并回流到状态上送
- `configs/default_config.json`
  - 默认启动配置
- `examples/internal_state.sample.json`
  - `file` 采集器的示例输入
- `configs/cboard_imu.sample.json`
  - C 板 / RT-Thread 实机 IMU 联调配置样例

## 环境要求

- Python `3.10+`
- 当前实现只使用标准库，不依赖第三方包

## 启动流程

### 1. 进入目录

```bash
cd /home/robomaster/QHXD_NUC
```

### 2. 本地无网线时先做 dry-run

这一步不会发 HTTP，只会采集并组出最终上送 JSON。

```bash
python3 -m nuc_state_uploader.main send-once --dry-run --print-payload
```

如果要看周期运行效果：

```bash
python3 -m nuc_state_uploader.main run --dry-run --print-payload
```

### 3. RK3588 后端启动后切换到 real 模式

按照 `NUC_DO.md` 中的要求，RK3588 只有在 `real` 模式下才会接受 NUC 状态。

如果命令是在 NUC 上执行，`127.0.0.1` 指向的是 NUC 本机，不是 RK3588。
这时必须把命令里的地址改成 NUC 实际可达的 RK3588 IP。

如果你的 NUC 环境里设置了 `http_proxy/https_proxy/all_proxy`，建议联调时加上 `--noproxy '*'`，避免本地代理把局域网请求转走。

用本项目命令切换：

```bash
python3 -m nuc_state_uploader.main --config configs/default_config.json switch-mode --mode real
```

或者直接用 curl：

```bash
curl --noproxy '*' -X POST http://<RK3588_IP>:8000/api/system/mode/switch \
  -H 'Content-Type: application/json' \
  -d '{"mode":"real","source":"integration-test","requested_by":"operator"}'
```

### 4. 启动 NUC mission 服务

如果需要让 RK3588 把任务下发到 NUC，需要先启动 mission 服务。

默认监听地址来自 `configs/default_config.json`：

- `host = 0.0.0.0`
- `port = 8090`
- `path = /api/internal/rk3588/mission`

也就是默认对外提供：

```text
http://<NUC_IP>:8090/api/internal/rk3588/mission
```

启动命令：

```bash
python3 -m nuc_state_uploader.main serve-mission
```

如果你使用自定义配置文件：

```bash
python3 -m nuc_state_uploader.main --config /tmp/qhxd_round4_config.json serve-mission
```

启动成功后，终端会看到类似日志：

```text
Mission server listening on 0.0.0.0:8090/api/internal/rk3588/mission
```

当前支持的命令包括：

- `go_to_waypoint`
- `start_patrol`
- `pause_task`
- `resume_task`
- `return_home`

### 5. 真正开始上发

单次发送：

```bash
python3 -m nuc_state_uploader.main send-once --print-payload
```

周期发送：

```bash
python3 -m nuc_state_uploader.main run
```

### 6. 真实 C 板 / IMU 联调

如果当前 C 板已经通过 `standard_robot_pp_ros2` 把真实 IMU 发布到 ROS2：

```bash
source /opt/ros/humble/setup.bash
source /home/robomaster/pb2025/install/setup.bash
ros2 launch standard_robot_pp_ros2 standard_robot_pp_ros2.launch.py
```

可使用样例配置检查 NUC 是否已经拿到真实低层链路：

```bash
python3 -m nuc_state_uploader.main --config configs/cboard_imu.sample.json inspect-rtt
```

这条命令会打印：

- `device`
- `environment`
- `imu`

其中：

- `device.online`
  - 由 `/serial/imu` 或 `/serial/robot_motion` 是否新鲜驱动
- `imu`
  - 为当前最新的真实 `sensor_msgs/Imu` 样本
  - 如果 `/serial/receive` 可用，还会额外包含 `euler_deg.yaw/pitch/roll`

如果 RK3588 后端后续提供了专用 IMU 接收接口，还可以直接做一次单次转发：

```bash
python3 -m nuc_state_uploader.main --config configs/cboard_imu.sample.json send-imu-once --print-payload
```

默认样例配置里的 IMU 接口路径是：

```text
/api/internal/nuc/imu
```

如果 RK3588 实际使用的是别的路径，请改 `rk3588.imu_endpoint`。

当前方案 A 已启用：

- 保留 `imu.orientation` 四元数
- 同时补发 `imu.euler_deg`

其中欧拉角来自：

- `/serial/receive.x = yaw (deg)`
- `/serial/receive.y = pitch (deg)`
- `/serial/receive.z = roll (deg)`

### 6.1 IMU 是否自动执行

当前版本里，**IMU 读取与 IMU 转发都不会自动执行**。

更准确地说：

- `python3 -m nuc_state_uploader.main run`
  - 只负责原有 `POST /api/internal/nuc/state` 状态上送
  - **不会自动把 `/serial/imu` 连续转发给 RK3588**
- `python3 -m nuc_state_uploader.main inspect-rtt`
  - 只做一次当前低层状态与 IMU 读取检查
- `python3 -m nuc_state_uploader.main send-imu-once`
  - 只做一次当前 IMU 样本转发

所以如果你要读 IMU 或把 IMU 发到 RK3588，当前都需要手动启动。

### 6.2 IMU 启动方法

#### 步骤 1：启动 C 板 ROS2 bridge

先让 `/dev/ttyCBoard` 对应的 `standard_robot_pp_ros2` 跑起来：

```bash
cd /home/robomaster
source /opt/ros/humble/setup.bash
source /home/robomaster/pb2025/install/setup.bash
ros2 launch standard_robot_pp_ros2 standard_robot_pp_ros2.launch.py log_level:=info
```

预期至少会看到：

- `Serial port opened!`

如果现场串口链路正常，后续 `inspect-rtt` 会看到非空 `imu`。

#### 步骤 2：读取一次当前 IMU

另开一个终端，执行：

```bash
cd /home/robomaster/QHXD_NUC
python3 -m nuc_state_uploader.main --config configs/cboard_imu.sample.json inspect-rtt
```

预期输出中至少应看到：

- `device.online = true`
- `imu.frame_id`
- `imu.timestamp`
- `imu.orientation`
- `imu.euler_deg.yaw`
- `imu.euler_deg.pitch`
- `imu.euler_deg.roll`
- `imu.angular_velocity`
- `imu.linear_acceleration`

如果这里还是：

- `device.online = false`
- `imu = null`

说明当前 C 板没有稳定送出新鲜 IMU 样本，需要先排查串口/BCP 链路。

#### 步骤 3：向 RK3588 单次转发 IMU

确认 RK3588 已启动并提供：

- `POST /api/internal/nuc/imu`
- `GET /api/imu/latest`

再执行：

```bash
cd /home/robomaster/QHXD_NUC
python3 -m nuc_state_uploader.main --config configs/cboard_imu.sample.json switch-mode --mode real --source imu-bridge --requested-by operator
python3 -m nuc_state_uploader.main --config configs/cboard_imu.sample.json send-imu-once --print-payload
```

成功时会看到类似：

- `accepted = true`
- `imu_updated = true`

然后可在 RK3588 侧检查：

```bash
curl --noproxy '*' http://192.168.10.2:8000/api/imu/latest
```

#### 步骤 4：实时看 IMU WebSocket

如果 RK3588 前后端已经支持 IMU 展示，可以继续看：

- `GET /api/imu/latest`
- `WS /ws/imu`
- 前端 IMU 调试卡片

### 6.3 当前自动化边界

当前还**没有**实现下面这类自动行为：

- NUC 开机后自动启动 IMU bridge
- `run` 启动后自动持续转发 IMU
- `inspect-rtt` 成功后自动切 `real` 并自动发 IMU

如果你后面希望它“像状态上送那样自动持续执行”，下一步可以再补一个连续 IMU bridge 命令，例如：

- `run-imu`
- 或把 IMU 转发并入一个统一守护进程

目前这部分还没有做。

### 7. 联调时检查结果

检查 RK3588 最新状态：

```bash
curl --noproxy '*' http://<RK3588_IP>:8000/api/state/latest
```

预期至少看到：

- `task_status.source` 为 `nuc`
- `system_mode.mode` 为 `real`
- `robot_pose`、`battery_percent`、`current_goal` 等字段与 NUC 发送值一致

如果还要联调任务桥接，可以直接从 NUC 本机先验证 mission 服务：

```bash
curl --noproxy '*' -X POST http://<NUC_IP>:8090/api/internal/rk3588/mission \
  -H 'Content-Type: application/json' \
  -d '{"command":"go_to_waypoint","source":"integration-test","requested_by":"operator","payload":{"waypoint_id":"wp-demo"}}'
```

预期返回：

- `accepted = true`
- `task_status.task_type = go_to_waypoint`
- `current_goal = wp-demo`

## 使用方式

### 默认模式：`mock`

默认配置里 `collector.type` 是 `mock`，程序会自动生成一组缓慢变化的状态，适合先打通接口链路。

启动命令：

```bash
python3 -m nuc_state_uploader.main run
```

### 真实低层模式：`ros2`

当 C 板已经通过 `standard_robot_pp_ros2` 提供 ROS2 话题时，可以把低层采集切到真实模式：

```json
{
  "rtt_collector": {
    "type": "ros2",
    "source_name": "rtt",
    "imu_topic": "/serial/imu",
    "receive_topic": "/serial/receive",
    "motion_topic": "/serial/robot_motion",
    "sample_timeout_sec": 0.5,
    "freshness_timeout_sec": 2.0
  }
}
```

当前这条真实低层链路主要用于：

- 判断 `device.online`
- 读取 `velocity_mps`
- 提取最新 IMU 样本
- 提取 `yaw / pitch / roll (deg)` 并补进 `imu.euler_deg`

由于当前 C 板侧还没有稳定提供电量、急停、故障码，所以这些字段暂时允许保持空值占位，后续再由更多 BCP 包补齐。

### 文件模式：`file`

如果你想先手改状态数据再发送，把 `configs/default_config.json` 改成：

```json
{
  "collector": {
    "type": "file",
    "state_file": "examples/internal_state.sample.json",
    "frame_id": "map",
    "source_name": "nuc"
  }
}
```

然后编辑 `examples/internal_state.sample.json`，程序每次发送前都会重新读取这个文件。

这很适合还没接导航栈、但想验证字段映射和页面刷新时使用。

## 当前实现目的

这版实现主要解决 4 件事：

1. 让 NUC 端先独立完成“真实状态上送”能力，不依赖后续任务桥接。
2. 把 RK3588 已冻结的字段契约固化下来，避免联调时字段名漂移。 
3. 让没有网线、没有真实导航输入时，仍然可以做本地 dry-run、自测和 JSON 验证。
4. 为后续接入真实导航、任务和设备状态预留明确改点。

## 后续接真实数据怎么改

最关键的是改 `nuc_state_uploader/state_collector.py`。

### 方案 A：直接替换 `MockStateCollector`

把 `collect()` 中的占位逻辑替换成你真实的导航系统取值，例如：

- 位姿来源改成定位模块
- `navigation.goal_id` 改成当前导航目标点
- `task.status` 改成任务管理模块状态
- `device.battery_percent` 改成底盘或 BMS 实际值
- `alerts` 改成你们内部告警列表

只要 `collect()` 返回的内部结构保持下面这些键，`state_mapper.py` 就能继续工作：

```json
{
  "pose": {},
  "navigation": {},
  "task": {},
  "device": {},
  "environment": {},
  "alerts": [],
  "updated_at": ""
}
```

### 方案 B：新增你自己的采集器

如果后续要对接 ROS2、串口桥、共享内存或现有任务系统，建议：

1. 在 `state_collector.py` 里新增一个采集器类
2. 实现 `collect(self, seq)` 方法
3. 在 `build_collector()` 里加一个新的 `collector.type`
4. 在 `configs/default_config.json` 里切换到新的类型

这样不会影响已有 `mock` 和 `file` 调试链路。

### 方案 C：继续扩真实 C 板字段

如果后续 C 板补上新的 BCP 包，建议继续在 `nuc_state_uploader/rtt_state_collector.py` 扩展，而不是把底层协议解析塞回主采集器。

当前已经预留好的真实入口有：

- `/serial/imu`
- `/serial/robot_motion`

后续可以继续往里补：

- 电量
- 急停
- 故障码
- 更多环境量

## 调试与排查

### 1. 先只看组包结果

```bash
python3 -m nuc_state_uploader.main send-once --dry-run --print-payload
```

这一步能确认：

- JSON 字段名是否和 `NUC_DO.md` 一致
- 默认值是否符合约定
- 告警、时间戳、枚举值是否被正确归一化

### 2. 如果 POST 成功但 `accepted: false`

大概率是 RK3588 还在 `mock` 模式。先执行：

```bash
python3 -m nuc_state_uploader.main switch-mode --mode real
```

### 3. 如果程序报网络错误

优先检查：

- 如果你在 RK3588 本机测试，后端是否真的启动在 `http://127.0.0.1:8000`
- 如果你在 NUC 上测试，`configs/default_config.json` 里的 `base_url` 是否写成了 NUC 实际可达的 RK3588 网口 IP
- NUC 与 RK3588 是否同网段、网线是否接通
- 防火墙或端口占用是否阻断了连接

### 4. 如果页面没更新

优先检查：

- 在 NUC 上执行 `curl --noproxy '*' http://<RK3588_IP>:8000/api/state/latest` 是否已经变化
- RK3588 是否处于 `real` 模式
- `task_status.source` 是否确实为 `nuc`
- WebSocket 页面是否连接到了正确的 RK3588 服务

### 5. 如果字段不对

优先改两个地方：

- 采集逻辑不对：改 `nuc_state_uploader/state_collector.py`
- 字段名或枚举映射不对：改 `nuc_state_uploader/state_mapper.py`

## 配置项说明

`configs/default_config.json` 中最常改的是：

- `send_interval_sec`
  - 发送周期，默认 `1.0`
- `rk3588.base_url`
  - RK3588 后端地址
  - 如果发送程序运行在 NUC 上，通常应改成 `http://<RK3588_IP>:8000`，而不是 `http://127.0.0.1:8000`
- `rk3588.timeout_sec`
  - HTTP 超时，默认 `2.0`
- `rk3588.retry_count`
  - 失败后重试次数，默认 `1`
- `collector.type`
  - `mock` 或 `file`
- `collector.state_file`
  - `file` 模式下读取的 JSON 文件
- `collector.runtime_state_file`
  - mission 服务和状态上送共享的运行态文件
- `mission_server.host`
  - mission 服务监听地址，默认 `0.0.0.0`
- `mission_server.port`
  - mission 服务监听端口，默认 `8090`
- `mission_server.path`
  - mission 服务路径，默认 `/api/internal/rk3588/mission`
- `mission_server.runtime_state_file`
  - mission 服务写入的运行态文件，通常应与 `collector.runtime_state_file` 保持一致
- `rk3588.imu_endpoint`
  - 可选的 IMU HTTP 转发路径
  - 只有当 RK3588 后端提供专用 IMU 接口时才需要配置
- `rtt_collector.type`
  - `mock`、`file` 或 `ros2`
- `rtt_collector.imu_topic`
  - 真实 IMU 话题，默认 `/serial/imu`
- `rtt_collector.receive_topic`
  - 真实欧拉角话题，默认 `/serial/receive`
- `rtt_collector.motion_topic`
  - 真实速度反馈话题，默认 `/serial/robot_motion`
- `rtt_collector.sample_timeout_sec`
  - 单次采样时的 ROS2 spin 等待时间
- `rtt_collector.freshness_timeout_sec`
  - 判断真实低层链路是否在线的超时时间

## 自测命令

运行单元测试：

```bash
python3 -m unittest discover -s tests
```

## 当前状态

本项目已经完成一轮纯有线 Round3 联调验收。

本轮实测结果见：

- [ROUND3验收.md](/home/robomaster/QHXD_NUC/ROUND3验收.md#L1)

当前确认：

- RK3588 有线地址：`192.168.10.2`
- NUC 有线网口 `enp89s0` 保留激光雷达地址 `192.168.1.50/24`
- 为 RK3588 联调额外挂第二地址：`192.168.10.3/24`
- `POST /api/internal/nuc/state`、`GET /api/state/latest`、`WS /ws/state` 已在纯有线路径下实测通过
- C 板 / RT-Thread 真实 IMU 接入与本轮现场结论见：
  - [PHASE3_ROUND3_1验收.md](/home/robomaster/QHXD_NUC/PHASE3_ROUND3_1验收.md#L1)

## 当前状态说明

代码已经完成到“可运行、可 dry-run、可在通网后直接 POST 联调”的阶段。

因为当前还没有接网线，本次默认不把“实际与 RK3588 成功通信”作为完成标准；等网络接通后，按照上面的 `switch-mode -> run -> curl /api/state/latest` 流程即可继续联调验收。

如果最终要求是“纯网口通信”，推荐给 RK3588 与 NUC 的有线网口分配单独的固定 IPv4 地址，并把 `rk3588.base_url` 配置为该有线 IP，而不是 Wi-Fi 地址或 Tailscale 地址。
