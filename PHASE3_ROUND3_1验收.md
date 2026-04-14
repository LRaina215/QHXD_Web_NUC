# PHASE3 Round3.1 验收

## 验收时间

- 本次验收时间：`2026-04-15`（Asia/Shanghai）

## 本轮目标调整说明

按 [NUC_DO6_1.md](/home/robomaster/QHXD_NUC/NUC_DO6_1.md#L1) 原始要求，本轮应优先把：

- `device_status.battery_percent`
- `device_status.emergency_stop`
- `device_status.fault_code`
- `device_status.online`

改成来自真实 C 板 / RT-Thread。

但结合当前现场约束，已确认：

- C 板侧暂时还没有稳定提供电量、急停、故障码对应的 BCP 数据
- 用户已明确接受这些字段先留空
- 当前 C 板已知最有希望先打通的真实数据是 IMU

因此本轮实际执行目标调整为：

- 先把 **真实 C 板 IMU / 运动反馈** 接入 NUC
- 在 NUC 侧补齐真实 ROS2 订阅采集接口
- 预留 **IMU 经网口转发到 RK3588** 的 HTTP 接口配置
- 同时给出这轮现场是否已经具备“真实 IMU 整链通过”的验收结论

## 最终结论

本轮 **NUC 侧代码能力已完成**，但 **现场实机整链验收当前不能判定通过**。

更准确地说：

- **通过**：
  - `NUC -> C板` 的真实接入代码入口已补齐
  - 已新增真实 `ros2` 低层采集模式
  - 已新增真实 IMU 样本归一化结构
  - 已新增可配置的 IMU HTTP 转发能力
  - 已留下样例配置与 README 启动说明
- **未通过**：
  - 当前现场没有采到新鲜 IMU 帧
  - 当前也未能把 IMU 真值实际发到 RK3588 后端

所以这轮当前更准确的表述应是：

- **Phase 3 Round 3.1 的 NUC 侧真实 IMU 接入代码已完成**
- **现场实机链路仍受 C 板当前无有效 BCP 帧 / RK3588 IMU 接口未实测可达影响，暂不能判整轮通过**

## 本轮完成的代码改动

### 1. 新增真实 `ros2` 低层采集模式

已在 [rtt_state_collector.py](/home/robomaster/QHXD_NUC/nuc_state_uploader/rtt_state_collector.py#L1) 增加：

- `Ros2TopicRttStateCollector`

当前真实低层模式会订阅：

- `/serial/imu`
- `/serial/robot_motion`

并在 NUC 内部归一化出：

- `device.online`
- `device.velocity_mps`

以及最新 IMU 样本。

### 2. 新增 IMU 归一化结构

已新增 [imu_bridge.py](/home/robomaster/QHXD_NUC/nuc_state_uploader/imu_bridge.py#L1)，用于定义：

- `NormalizedImuSample`
- `QuaternionSample`
- `Vector3Sample`

以及 IMU HTTP 转发 payload 结构：

```json
{
  "source": "rtt",
  "updated_at": "2026-04-15T00:00:00.123456Z",
  "imu": {
    "frame_id": "gimbal_pitch_odom",
    "timestamp": "2026-04-15T00:00:00.123456Z",
    "orientation": {"x": 0, "y": 0, "z": 0, "w": 1},
    "angular_velocity": {"x": 0, "y": 0, "z": 0},
    "linear_acceleration": {"x": 0, "y": 0, "z": 0}
  }
}
```

### 3. 新增命令行验收入口

已在 [main.py](/home/robomaster/QHXD_NUC/nuc_state_uploader/main.py#L1) 增加：

- `inspect-rtt`
  - 打印当前真实低层 `device / environment / imu`
- `send-imu-once`
  - 构造一次 IMU JSON，并在配置了 `rk3588.imu_endpoint` 时转发到 RK3588

### 4. 新增样例配置

已新增：

- [cboard_imu.sample.json](/home/robomaster/QHXD_NUC/configs/cboard_imu.sample.json#L1)

当前样例配置中：

- `rtt_collector.type = "ros2"`
- `rtt_collector.imu_topic = "/serial/imu"`
- `rtt_collector.motion_topic = "/serial/robot_motion"`
- `rk3588.imu_endpoint = "/api/internal/nuc/imu"`

注意：

- 这个 IMU endpoint 是 **NUC 侧预留配置项**
- 如果 RK3588 端使用别的路径，需要按实际接口改配置

## 本轮现场实测

### 1. C 板真实通信方式确认

本次已确认当前真实链路为：

- 接口类型：`USB CDC / 串口`
- NUC 设备名：`/dev/ttyCBoard`
- 实际映射：`/dev/ttyACM0`
- 上层接收程序：`standard_robot_pp_ros2`
- 当前已知真实 ROS2 话题：
  - `/serial/imu`
  - `/serial/robot_motion`

使用启动方式：

```bash
source /opt/ros/humble/setup.bash
source /home/robomaster/pb2025/install/setup.bash
ros2 launch standard_robot_pp_ros2 standard_robot_pp_ros2.launch.py log_level:=info
```

### 2. `standard_robot_pp_ros2` 真实启动结果

实测启动后可见：

- ROS2 节点能启动
- `serial/imu`、`serial/robot_motion` 等 topic 会被创建
- 串口会短暂打开成功：
  - `Serial port opened!`

但随后持续出现：

- `No valid BCP frame for 12001 ms, forcing serial reopen.`
- `Serial port reopened after rx stall.`
- `Error receiving data: read_some: Bad file descriptor`

结论：

- **串口设备存在**
- **ROS2 bridge 能启动**
- **但当前现场没有持续收到有效 BCP 帧**

### 3. 真实低层采集器本地检查

在上述真实 ROS2 bridge 运行时，执行：

```bash
python3 -m nuc_state_uploader.main --config configs/cboard_imu.sample.json inspect-rtt
```

本次实际输出为：

```json
{
  "device": {
    "battery_percent": null,
    "emergency_stop": false,
    "fault_code": null,
    "online": false,
    "velocity_mps": null,
    "source": "rtt"
  },
  "environment": {
    "temperature_c": null,
    "humidity_percent": null,
    "status": "offline",
    "source": "rtt"
  },
  "imu": null
}
```

这说明：

- 新增的真实 `ros2` 低层采集器已经被实际调用
- 但当前时刻没有采到新鲜 IMU 样本
- 因而 `device.online` 也正确落为 `false`

### 4. 直接读取串口原始字节

为了区分“ROS2 解析问题”和“现场根本没有串口数据”，我停止了 `standard_robot_pp_ros2`，直接从 `/dev/ttyCBoard` 读了 3 秒原始串口数据。

实测结果：

- `chunks = 0`
- `bytes = 0`

结论：

- **在本次验收窗口内，NUC 侧直接读串口也没有收到任何原始字节**

这进一步说明当前阻塞点更偏向：

- C 板当前时刻没有稳定往串口送出可读数据
- 或 USB 串口链路仍处于现场不稳定状态

而不只是 NUC 新增采集器实现问题。

### 5. RK3588 IMU 接口联调可达性

我尝试访问当前纯有线 RK3588 地址：

```bash
curl --noproxy '*' http://192.168.10.2:8000/openapi.json
```

本次结果为：

- `Connection refused`

这意味着当前验收窗口内：

- 不能证明 RK3588 上已经存在并对外监听 IMU 接收接口
- 也就不能完成真实 IMU 的最终 HTTP 转发验收

## 本轮测试结论分层

### A. NUC 侧代码实现是否完成

- **通过**

理由：

- 真实 `ros2` 低层采集入口已补齐
- IMU 样本归一化结构已补齐
- 可选 HTTP 转发接口已补齐
- README 和样例配置已补齐

### B. NUC 是否已证明自己能接住真实 C 板 IMU

- **本次未通过**

理由：

- 当前现场窗口内，`inspect-rtt` 未拿到新鲜 IMU
- 直接读 `/dev/ttyCBoard` 也未读到原始字节

### C. NUC 是否已证明能把真实 IMU 发到 RK3588

- **本次未通过**

理由：

- 当前未采到真实 IMU 样本
- 当前 RK3588 `192.168.10.2:8000` 也未处于可用监听状态

## 单元测试

执行：

```bash
python3 -m unittest discover -s tests -v
```

结果：

- `Ran 12 tests`
- `OK`

本轮新增覆盖包括：

- IMU payload 结构
- ROS 时间转 ISO 格式
- 主采集器对低层 IMU 样本的透传入口

## 当前建议对外表述

建议当前统一表述为：

```text
Phase 3 Round 3.1 中，NUC 侧已经完成真实 C 板 IMU 接入所需的代码改造，
包括 ROS2 低层采集、IMU 样本归一化和可配置的 HTTP 转发入口。

但本次现场验收窗口内，C 板串口未持续输出有效 BCP 帧，
且 RK3588 IMU 接口未实测可达，因此当前只能确认 NUC 侧实现已就绪，
暂不能把“真实 IMU 已整链转发到 RK3588”判定为通过。
```

## 后续继续联调时建议直接执行

### 1. 先启动 C 板 ROS2 bridge

```bash
source /opt/ros/humble/setup.bash
source /home/robomaster/pb2025/install/setup.bash
ros2 launch standard_robot_pp_ros2 standard_robot_pp_ros2.launch.py log_level:=info
```

### 2. 本地观察真实低层对象

```bash
python3 -m nuc_state_uploader.main --config configs/cboard_imu.sample.json inspect-rtt
```

### 3. 如果 `imu` 已不为 null，再做一次 IMU dry-run

```bash
python3 -m nuc_state_uploader.main --config configs/cboard_imu.sample.json send-imu-once --dry-run --print-payload
```

### 4. RK3588 IMU 接口可用后，再把 `rk3588.imu_endpoint` 改成真实路径并实发

```bash
python3 -m nuc_state_uploader.main --config configs/cboard_imu.sample.json send-imu-once --print-payload
```
