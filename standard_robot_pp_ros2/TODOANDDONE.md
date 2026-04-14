# TODO 与 DONE

## 项目目标

仅替换 `standard_robot_pp_ros2` 的通信层协议与串口收发实现：

- 对上继续提供北极熊原有 ROS 接口
- 对下完全切换到 BCP
- 不走单独兼容节点方案
- 继续沿用 `standard_robot_pp_ros2` 的 C++ 框架

## 当前总状态

当前可以把进度理解成：

- 通信层对接已经过半
- 主要活动收发链路已经改成纯 BCP
- 上层输入接口未减少
- 上层输出接口未减少
- 旧包风格姿态观测已补回
- 剩余工作集中在“非同构裁判信息字段”和“占位接口逐步真值化”

## 当前接口基线结论

### 输入接口未减少

当前继续保留：

- `cmd_vel`
- `cmd_gimbal`
- `cmd_gimbal_joint`
- `cmd_shoot`
- `tracker/target`

### 输出接口未减少

当前继续保留：

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

额外新增但不破坏原接口的兼容输出：

- `serial/receive`

## 当前已完成

### 主收发链路

- [x] `cmd_vel -> BCP 0x12 chassis_ctrl`
- [x] `cmd_gimbal -> gimbal_manager -> cmd_gimbal_joint -> BCP 0x20 gimbal`
- [x] `cmd_shoot -> BCP 0x40 barrel`
- [x] `serial/imu` 第一版打通
- [x] `serial/gimbal_joint_state` 第一版打通
- [x] `serial/robot_motion` 第一版打通
- [x] `referee/game_status` 第一版打通
- [x] `referee/all_robot_hp` 第一版打通

### 兼容观测链路

- [x] 补回旧包风格姿态观测：
  - `serial/receive`
  - 当前 `x/y/z = yaw_deg/pitch_deg/roll_deg`
- [x] 保留动态 debug publisher 机制
- [x] 已增加接收链路与串口状态 debug 观测：
  - `bcp_last_rx_id`
  - `bcp_last_rx_len`
  - `bcp_unhandled_id`
  - `bcp_unhandled_len`
  - `bcp_imu_fallback_from_gimbal`
  - `bcp_addcheck_mismatch_accepted`
  - `bcp_rx_idle_ms`
  - `serial_reopen_count`
  - `serial_termios_configured`

### 串口层修正

- [x] 串口统一命名为 `/dev/ttyCBoard`
- [x] 已写 udev 规则并验证生效
- [x] 已将启动期自动重连等待窗口放宽到 `12s`
- [x] 已在打开串口前强制设置：
  - `raw`
  - `clocal`
  - `-hupcl`
  - `-crtscts`
- [x] 已完成一次“刚 `colcon build` 完不插拔 USB 直接启动”的实测
  - 本轮已成功直接读到：
    - `/serial/receive --once -> x=-14.688, y=-31.729, z=-0.422`
    - `/serial/imu --once` 成功收到 `sensor_msgs/Imu`

### 协议级关键修正

- [x] 已确认问题不是帧头差异
  - 两个包都使用 `0xFF`
- [x] 已确认现网姿态主来源更接近 `0x20 gimbal` 回退，而不是只依赖 `0x13 chassis_imu`
- [x] 已将 C++ 版接收逻辑改为更接近旧包的字节流状态机
- [x] 已将 `addcheck` 校验策略向旧包兼容
- [x] 已把 `0x20 gimbal` 作为 `serial/imu` 的兼容回退来源
- [x] 已把发送策略从“持续激进灌发送”收敛为“仅命令活跃期发送”

## 当前部分完成

- [~] `referee/event_data`
  - 已接入 `0x32 ICRA_buff_debuff_zone` 第一版部分映射
  - 剩余字段还未完全定义
- [~] `referee/rfid_status`
  - 已接入 `0x32 ICRA_buff_debuff_zone` 第一版部分映射
  - 剩余字段还未完全定义

## 当前保留占位

这些接口当前仍保留，但不是完整实时真值：

- [~] `serial/robot_state_info`
- [~] `referee/ground_robot_position`
- [~] `referee/robot_status`
- [~] `referee/buff`

说明：

- 这些 publisher 没有删
- 当前主要以一次性占位发布保接口为主
- 后续逐项替换为真实数据源

## 当前仍待解决

- [ ] `tracker/target.tracking` 的最终 BCP 承载方式
- [ ] `gimbal` 第 4、5 位和旧包现网复用语义的最终对齐
- [ ] `barrel.bullet_vel` 的真实来源
- [ ] `barrel.remain_bullet` 的真实来源
- [ ] `referee/buff` 的正式语义落地
- [ ] `referee/event_data` 剩余字段的真实落地
- [ ] `referee/rfid_status` 剩余字段的真实落地
- [ ] 非同构占位接口逐步真值化

## 本轮文档整理结论

这轮已经明确并固化：

1. 当前改造不是“删原接口再补新接口”，而是“原接口保留，底层换成 BCP”。
2. 当前活动主链路已经能闭环工作。
3. 当前问题重心已经从“基本通信能不能通”转向“剩余未定义字段如何真值化”。
4. 后续所有新增改动都应继续遵守：
   - 输入接口不减少
   - 输出接口不减少
   - 未完全定义项不删，只能继续登记和推进

## 下轮优先级

1. 继续把 `referee/event_data`、`referee/rfid_status`、`referee/buff` 的剩余字段语义做实。
2. 继续把 `serial/robot_state_info`、`referee/ground_robot_position`、`referee/robot_status` 从占位推进到真实源。
3. 继续观察“build 后无需插拔 USB”是否稳定复现，若仍偶发，再继续收敛串口恢复策略。
4. 继续维护：
   - `COMM_CONTRACT.md`
   - `COMM_MAPPING.md`
   - `TARGET_ENV_CHECKLIST.md`
   - `NOTDEFINE.md`
