# 目标环境联调检查清单

## 文档目的

本清单用于确认三件事：

1. `standard_robot_pp_ros2` 已改成纯 BCP 通信
2. 北极熊上层 ROS 接口没有缩水
3. 当前真实值、占位值、兼容输出的边界是清楚的

## 启动前准备

### 1. 串口确认

优先确认：

- `/dev/ttyCBoard` 存在

建议执行：

```bash
ls -l /dev/ttyCBoard /dev/ttyACM* /dev/serial/by-id
```

当前推荐统一使用：

- `/dev/ttyCBoard`

### 2. 参数确认

重点检查 [standard_robot_pp_ros2.yaml](/home/robomaster/pb2025/src/standard_robot_pp_ros2/config/standard_robot_pp_ros2.yaml)：

- `device_name`
- `baud_rate`
- `debug`
- `bcp_d_addr`
- `bcp_rx_addr`
- `bcp_gimbal_ctrl_mode`
- `bcp_default_bullet_vel`
- `bcp_default_remain_bullet`

联调建议：

- 先设 `debug: true`

### 3. 启动对象

主启动文件：

- [standard_robot_pp_ros2.launch.py](/home/robomaster/pb2025/src/standard_robot_pp_ros2/launch/standard_robot_pp_ros2.launch.py)

实际节点：

- `standard_robot_pp_ros2`
- `gimbal_manager`

## 第一组检查：接口有没有减少

### 上层输入接口不应减少

当前应继续可用：

- `cmd_vel`
- `cmd_gimbal`
- `cmd_gimbal_joint`
- `cmd_shoot`
- `tracker/target`

核对点：

- topic 名不变
- 消息类型不变
- `cmd_gimbal` 仍由 `gimbal_manager` 接收
- `cmd_gimbal_joint` 仍由 `standard_robot_pp_ros2` 接收

### 上层输出接口不应减少

当前应继续可见：

- `serial/imu`
- `serial/robot_state_info`
- `serial/gimbal_joint_state`
- `serial/robot_motion`
- `referee/event_data`
- `referee/all_robot_hp`
- `referee/game_status`
- `referee/ground_robot_position`
- `referee/rfid_status`
- `referee/robot_status`
- `referee/buff`
- `serial/debug/<name>`

额外增加但不破坏原接口的兼容输出：

- `serial/receive`

核对命令：

```bash
ros2 topic list | rg 'cmd_|serial/|referee/|tracker/target'
```

## 第二组检查：build 后不插拔是否还能起来

这是当前非常关键的一项回归。

步骤：

1. 先执行一次 `colcon build`
2. 不插拔 USB
3. 直接启动通信包

启动命令：

```bash
cd /home/robomaster/pb2025
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch standard_robot_pp_ros2 standard_robot_pp_ros2.launch.py
```

当前期望：

- 节点可直接启动
- 不应再出现“每次 build 完都必须物理插拔 USB 才能恢复”的必现情况

若仍偶发失败：

- 先记录这是否是偶发
- 再看 `serial/debug/serial_termios_configured`
- 再看是否出现持续 `reopen`

## 第三组检查：姿态链路

### 1. 旧包风格角度观测

直接看：

```bash
ros2 topic echo /serial/receive
```

当前语义：

- `x = yaw_deg`
- `y = pitch_deg`
- `z = roll_deg`

这是当前最直观的联调入口。

### 2. 标准 IMU 观测

再看：

```bash
ros2 topic echo /serial/imu
```

注意：

- `orientation.x/y/z/w` 是四元数，不是欧拉角
- “数值很小”本身是正常现象
- 如果要看角度，请回到 `serial/receive`

### 3. 云台 joint 状态

```bash
ros2 topic echo /serial/gimbal_joint_state
```

核对点：

- 仍有以下 joint 名：
  - `gimbal_pitch_joint`
  - `gimbal_yaw_joint`
  - `gimbal_pitch_odom_joint`
  - `gimbal_yaw_odom_joint`

## 第四组检查：主要发送链路

### 1. `cmd_vel -> 0x12 chassis_ctrl`

测试：

```bash
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist \
"{linear: {x: 0.15, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}" -r 10
```

核对点：

- 底盘动作与 `cmd_vel` 一致
- 停止时兼容旧包静止特殊值逻辑不应引起异常

### 2. `cmd_gimbal -> gimbal_manager -> cmd_gimbal_joint -> 0x20 gimbal`

核对点：

- `cmd_gimbal` 仍能驱动云台
- `cmd_gimbal_joint` 内部桥接仍生效

### 3. `cmd_shoot -> 0x40 barrel`

核对点：

- 开火控制仍能下发
- `bullet_vel` / `remain_bullet` 当前仍是兼容默认值

## 第五组检查：主要接收链路

### 1. `serial/robot_motion`

期望来源：

- `0x11 chassis_odom`

核对：

```bash
ros2 topic echo /serial/robot_motion
```

### 2. `referee/game_status`

期望来源：

- `0x30 game_status`

核对：

```bash
ros2 topic echo /referee/game_status
```

### 3. `referee/all_robot_hp`

期望来源：

- `0x31 robot_HP`

核对：

```bash
ros2 topic echo /referee/all_robot_hp
```

### 4. `referee/event_data`

当前状态：

- `0x32 ICRA_buff_debuff_zone` 第一版部分映射

核对重点：

- F1-F5 是否与当前场地语义相符

### 5. `referee/rfid_status`

当前状态：

- `0x32 ICRA_buff_debuff_zone` 第一版部分映射

核对重点：

- 补给区
- 中央高地
- 梯形高地
- 中心增益区

## 第六组检查：未完全定义项

以下接口当前不能删，但仍不一定是实时真实数据：

- `serial/robot_state_info`
- `referee/ground_robot_position`
- `referee/robot_status`
- `referee/buff`

当前核对目标：

- 确认这些 topic 仍然存在
- 确认它们没有因为协议切换而消失
- 后续再逐步从“占位”推进到“真实源”

## 第七组检查：调试观测

建议优先看：

- `serial/debug/bcp_last_rx_id`
- `serial/debug/bcp_last_rx_len`
- `serial/debug/bcp_unhandled_id`
- `serial/debug/bcp_unhandled_len`
- `serial/debug/bcp_imu_fallback_from_gimbal`
- `serial/debug/bcp_addcheck_mismatch_accepted`
- `serial/debug/bcp_rx_idle_ms`
- `serial/debug/serial_reopen_count`
- `serial/debug/serial_termios_configured`

目标：

- 判断当前姿态到底来自 `0x13` 还是 `0x20` 回退
- 判断是否还在频繁 `reopen`
- 判断是否还有未消费的 BCP 帧

## 联调记录要求

每轮目标环境验证后，至少回填：

- 哪些 topic 已确认真实可用
- 哪些 topic 只是保留占位
- build 后是否仍需要物理插拔 USB
- `serial/receive` 角度值是否符合预期
- `serial/imu` 与 `serial/receive` 是否语义一致

回填文件：

- [TODOANDDONE.md](/home/robomaster/pb2025/src/standard_robot_pp_ros2/TODOANDDONE.md)
- [NOTDEFINE.md](/home/robomaster/pb2025/src/standard_robot_pp_ros2/NOTDEFINE.md)
- [COMM_MAPPING.md](/home/robomaster/pb2025/src/standard_robot_pp_ros2/COMM_MAPPING.md)
