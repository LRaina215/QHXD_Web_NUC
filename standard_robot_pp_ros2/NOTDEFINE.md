# 未完全定义项登记

## 文档目的

本文件只记录一类内容：

- 北极熊接口必须保留
- 但当前 BCP 中还没有完全同构的直接语义
- 或虽然已部分落地，但仍不是最终真实业务定义

处理原则：

- 不删除接口
- 不减少 topic
- 不减少输入入口
- 当前能真实映射的先真实映射
- 当前不能真实映射的先占位，并明确后续补齐方向

## 发送侧未完全定义项

### 1. `tracker/target.tracking`

当前情况：

- 上层输入入口仍保留
- 兼容层里也继续缓存 `tracking`
- 但当前 BCP 中还没有确认稳定直接承载方式

当前策略：

- 不删除订阅
- 不删除缓存位
- 后续判断它最终应落在哪个 BCP 扩展字段或控制语义中

### 2. `barrel.bullet_vel`

当前情况：

- `cmd_shoot -> 0x40 barrel` 已打通
- 但 `bullet_vel` 当前仍使用兼容默认参数

当前策略：

- 保留开火控制链路
- 后续找到真实来源后替换默认值

### 3. `barrel.remain_bullet`

当前情况：

- 当前仍使用兼容默认参数

当前策略：

- 不删接口
- 后续确认是否来自裁判系统、弹仓计数或 MCU 反馈缓存

### 4. `gimbal` 第 4、5 位复用语义

当前情况：

- 旧包现网中：
  - 第 4 位更像 `fire_advice`
  - 第 5 位更像 `has_target`
- 当前 C++ 版尚未完全把这层历史复用语义做成最终版

当前策略：

- 继续保留兼容思路
- 在真正完全对齐前，不擅自删掉该历史行为

### 5. 北极熊聚合发包模型

当前情况：

- 北极熊原模型更像“聚合缓存后统一发送”
- 当前 BCP 实际落地为多帧拆分发送

当前策略：

- 对上继续保留聚合控制体验
- 对下继续由兼容层完成拆帧

这不是上层接口问题，但仍属于兼容层必须长期承担的语义债。

## 接收侧未完全定义项

### 1. `serial/robot_state_info`

当前情况：

- publisher 已保留
- 当前仍以一次性占位发布为主
- 还没有稳定、同构的 BCP 直接来源

### 2. `referee/ground_robot_position`

当前情况：

- publisher 已保留
- 当前仍以一次性占位发布为主

### 3. `referee/robot_status`

当前情况：

- publisher 已保留
- 当前仍以一次性占位发布为主

### 4. `referee/buff`

当前情况：

- publisher 已保留
- 当前仍是一次性占位发布
- `ICRA_buff_debuff_zone.*_buff_debuff_status` 和北极熊 `Buff` 各字段之间，还没有最终稳定映射

当前策略：

- 先不删接口
- 先保留 debug 观测值
- 后续拿真实场地语义再正式落地

### 5. `referee/event_data` 的剩余字段

当前已完成部分映射：

- `F1 -> non_overlapping_supply_zone`
- `F2 -> overlapping_supply_zone`
- `F3 -> central_highland`
- `F4 -> trapezoidal_highland`
- `F5 -> center_gain_zone`

当前仍未完全定义：

- `supply_zone`
- `small_energy`
- `big_energy`

### 6. `referee/rfid_status` 的剩余字段

当前已完成部分映射：

- 补给区相关位
- 高地相关位
- 中心增益区相关位

当前仍未完全定义：

- 其余没有稳定同构来源的字段

### 7. `serial/debug/<name>` 与原北极熊调试帧的完全等价关系

当前情况：

- 动态 debug publisher 机制已保留
- 当前已经有：
  - BCP 接收诊断
  - ICRA 剩余字段观测
  - 串口重连与打开配置观测

当前仍未完全定义：

- 它并不是原北极熊 `ReceiveDebugData` 的逐字段同构替代
- 后续如果需要完全贴近原调试包命名，再继续扩展

## 当前不再归类为“完全未定义”的项

这些项已经不是“完全没有真实来源”，因此不再按纯占位项看待：

- `serial/imu`
- `serial/gimbal_joint_state`
- `serial/robot_motion`
- `referee/game_status`
- `referee/all_robot_hp`
- `referee/event_data` 的已映射部分
- `referee/rfid_status` 的已映射部分
- `serial/receive`

## 当前一句话结论

当前真正仍需继续清债的，不是“接口有没有”，而是：

- 哪些字段已经是真值
- 哪些字段只是部分真值
- 哪些字段还只是接口占位
