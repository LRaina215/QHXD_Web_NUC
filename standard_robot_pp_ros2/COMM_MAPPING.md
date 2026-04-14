# 北极熊接口到 BCP 的协议映射表

## 文档目的

本表记录当前兼容层内部如何把“北极熊 ROS 接口”翻译到“BCP 串口协议”。

状态说明：

- `已完成`：当前代码已落地并可用
- `部分完成`：已有真实映射，但仍有字段或语义未完全对齐
- `保留占位`：接口已保留，但当前仍不是实时真实业务数据
- `未完全定义`：已登记到 `NOTDEFINE.md`

## 当前总原则

- 对上仍是北极熊 ROS 接口
- 对下改成纯 BCP
- 内部允许多帧拆分、回退来源、兼容观测 topic
- 不做“单独兼容节点替身”方案

## 发送方向映射

| 北极熊输入 | 当前兼容层处理 | BCP 落点 | 状态 | 备注 |
| --- | --- | --- | --- | --- |
| `cmd_vel` | 缓存 `vx/vy/wz` | `0x12 chassis_ctrl` | 已完成 | 静止时兼容旧包特殊值 `[0,0,1,0,0,0]` |
| `cmd_gimbal` | `gimbal_manager` 先转 `cmd_gimbal_joint` | 内部桥接 | 已完成 | 上层 `cmd_gimbal` 没丢 |
| `cmd_gimbal_joint` | 按 joint 名提取 pitch/yaw | `0x20 gimbal` | 已完成 | 当前仍保留 direct joint 入口 |
| `cmd_shoot` | `fire -> is_shoot`，默认 `fric_on=true` | `0x40 barrel` | 已完成 | `bullet_vel/remain_bullet` 仍用兼容参数默认值 |
| `tracker/target` | 保留 `tracking` 缓存 | 暂无稳定直接落点 | 未完全定义 | 当前不删入口，但线协议承载仍待确认 |

## 发送字段级现状

### 1. `cmd_vel -> 0x12 chassis_ctrl`

当前映射：

- `linear.x -> chassis_target_linear_x`
- `linear.y -> chassis_target_linear_y`
- `angular.z -> chassis_target_angular_z`
- 其余字段补 `0`

当前缩放：

- `* 10000`

已保留的旧包兼容行为：

- 全静止时发送 `[0, 0, 1, 0, 0, 0]`

### 2. `cmd_gimbal_joint -> 0x20 gimbal`

当前映射：

- `mode -> bcp_gimbal_ctrl_mode`
- `yaw -> gimbal_yaw`
- `pitch -> gimbal_pitch`

当前缩放：

- `yaw/pitch * 1000`

当前兼容说明：

- 第 4、5 位仍需继续和旧包现网复用语义完全对齐：
  - 第 4 位历史上被当作 `fire_advice`
  - 第 5 位历史上被当作 `has_target`

### 3. `cmd_shoot -> 0x40 barrel`

当前映射：

- `cmd_shoot.data -> is_shoot`
- `bullet_vel -> bcp_default_bullet_vel`
- `remain_bullet -> bcp_default_remain_bullet`

当前结论：

- 已能打通开火控制
- 但 `bullet_vel`、`remain_bullet` 仍属于兼容默认值，不是最终业务真值

## 接收方向映射

| 北极熊输出 | 当前 BCP 来源 | 当前处理 | 状态 | 备注 |
| --- | --- | --- | --- | --- |
| `serial/imu` | `0x13 chassis_imu` 或 `0x20 gimbal` 回退 | 发布 `sensor_msgs/Imu` | 已完成 | 当前现网常见姿态来源更接近 `0x20` 回退 |
| `serial/receive` | 同 `serial/imu` | 发布 `Vector3(yaw/pitch/roll deg)` | 已完成 | 这是新增兼容观测输出 |
| `serial/gimbal_joint_state` | `0x20 gimbal` + IMU/odom 缓存 | 发布 `JointState` | 已完成 | 保持 `joint_state_publisher` 可消费 |
| `serial/robot_motion` | `0x11 chassis_odom` | 发布 `Twist` | 已完成 | 速度反馈已接通 |
| `referee/game_status` | `0x30 game_status` | 直接转发 | 已完成 | 字段同类 |
| `referee/all_robot_hp` | `0x31 robot_HP` | 直接转发 | 已完成 | 现网已验证有真实上行 |
| `referee/event_data` | `0x32 ICRA_buff_debuff_zone` | 部分映射 | 部分完成 | F1-F5 已接入第一版 |
| `referee/rfid_status` | `0x32 ICRA_buff_debuff_zone` | 部分映射 | 部分完成 | 高地/补给区相关位已接入第一版 |
| `serial/robot_state_info` | 无稳定直接来源 | 一次性占位发布 | 保留占位 | 不删 publisher |
| `referee/ground_robot_position` | 无稳定直接来源 | 一次性占位发布 | 保留占位 | 不删 publisher |
| `referee/robot_status` | 无稳定直接来源 | 一次性占位发布 | 保留占位 | 不删 publisher |
| `referee/buff` | `0x32` 语义仍未定 | 一次性占位发布 | 保留占位 | 先通过 debug 暴露剩余字段 |
| `serial/debug/<name>` | 接收链路观测 + 剩余 ICRA 观测 | 动态创建 publisher | 已完成 | 不是原北极熊调试帧同构替代，但机制保留 |

## 关于 `serial/imu` 的当前真实语义

这部分是当前最容易误解的地方。

当前结论：

- `serial/imu` 是标准 `sensor_msgs/Imu`
- 它的 `orientation.x/y/z/w` 是四元数，不是欧拉角
- 如果你想看和旧包一致的角度观测，请优先看：
  - `serial/receive`

当前 `serial/receive` 语义：

- `x = yaw_deg`
- `y = pitch_deg`
- `z = roll_deg`

## 当前已知现网帧特点

目标环境直接抓串口原始字节后，当前已确认：

- 实际稳定上行中常见帧头为：
  - `ff 01 20 19 ...`
- 也就是：
  - `d_addr = 0x01`
  - `id = 0x20`
  - `len = 0x19`

这也是当前把 `0x20 gimbal` 作为姿态回退来源的主要依据。

## 当前仍未完全收敛的项

这些项不允许删接口，但当前仍未完全收敛：

- `tracker/target.tracking` 的线协议承载方式
- `gimbal` 第 4、5 位与旧包现网复用语义的最终对齐
- `barrel.bullet_vel` 与 `barrel.remain_bullet` 的真实数据源
- `referee/buff` 的正式语义落点
- `referee/event_data` / `referee/rfid_status` 中尚未映射的剩余字段

这些项统一继续维护在：

- `NOTDEFINE.md`

## 当前一句话结论

当前兼容层已经做到：

- 北极熊 ROS 接口基本不动
- 主要活动收发路径改成纯 BCP
- 对上额外补了旧包风格的姿态观测

当前兼容层还没有完全做完的，是：

- 所有裁判系统细项与非同构状态项的最终语义落地
