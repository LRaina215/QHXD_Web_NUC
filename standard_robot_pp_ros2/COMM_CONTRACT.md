# 北极熊通信上层契约表

## 文档目的

本表只回答一件事：

- 在 `standard_robot_pp_ros2` 通信层改成 BCP 之后，上层导航还能稳定依赖哪些 ROS 接口

原则：

- 只替换通信协议与串口实现
- 不减少上层可发送的 topic
- 不减少上层可消费的 topic
- 允许增加“兼容观测输出”，但不能破坏原有接口

## 当前代码核对结论

当前已按代码核对：

- 对上输入接口未减少
- 对上输出接口未减少
- 额外补充了一个旧包风格兼容输出：
  - `serial/receive`

当前实际节点：

- `standard_robot_pp_ros2`
- `gimbal_manager`

其中：

- `gimbal_manager` 继续接收 `cmd_gimbal`
- `gimbal_manager` 继续输出 `cmd_gimbal_joint`
- `standard_robot_pp_ros2` 继续接收 `cmd_gimbal_joint`

也就是说：

- 上层如果仍走 `cmd_gimbal`，接口没有丢
- 内部桥接 `cmd_gimbal -> cmd_gimbal_joint -> BCP gimbal` 也还在

## 主要参数契约

基础串口参数：

- `device_name`
- `baud_rate`
- `flow_control`
- `parity`
- `stop_bits`

北极熊既有行为相关参数：

- `set_detector_color`
- `record_rosbag`
- `debug`

当前 BCP 兼容层参数：

- `bcp_d_addr`
- `bcp_rx_addr`
- `bcp_gimbal_ctrl_mode`
- `bcp_default_bullet_vel`
- `bcp_default_remain_bullet`

说明：

- 这些参数属于兼容层内部实现细节
- 上层不应依赖“固定 5ms 发送循环”这类内部时序实现
- 当前推荐串口路径为 `/dev/ttyCBoard`

## 对上输入契约

### 1. `cmd_vel`

- 消息类型：`geometry_msgs/msg/Twist`
- 用途：底盘速度控制
- 当前兼容要求：
  - topic 名不变
  - 消息类型不变
  - 仍只依赖：
    - `linear.x`
    - `linear.y`
    - `angular.z`

### 2. `cmd_gimbal`

- 消息类型：`pb_rm_interfaces/msg/GimbalCmd`
- 用途：北极熊对云台的主输入接口
- 当前兼容要求：
  - topic 名不变
  - 消息类型不变
  - 仍由 `gimbal_manager` 消费
  - 仍通过内部桥接转成 `cmd_gimbal_joint`

### 3. `cmd_gimbal_joint`

- 消息类型：`sensor_msgs/msg/JointState`
- 用途：云台关节级兼容输入接口
- 当前兼容要求：
  - topic 名不变
  - 消息类型不变
  - 仍按关节名识别：
    - `gimbal_pitch_joint`
    - `gimbal_yaw_joint`

### 4. `cmd_shoot`

- 消息类型：`example_interfaces/msg/UInt8`
- 用途：射击控制
- 当前兼容要求：
  - topic 名不变
  - 消息类型不变
  - 当前仍保持“收到开火控制时默认 `fric_on = true`”这一既有行为

### 5. `tracker/target`

- 消息类型：`auto_aim_interfaces/msg/Target`
- 用途：视觉跟踪状态输入
- 当前兼容要求：
  - topic 名不变
  - 消息类型不变
  - 兼容层必须继续保留该输入入口

## 对上输出契约

### 北极熊原有输出，必须保留

- `serial/imu`
  - `sensor_msgs/msg/Imu`
- `serial/robot_state_info`
  - `pb_rm_interfaces/msg/RobotStateInfo`
- `serial/gimbal_joint_state`
  - `sensor_msgs/msg/JointState`
- `serial/robot_motion`
  - `geometry_msgs/msg/Twist`
- `referee/event_data`
  - `pb_rm_interfaces/msg/EventData`
- `referee/all_robot_hp`
  - `pb_rm_interfaces/msg/GameRobotHP`
- `referee/game_status`
  - `pb_rm_interfaces/msg/GameStatus`
- `referee/ground_robot_position`
  - `pb_rm_interfaces/msg/GroundRobotPosition`
- `referee/rfid_status`
  - `pb_rm_interfaces/msg/RfidStatus`
- `referee/robot_status`
  - `pb_rm_interfaces/msg/RobotStatus`
- `referee/buff`
  - `pb_rm_interfaces/msg/Buff`
- `serial/debug/<name>`
  - `example_interfaces/msg/Float64`

### 额外增加的兼容输出

- `serial/receive`
  - `geometry_msgs/msg/Vector3`
  - 用途：恢复旧 `bubble_protocol` 联调时直观的姿态观测方式
  - 当前语义：
    - `x = yaw_deg`
    - `y = pitch_deg`
    - `z = roll_deg`

说明：

- `serial/receive` 是新增兼容输出
- 它不会替代 `serial/imu`
- 它的存在是为了减少联调成本，不是为了改变上层标准接口

## 附带行为契约

### 1. `joint_state_publisher` 依赖

- `serial/gimbal_joint_state` 不能删
- `name` 仍应保持：
  - `gimbal_pitch_joint`
  - `gimbal_yaw_joint`
  - `gimbal_pitch_odom_joint`
  - `gimbal_yaw_odom_joint`

### 2. 录包行为

- 若 `record_rosbag = true`
- `referee/game_status` 的比赛阶段变化仍可能触发：
  - `start_recording`
  - `stop_recording`

### 3. 检测器颜色行为

- 若 `set_detector_color = true`
- `referee/robot_status` 仍承担“根据 `robot_id` 推导 `detect_color`”的兼容责任

## 当前接口边界

已经完成并确认保留的，是：

- 输入 topic 外形
- 输出 topic 外形
- `cmd_gimbal` 与 `cmd_gimbal_joint` 双入口
- 动态 debug topic 机制

当前仍可能变化、但不应让上层感知的，是：

- 串口读写实现
- BCP 分帧细节
- 打开串口前的 termios 配置
- 是否使用 `0x13` 还是 `0x20` 作为姿态主来源
- 内部发送节奏与重连策略
