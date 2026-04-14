// Copyright 2025 SMBU-PolarBear-Robotics-Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "standard_robot_pp_ros2/standard_robot_pp_ros2.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <termios.h>
#include <unistd.h>

#include "standard_robot_pp_ros2/crc8_crc16.hpp"
#include "standard_robot_pp_ros2/packet_typedef.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"

#define USB_NOT_OK_SLEEP_TIME 1000   // (ms)
#define USB_PROTECT_SLEEP_TIME 1000  // (ms)
#define USB_OPEN_STABILIZE_SLEEP_TIME 200  // (ms)
#define BCP_RX_TIMEOUT_MS 1500  // (ms)
#define BCP_INITIAL_RX_GRACE_PERIOD_MS 12000  // (ms)

using namespace std::chrono_literals;

namespace standard_robot_pp_ros2
{
namespace
{
constexpr uint8_t BCP_HEAD = 0xFF;
constexpr uint8_t BCP_ID_CHASSIS_ODOM = 0x11;
constexpr uint8_t BCP_ID_CHASSIS_IMU = 0x13;
constexpr uint8_t BCP_ID_CHASSIS_CTRL = 0x12;
constexpr uint8_t BCP_ID_GIMBAL = 0x20;
constexpr uint8_t BCP_ID_GAME_STATUS = 0x30;
constexpr uint8_t BCP_ID_ROBOT_HP = 0x31;
constexpr uint8_t BCP_ID_ICRA_ZONE = 0x32;
constexpr uint8_t BCP_ID_BARREL = 0x40;
constexpr double BCP_PI = 3.14159265358979323846;
constexpr int64_t BCP_CMD_ACTIVE_TIMEOUT_MS = 300;
constexpr int64_t BCP_SEND_INTERVAL_MS = 10;

template <typename T>
void appendLittleEndian(std::vector<uint8_t> & payload, T value)
{
  const auto * raw = reinterpret_cast<const uint8_t *>(&value);
  payload.insert(payload.end(), raw, raw + sizeof(T));
}

template <typename T>
T readLittleEndian(const std::vector<uint8_t> & payload, size_t offset)
{
  T value{};
  if (offset + sizeof(T) > payload.size()) {
    return value;
  }
  std::memcpy(&value, payload.data() + offset, sizeof(T));
  return value;
}

uint8_t clampOccupiedState(uint8_t value)
{
  return std::min<uint8_t>(value, 3U);
}

bool isFriendlyActive(uint8_t occupied_state)
{
  return occupied_state == 1U || occupied_state == 3U;
}

bool isEnemyActive(uint8_t occupied_state)
{
  return occupied_state == 2U || occupied_state == 3U;
}

bool isFiniteDouble(double value)
{
  return std::isfinite(value);
}

bool isPlausibleBcpImuSample(
  double yaw_deg, double pitch_deg, double roll_deg, double angle_x_deg, double angle_y_deg,
  double angle_z_deg, double acc_x, double acc_y, double acc_z)
{
  return isFiniteDouble(yaw_deg) && isFiniteDouble(pitch_deg) && isFiniteDouble(roll_deg) &&
         isFiniteDouble(angle_x_deg) && isFiniteDouble(angle_y_deg) &&
         isFiniteDouble(angle_z_deg) && isFiniteDouble(acc_x) && isFiniteDouble(acc_y) &&
         isFiniteDouble(acc_z) && std::abs(yaw_deg) < 1000.0 && std::abs(pitch_deg) < 1000.0 &&
         std::abs(roll_deg) < 1000.0 && std::abs(angle_x_deg) < 10000.0 &&
         std::abs(angle_y_deg) < 10000.0 && std::abs(angle_z_deg) < 10000.0 &&
         std::abs(acc_x) < 500.0 && std::abs(acc_y) < 500.0 && std::abs(acc_z) < 500.0;
}

int64_t nowSteadyMs()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
           std::chrono::steady_clock::now().time_since_epoch())
    .count();
}

speed_t toTermiosBaud(uint32_t baud_rate)
{
  switch (baud_rate) {
    case 9600:
      return B9600;
    case 19200:
      return B19200;
    case 38400:
      return B38400;
    case 57600:
      return B57600;
    case 115200:
      return B115200;
    default:
      return B115200;
  }
}
}  // namespace


StandardRobotPpRos2Node::StandardRobotPpRos2Node(const rclcpp::NodeOptions & options)
: Node("StandardRobotPpRos2Node", options),
  owned_ctx_{new IoContext(2)},
  serial_driver_{new drivers::serial_driver::SerialDriver(*owned_ctx_)}
{
  RCLCPP_INFO(get_logger(), "Start StandardRobotPpRos2Node!");

  getParams();
  createPublisher();
  createSubscription();

  robot_models_.chassis = {
    {0, "无底盘"}, {1, "麦轮底盘"}, {2, "全向轮底盘"}, {3, "舵轮底盘"}, {4, "平衡底盘"}};
  robot_models_.gimbal = {{0, "无云台"}, {1, "yaw_pitch直连云台"}};
  robot_models_.shoot = {{0, "无发射机构"}, {1, "摩擦轮+拨弹盘"}, {2, "气动+拨弹盘"}};
  robot_models_.arm = {{0, "无机械臂"}, {1, "mini机械臂"}};
  robot_models_.custom_controller = {{0, "无自定义控制器"}, {1, "mini自定义控制器"}};

  // For outputs that have no direct BCP source yet, publish a one-shot placeholder so the
  // ROS interface remains intact without continuously injecting fake runtime data.
  publishUndefinedPlaceholders();

  serial_port_protect_thread_ = std::thread(&StandardRobotPpRos2Node::serialPortProtect, this);
  receive_thread_ = std::thread(&StandardRobotPpRos2Node::receiveData, this);
  send_thread_ = std::thread(&StandardRobotPpRos2Node::sendData, this);
}

StandardRobotPpRos2Node::~StandardRobotPpRos2Node()
{
  if (send_thread_.joinable()) {
    send_thread_.join();
  }

  if (receive_thread_.joinable()) {
    receive_thread_.join();
  }

  if (serial_port_protect_thread_.joinable()) {
    serial_port_protect_thread_.join();
  }

  if (serial_driver_->port()->is_open()) {
    serial_driver_->port()->close();
  }

  if (owned_ctx_) {
    owned_ctx_->waitForExit();
  }
}

void StandardRobotPpRos2Node::createPublisher()
{
  imu_pub_ = this->create_publisher<sensor_msgs::msg::Imu>("serial/imu", 10);
  serial_receive_pub_ = this->create_publisher<geometry_msgs::msg::Vector3>("serial/receive", 10);
  robot_state_info_pub_ =
    this->create_publisher<pb_rm_interfaces::msg::RobotStateInfo>("serial/robot_state_info", 10);
  joint_state_pub_ =
    this->create_publisher<sensor_msgs::msg::JointState>("serial/gimbal_joint_state", 10);
  robot_motion_pub_ = this->create_publisher<geometry_msgs::msg::Twist>("serial/robot_motion", 10);
  event_data_pub_ =
    this->create_publisher<pb_rm_interfaces::msg::EventData>("referee/event_data", 10);
  all_robot_hp_pub_ =
    this->create_publisher<pb_rm_interfaces::msg::GameRobotHP>("referee/all_robot_hp", 10);
  game_status_pub_ =
    this->create_publisher<pb_rm_interfaces::msg::GameStatus>("referee/game_status", 10);
  ground_robot_position_pub_ = this->create_publisher<pb_rm_interfaces::msg::GroundRobotPosition>(
    "referee/ground_robot_position", 10);
  rfid_status_pub_ =
    this->create_publisher<pb_rm_interfaces::msg::RfidStatus>("referee/rfid_status", 10);
  robot_status_pub_ =
    this->create_publisher<pb_rm_interfaces::msg::RobotStatus>("referee/robot_status", 10);
  buff_pub_ = this->create_publisher<pb_rm_interfaces::msg::Buff>("referee/buff", 10);
}

void StandardRobotPpRos2Node::createNewDebugPublisher(const std::string & name)
{
  RCLCPP_INFO(get_logger(), "Create new debug publisher: %s", name.c_str());
  std::string topic_name = "serial/debug/" + name;
  auto debug_pub = this->create_publisher<example_interfaces::msg::Float64>(topic_name, 10);
  debug_pub_map_.insert(std::make_pair(name, debug_pub));
}

void StandardRobotPpRos2Node::publishNamedDebugValue(const std::string & name, double value)
{
  if (debug_pub_map_.find(name) == debug_pub_map_.end()) {
    createNewDebugPublisher(name);
  }

  example_interfaces::msg::Float64 msg;
  msg.data = value;
  debug_pub_map_.at(name)->publish(msg);
}

void StandardRobotPpRos2Node::publishBcpImuFromEulerDegrees(
  double yaw_deg, double pitch_deg, double roll_deg, double angle_x_deg, double angle_y_deg,
  double angle_z_deg, double acc_x, double acc_y, double acc_z)
{
  geometry_msgs::msg::Vector3 serial_receive_msg;
  serial_receive_msg.x = yaw_deg;
  serial_receive_msg.y = pitch_deg;
  serial_receive_msg.z = roll_deg;
  serial_receive_pub_->publish(serial_receive_msg);

  sensor_msgs::msg::Imu imu_msg;
  imu_msg.header.stamp = now();
  imu_msg.header.frame_id = "gimbal_pitch_odom";

  tf2::Quaternion q;
  q.setRPY(
    roll_deg * BCP_PI / 180.0,
    -pitch_deg * BCP_PI / 180.0,
    yaw_deg * BCP_PI / 180.0);
  imu_msg.orientation = tf2::toMsg(q);
  imu_msg.angular_velocity.x = angle_x_deg * BCP_PI / 180.0;
  imu_msg.angular_velocity.y = angle_y_deg * BCP_PI / 180.0;
  imu_msg.angular_velocity.z = angle_z_deg * BCP_PI / 180.0;
  imu_msg.linear_acceleration.x = acc_x;
  imu_msg.linear_acceleration.y = acc_y;
  imu_msg.linear_acceleration.z = acc_z;
  imu_pub_->publish(imu_msg);
}

void StandardRobotPpRos2Node::createSubscription()
{
  cmd_vel_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
    "cmd_vel", 10,
    std::bind(&StandardRobotPpRos2Node::cmdVelCallback, this, std::placeholders::_1));

  cmd_gimbal_joint_sub_ = this->create_subscription<sensor_msgs::msg::JointState>(
    "cmd_gimbal_joint", 10,
    std::bind(&StandardRobotPpRos2Node::cmdGimbalJointCallback, this, std::placeholders::_1));

  cmd_shoot_sub_ = this->create_subscription<example_interfaces::msg::UInt8>(
    "cmd_shoot", 10,
    std::bind(&StandardRobotPpRos2Node::cmdShootCallback, this, std::placeholders::_1));
  cmd_tracking_sub_ = this->create_subscription<auto_aim_interfaces::msg::Target>(
    "tracker/target", 10,
    std::bind(&StandardRobotPpRos2Node::visionTargetCallback, this, std::placeholders::_1));
}

void StandardRobotPpRos2Node::getParams()
{
  using FlowControl = drivers::serial_driver::FlowControl;
  using Parity = drivers::serial_driver::Parity;
  using StopBits = drivers::serial_driver::StopBits;

  uint32_t baud_rate{};
  auto fc = FlowControl::NONE;
  auto pt = Parity::NONE;
  auto sb = StopBits::ONE;

  try {
    device_name_ = declare_parameter<std::string>("device_name", "");
  } catch (rclcpp::ParameterTypeException & ex) {
    RCLCPP_ERROR(get_logger(), "The device name provided was invalid");
    throw ex;
  }

  try {
    baud_rate = declare_parameter<int>("baud_rate", 0);
  } catch (rclcpp::ParameterTypeException & ex) {
    RCLCPP_ERROR(get_logger(), "The baud_rate provided was invalid");
    throw ex;
  }

  try {
    const auto fc_string = declare_parameter<std::string>("flow_control", "");

    if (fc_string == "none") {
      fc = FlowControl::NONE;
    } else if (fc_string == "hardware") {
      fc = FlowControl::HARDWARE;
    } else if (fc_string == "software") {
      fc = FlowControl::SOFTWARE;
    } else {
      throw std::invalid_argument{
        "The flow_control parameter must be one of: none, software, or hardware."};
    }
  } catch (rclcpp::ParameterTypeException & ex) {
    RCLCPP_ERROR(get_logger(), "The flow_control provided was invalid");
    throw ex;
  }

  try {
    const auto pt_string = declare_parameter<std::string>("parity", "");

    if (pt_string == "none") {
      pt = Parity::NONE;
    } else if (pt_string == "odd") {
      pt = Parity::ODD;
    } else if (pt_string == "even") {
      pt = Parity::EVEN;
    } else {
      throw std::invalid_argument{"The parity parameter must be one of: none, odd, or even."};
    }
  } catch (rclcpp::ParameterTypeException & ex) {
    RCLCPP_ERROR(get_logger(), "The parity provided was invalid");
    throw ex;
  }

  try {
    const auto sb_string = declare_parameter<std::string>("stop_bits", "");

    if (sb_string == "1" || sb_string == "1.0") {
      sb = StopBits::ONE;
    } else if (sb_string == "1.5") {
      sb = StopBits::ONE_POINT_FIVE;
    } else if (sb_string == "2" || sb_string == "2.0") {
      sb = StopBits::TWO;
    } else {
      throw std::invalid_argument{"The stop_bits parameter must be one of: 1, 1.5, or 2."};
    }
  } catch (rclcpp::ParameterTypeException & ex) {
    RCLCPP_ERROR(get_logger(), "The stop_bits provided was invalid");
    throw ex;
  }

  device_config_ =
    std::make_unique<drivers::serial_driver::SerialPortConfig>(baud_rate, fc, pt, sb);

  record_rosbag_ = declare_parameter("record_rosbag", false);
  set_detector_color_ = declare_parameter("set_detector_color", false);
  debug_ = declare_parameter("debug", false);
  bcp_d_addr_ = static_cast<uint8_t>(declare_parameter("bcp_d_addr", 0x03));
  bcp_rx_addr_ = static_cast<uint8_t>(declare_parameter("bcp_rx_addr", 0x01));
  bcp_gimbal_ctrl_mode_ =
    static_cast<uint8_t>(declare_parameter("bcp_gimbal_ctrl_mode", 1));
  bcp_default_bullet_vel_ = declare_parameter("bcp_default_bullet_vel", 15);
  bcp_default_remain_bullet_ =
    static_cast<int16_t>(declare_parameter("bcp_default_remain_bullet", 0));
}

/********************************************************/
/* Serial port protect                                  */
/********************************************************/
bool StandardRobotPpRos2Node::configureSerialPortTermios()
{
  const int fd = ::open(device_name_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    RCLCPP_WARN(get_logger(), "Pre-open termios configure skipped: open %s failed", device_name_.c_str());
    return false;
  }

  termios tio {};
  if (::tcgetattr(fd, &tio) != 0) {
    ::close(fd);
    RCLCPP_WARN(get_logger(), "Pre-open termios configure skipped: tcgetattr failed");
    return false;
  }

  ::cfmakeraw(&tio);
  tio.c_cflag |= (CLOCAL | CREAD);
  tio.c_cflag &= ~HUPCL;
  tio.c_cflag &= ~CRTSCTS;

  const auto baud = toTermiosBaud(device_config_->get_baud_rate());
  ::cfsetispeed(&tio, baud);
  ::cfsetospeed(&tio, baud);

  const bool ok = ::tcsetattr(fd, TCSANOW, &tio) == 0;
  if (ok) {
    ::tcflush(fd, TCIOFLUSH);
    if (debug_) {
      publishNamedDebugValue("serial_termios_configured", 1.0);
    }
  } else {
    RCLCPP_WARN(get_logger(), "Pre-open termios configure skipped: tcsetattr failed");
  }

  ::close(fd);
  return ok;
}

bool StandardRobotPpRos2Node::openSerialPort(bool reopen)
{
  try {
    std::lock_guard<std::mutex> tx_lock(serial_port_tx_mutex_);
    configureSerialPortTermios();
    if (serial_driver_->port()->is_open()) {
      serial_driver_->port()->close();
      std::this_thread::sleep_for(std::chrono::milliseconds(USB_OPEN_STABILIZE_SLEEP_TIME));
    }

    serial_driver_->port()->open();
    std::this_thread::sleep_for(std::chrono::milliseconds(USB_OPEN_STABILIZE_SLEEP_TIME));

    const auto now_ms = nowSteadyMs();
    serial_open_time_ms_.store(now_ms);
    last_rx_success_time_ms_.store(now_ms);

    if (reopen) {
      const auto reopen_count = serial_reopen_count_.fetch_add(1) + 1;
      RCLCPP_WARN(
        get_logger(), "Serial port reopened after rx stall. Reopen count: %u", reopen_count);
      if (debug_) {
        publishNamedDebugValue("serial_reopen_count", static_cast<double>(reopen_count));
      }
    } else {
      RCLCPP_INFO(get_logger(), "Serial port opened!");
    }

    is_usb_ok_ = true;
    return true;
  } catch (const std::exception & ex) {
    is_usb_ok_ = false;
    RCLCPP_ERROR(get_logger(), "Open serial port failed : %s", ex.what());
    return false;
  }
}

void StandardRobotPpRos2Node::serialPortProtect()
{
  RCLCPP_INFO(get_logger(), "Start serialPortProtect!");

  // @TODO: 1.保持串口连接 2.串口断开重连 3.串口异常处理

  // 初始化串口
  serial_driver_->init_port(device_name_, *device_config_);
  openSerialPort(false);

  std::this_thread::sleep_for(std::chrono::milliseconds(USB_PROTECT_SLEEP_TIME));

  while (rclcpp::ok()) {
    if (!is_usb_ok_) {
      openSerialPort(true);
    } else {
      const auto now_ms = nowSteadyMs();
      const auto last_rx_ms = last_rx_success_time_ms_.load();
      const auto open_ms = serial_open_time_ms_.load();
      const auto rx_idle_ms = now_ms - last_rx_ms;
      const auto time_since_open_ms = now_ms - open_ms;

      if (debug_) {
        publishNamedDebugValue("bcp_rx_idle_ms", static_cast<double>(rx_idle_ms));
      }

      if (
        last_rx_ms > 0 && time_since_open_ms > BCP_INITIAL_RX_GRACE_PERIOD_MS &&
        rx_idle_ms > BCP_RX_TIMEOUT_MS)
      {
        RCLCPP_WARN(
          get_logger(), "No valid BCP frame for %lld ms, forcing serial reopen.",
          static_cast<long long>(rx_idle_ms));
        is_usb_ok_ = false;
        openSerialPort(true);
      }
    }

    // thread sleep
    std::this_thread::sleep_for(std::chrono::milliseconds(USB_PROTECT_SLEEP_TIME));
  }
}

/********************************************************/
/* Receive data                                         */
/********************************************************/

void StandardRobotPpRos2Node::receiveData()
{
  RCLCPP_INFO(get_logger(), "Start receiveData!");

  int retry_count = 0;

  while (rclcpp::ok()) {
    if (!is_usb_ok_) {
      RCLCPP_WARN(get_logger(), "receive: usb is not ok! Retry count: %d", retry_count++);
      std::this_thread::sleep_for(std::chrono::milliseconds(USB_NOT_OK_SLEEP_TIME));
      continue;
    }

    try {
      std::vector<uint8_t> frame;
      if (!receiveBcpFrame(frame)) {
        continue;
      }
      last_rx_success_time_ms_.store(nowSteadyMs());
      handleBcpFrame(frame);
    } catch (const std::exception & ex) {
      RCLCPP_ERROR(get_logger(), "Error receiving data: %s", ex.what());
      is_usb_ok_ = false;
    }
  }
}

void StandardRobotPpRos2Node::publishDebugData(ReceiveDebugData & received_debug_data)
{
  static rclcpp::Publisher<example_interfaces::msg::Float64>::SharedPtr debug_pub;
  for (auto & package : received_debug_data.packages) {
    // Create a vector to hold the non-zero data
    std::vector<uint8_t> non_zero_data;
    for (unsigned char name : package.name) {
      if (name != 0) {
        non_zero_data.push_back(name);
      } else {
        break;
      }
    }
    // Convert the non-zero data to a string
    std::string name(non_zero_data.begin(), non_zero_data.end());

    if (name.empty()) {
      continue;
    }

    if (debug_pub_map_.find(name) == debug_pub_map_.end()) {
      createNewDebugPublisher(name);
    }
    debug_pub = debug_pub_map_.at(name);

    example_interfaces::msg::Float64 msg;
    msg.data = package.data;
    debug_pub->publish(msg);
  }
}

void StandardRobotPpRos2Node::publishImuData(ReceiveImuData & imu_data)
{
  sensor_msgs::msg::JointState joint_msg;
  sensor_msgs::msg::Imu imu_msg;
  imu_msg.header.stamp = joint_msg.header.stamp = now();
  imu_msg.header.frame_id = "gimbal_pitch_odom";

  // Convert Euler angles to quaternion
  tf2::Quaternion q;
  q.setRPY(imu_data.data.roll, imu_data.data.pitch, imu_data.data.yaw);
  imu_msg.orientation = tf2::toMsg(q);
  imu_msg.angular_velocity.x = imu_data.data.roll_vel;
  imu_msg.angular_velocity.y = imu_data.data.pitch_vel;
  imu_msg.angular_velocity.z = imu_data.data.yaw_vel;
  imu_pub_->publish(imu_msg);

  joint_msg.name = {
    "gimbal_pitch_joint",
    "gimbal_yaw_joint",
    "gimbal_pitch_odom_joint",
    "gimbal_yaw_odom_joint",
  };
  joint_msg.position = {
    imu_data.data.pitch,
    imu_data.data.yaw,
    last_gimbal_pitch_odom_joint_,
    last_gimbal_yaw_odom_joint_,
  };
  joint_state_pub_->publish(joint_msg);
}

void StandardRobotPpRos2Node::publishRobotInfo(ReceiveRobotInfoData & robot_info)
{
  pb_rm_interfaces::msg::RobotStateInfo msg;

  msg.header.stamp.sec = robot_info.time_stamp / 1000;
  msg.header.stamp.nanosec = (robot_info.time_stamp % 1000) * 1e6;
  msg.header.frame_id = "odom";

  msg.models.chassis = robot_models_.chassis.at(robot_info.data.type.chassis);
  msg.models.gimbal = robot_models_.gimbal.at(robot_info.data.type.gimbal);
  msg.models.shoot = robot_models_.shoot.at(robot_info.data.type.shoot);
  msg.models.arm = robot_models_.arm.at(robot_info.data.type.arm);
  msg.models.custom_controller =
    robot_models_.custom_controller.at(robot_info.data.type.custom_controller);

  robot_state_info_pub_->publish(msg);
}

void StandardRobotPpRos2Node::publishEventData(ReceiveEventData & event_data)
{
  pb_rm_interfaces::msg::EventData msg;

  msg.non_overlapping_supply_zone = event_data.data.non_overlapping_supply_zone;
  msg.overlapping_supply_zone = event_data.data.overlapping_supply_zone;
  msg.supply_zone = event_data.data.supply_zone;

  msg.small_energy = event_data.data.small_energy;
  msg.big_energy = event_data.data.big_energy;

  msg.central_highland = event_data.data.central_highland;
  msg.trapezoidal_highland = event_data.data.trapezoidal_highland;

  msg.center_gain_zone = event_data.data.center_gain_zone;

  event_data_pub_->publish(msg);
}

void StandardRobotPpRos2Node::publishAllRobotHp(ReceiveAllRobotHpData & all_robot_hp)
{
  pb_rm_interfaces::msg::GameRobotHP msg;

  msg.red_1_robot_hp = all_robot_hp.data.red_1_robot_hp;
  msg.red_2_robot_hp = all_robot_hp.data.red_2_robot_hp;
  msg.red_3_robot_hp = all_robot_hp.data.red_3_robot_hp;
  msg.red_4_robot_hp = all_robot_hp.data.red_4_robot_hp;
  msg.red_7_robot_hp = all_robot_hp.data.red_7_robot_hp;
  msg.red_outpost_hp = all_robot_hp.data.red_outpost_hp;
  msg.red_base_hp = all_robot_hp.data.red_base_hp;

  msg.blue_1_robot_hp = all_robot_hp.data.blue_1_robot_hp;
  msg.blue_2_robot_hp = all_robot_hp.data.blue_2_robot_hp;
  msg.blue_3_robot_hp = all_robot_hp.data.blue_3_robot_hp;
  msg.blue_4_robot_hp = all_robot_hp.data.blue_4_robot_hp;
  msg.blue_7_robot_hp = all_robot_hp.data.blue_7_robot_hp;
  msg.blue_outpost_hp = all_robot_hp.data.blue_outpost_hp;
  msg.blue_base_hp = all_robot_hp.data.blue_base_hp;

  all_robot_hp_pub_->publish(msg);
}

void StandardRobotPpRos2Node::publishGameStatus(ReceiveGameStatusData & game_status)
{
  pb_rm_interfaces::msg::GameStatus msg;
  msg.game_progress = game_status.data.game_progress;
  msg.stage_remain_time = game_status.data.stage_remain_time;
  game_status_pub_->publish(msg);

  if (record_rosbag_ && game_status.data.game_progress != previous_game_progress_) {
    previous_game_progress_ = game_status.data.game_progress;
    RCLCPP_INFO(get_logger(), "Game progress: %d", game_status.data.game_progress);

    std::string service_name;
    switch (game_status.data.game_progress) {
      case pb_rm_interfaces::msg::GameStatus::COUNT_DOWN:
        service_name = "start_recording";
        break;
      case pb_rm_interfaces::msg::GameStatus::GAME_OVER:
        service_name = "stop_recording";
        break;
      default:
        return;
    }

    if (!callTriggerService(service_name)) {
      RCLCPP_ERROR(get_logger(), "Failed to call service: %s", service_name.c_str());
    }
  }
}

void StandardRobotPpRos2Node::publishRobotMotion(ReceiveRobotMotionData & robot_motion)
{
  geometry_msgs::msg::Twist msg;

  msg.linear.x = robot_motion.data.speed_vector.vx;
  msg.linear.y = robot_motion.data.speed_vector.vy;
  msg.angular.z = robot_motion.data.speed_vector.wz;

  robot_motion_pub_->publish(msg);
}

void StandardRobotPpRos2Node::publishGroundRobotPosition(
  ReceiveGroundRobotPosition & ground_robot_position)
{
  pb_rm_interfaces::msg::GroundRobotPosition msg;

  msg.hero_position.x = ground_robot_position.data.hero_x;
  msg.hero_position.y = ground_robot_position.data.hero_y;

  msg.engineer_position.x = ground_robot_position.data.engineer_x;
  msg.engineer_position.y = ground_robot_position.data.engineer_y;

  msg.standard_3_position.x = ground_robot_position.data.standard_3_x;
  msg.standard_3_position.y = ground_robot_position.data.standard_3_y;

  msg.standard_4_position.x = ground_robot_position.data.standard_4_x;
  msg.standard_4_position.y = ground_robot_position.data.standard_4_y;

  ground_robot_position_pub_->publish(msg);
}

void StandardRobotPpRos2Node::publishRfidStatus(ReceiveRfidStatus & rfid_status)
{
  pb_rm_interfaces::msg::RfidStatus msg;

  msg.base_gain_point = rfid_status.data.base_gain_point;
  msg.central_highland_gain_point = rfid_status.data.central_highland_gain_point;
  msg.enemy_central_highland_gain_point = rfid_status.data.enemy_central_highland_gain_point;
  msg.friendly_trapezoidal_highland_gain_point =
    rfid_status.data.friendly_trapezoidal_highland_gain_point;
  msg.enemy_trapezoidal_highland_gain_point =
    rfid_status.data.enemy_trapezoidal_highland_gain_point;
  msg.friendly_fly_ramp_front_gain_point = rfid_status.data.friendly_fly_ramp_front_gain_point;
  msg.friendly_fly_ramp_back_gain_point = rfid_status.data.friendly_fly_ramp_back_gain_point;
  msg.enemy_fly_ramp_front_gain_point = rfid_status.data.enemy_fly_ramp_front_gain_point;
  msg.enemy_fly_ramp_back_gain_point = rfid_status.data.enemy_fly_ramp_back_gain_point;
  msg.friendly_central_highland_lower_gain_point =
    rfid_status.data.friendly_central_highland_lower_gain_point;
  msg.friendly_central_highland_upper_gain_point =
    rfid_status.data.friendly_central_highland_upper_gain_point;
  msg.enemy_central_highland_lower_gain_point =
    rfid_status.data.enemy_central_highland_lower_gain_point;
  msg.enemy_central_highland_upper_gain_point =
    rfid_status.data.enemy_central_highland_upper_gain_point;
  msg.friendly_highway_lower_gain_point = rfid_status.data.friendly_highway_lower_gain_point;
  msg.friendly_highway_upper_gain_point = rfid_status.data.friendly_highway_upper_gain_point;
  msg.enemy_highway_lower_gain_point = rfid_status.data.enemy_highway_lower_gain_point;
  msg.enemy_highway_upper_gain_point = rfid_status.data.enemy_highway_upper_gain_point;
  msg.friendly_fortress_gain_point = rfid_status.data.friendly_fortress_gain_point;
  msg.friendly_outpost_gain_point = rfid_status.data.friendly_outpost_gain_point;
  msg.friendly_supply_zone_non_exchange = rfid_status.data.friendly_supply_zone_non_exchange;
  msg.friendly_supply_zone_exchange = rfid_status.data.friendly_supply_zone_exchange;
  msg.friendly_big_resource_island = rfid_status.data.friendly_big_resource_island;
  msg.enemy_big_resource_island = rfid_status.data.enemy_big_resource_island;
  msg.center_gain_point = rfid_status.data.center_gain_point;

  rfid_status_pub_->publish(msg);
}

void StandardRobotPpRos2Node::publishRobotStatus(ReceiveRobotStatus & robot_status)
{
  pb_rm_interfaces::msg::RobotStatus msg;

  msg.robot_id = robot_status.data.robot_id;
  msg.robot_level = robot_status.data.robot_level;
  msg.current_hp = robot_status.data.current_up;
  msg.maximum_hp = robot_status.data.maximum_hp;
  msg.shooter_barrel_cooling_value = robot_status.data.shooter_barrel_cooling_value;
  msg.shooter_barrel_heat_limit = robot_status.data.shooter_barrel_heat_limit;
  msg.shooter_17mm_1_barrel_heat = robot_status.data.shooter_17mm_1_barrel_heat;
  msg.robot_pos.position.x = robot_status.data.robot_pos_x;
  msg.robot_pos.position.y = robot_status.data.robot_pos_y;
  msg.robot_pos.orientation =
    tf2::toMsg(tf2::Quaternion(tf2::Vector3(0, 0, 1), robot_status.data.robot_pos_angle));
  msg.armor_id = robot_status.data.armor_id;
  msg.hp_deduction_reason = robot_status.data.hp_deduction_reason;
  msg.projectile_allowance_17mm = robot_status.data.projectile_allowance_17mm;
  msg.remaining_gold_coin = robot_status.data.remaining_gold_coin;

  if (last_hp_ - msg.current_hp > 0) {
    msg.is_hp_deduced = true;
  }
  last_hp_ = robot_status.data.current_up;

  robot_status_pub_->publish(msg);

  if (set_detector_color_) {
    uint8_t detect_color;
    if (getDetectColor(robot_status.data.robot_id, detect_color)) {
      if (!initial_set_param_ || detect_color != previous_receive_color_) {
        previous_receive_color_ = detect_color;
        setParam(rclcpp::Parameter("detect_color", detect_color));
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
    }
  }
}

void StandardRobotPpRos2Node::publishJointState(ReceiveJointState & packet)
{
  last_gimbal_pitch_odom_joint_ = packet.data.pitch;
  last_gimbal_yaw_odom_joint_ = packet.data.yaw;
}

void StandardRobotPpRos2Node::publishBuff(ReceiveBuff & buff)
{
  pb_rm_interfaces::msg::Buff msg;
  msg.recovery_buff = buff.data.recovery_buff;
  msg.cooling_buff = buff.data.cooling_buff;
  msg.defence_buff = buff.data.defence_buff;
  msg.vulnerability_buff = buff.data.vulnerability_buff;
  msg.attack_buff = buff.data.attack_buff;
  msg.remaining_energy = buff.data.remaining_energy;
  buff_pub_->publish(msg);
}

void StandardRobotPpRos2Node::publishUndefinedPlaceholders()
{
  pb_rm_interfaces::msg::RobotStateInfo robot_state_info_msg;
  robot_state_info_msg.header.stamp = now();
  robot_state_info_msg.header.frame_id = "odom";
  robot_state_info_msg.models.chassis = robot_models_.chassis.at(0);
  robot_state_info_msg.models.gimbal = robot_models_.gimbal.at(0);
  robot_state_info_msg.models.shoot = robot_models_.shoot.at(0);
  robot_state_info_msg.models.arm = robot_models_.arm.at(0);
  robot_state_info_msg.models.custom_controller = robot_models_.custom_controller.at(0);
  robot_state_info_pub_->publish(robot_state_info_msg);

  pb_rm_interfaces::msg::EventData event_data_msg;
  event_data_pub_->publish(event_data_msg);

  pb_rm_interfaces::msg::GroundRobotPosition ground_robot_position_msg;
  ground_robot_position_pub_->publish(ground_robot_position_msg);

  pb_rm_interfaces::msg::RfidStatus rfid_status_msg;
  rfid_status_pub_->publish(rfid_status_msg);

  pb_rm_interfaces::msg::RobotStatus robot_status_msg;
  robot_status_msg.robot_pos.orientation.w = 1.0;
  robot_status_pub_->publish(robot_status_msg);

  pb_rm_interfaces::msg::Buff buff_msg;
  buff_pub_->publish(buff_msg);
}

/********************************************************/
/* Send data                                            */
/********************************************************/
void StandardRobotPpRos2Node::sendData()
{
  RCLCPP_INFO(get_logger(), "Start sendData!");
  sendBcpData();
}

void StandardRobotPpRos2Node::sendBcpData()
{
  RCLCPP_INFO(get_logger(), "Start sendBcpData!");

  int retry_count = 0;
  while (rclcpp::ok()) {
    if (!is_usb_ok_) {
      RCLCPP_WARN(get_logger(), "send bcp: usb is not ok! Retry count: %d", retry_count++);
      std::this_thread::sleep_for(std::chrono::milliseconds(USB_NOT_OK_SLEEP_TIME));
      continue;
    }

    const auto now_ms = nowSteadyMs();
    if (now_ms - last_cmd_update_time_ms_.load() > BCP_CMD_ACTIVE_TIMEOUT_MS) {
      std::this_thread::sleep_for(std::chrono::milliseconds(BCP_SEND_INTERVAL_MS));
      continue;
    }

    try {
      std::lock_guard<std::mutex> tx_lock(serial_port_tx_mutex_);
      serial_driver_->port()->send(buildBcpChassisCtrlFrame());
      serial_driver_->port()->send(buildBcpGimbalFrame());
      serial_driver_->port()->send(buildBcpBarrelFrame());
    } catch (const std::exception & ex) {
      RCLCPP_ERROR(get_logger(), "Error sending BCP data: %s", ex.what());
      is_usb_ok_ = false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(BCP_SEND_INTERVAL_MS));
  }
}

std::vector<uint8_t> StandardRobotPpRos2Node::buildBcpFrame(
  uint8_t id, const std::vector<uint8_t> & payload) const
{
  std::vector<uint8_t> frame;
  frame.reserve(payload.size() + 6);
  frame.push_back(BCP_HEAD);
  frame.push_back(bcp_d_addr_);
  frame.push_back(id);
  frame.push_back(static_cast<uint8_t>(payload.size()));
  frame.insert(frame.end(), payload.begin(), payload.end());

  int sumcheck = BCP_HEAD + bcp_d_addr_;
  int addcheck = BCP_HEAD + sumcheck;
  for (size_t idx = 2; idx < frame.size(); ++idx) {
    sumcheck += frame[idx];
    addcheck += sumcheck;
  }

  frame.push_back(static_cast<uint8_t>(sumcheck & 0xFF));
  frame.push_back(static_cast<uint8_t>(addcheck & 0xFF));
  return frame;
}

std::vector<uint8_t> StandardRobotPpRos2Node::buildBcpChassisCtrlFrame() const
{
  std::vector<uint8_t> payload;
  payload.reserve(6 * sizeof(int32_t));

  const int32_t linear_x = static_cast<int32_t>(std::lround(bcp_cmd_cache_.vx * 10000.0));
  const int32_t linear_y = static_cast<int32_t>(std::lround(bcp_cmd_cache_.vy * 10000.0));
  const int32_t angular_z = static_cast<int32_t>(std::lround(bcp_cmd_cache_.wz * 10000.0));
  // Keep the historical BCP "stationary" sentinel from the Python implementation.
  // The legacy bubble_protocol sent [0, 0, 1, 0, 0, 0] when the chassis command was idle.
  const int32_t linear_z = (linear_x == 0 && linear_y == 0 && angular_z == 0) ? 1 : 0;
  const int32_t angular_x = 0;
  const int32_t angular_y = 0;

  appendLittleEndian(payload, linear_x);
  appendLittleEndian(payload, linear_y);
  appendLittleEndian(payload, linear_z);
  appendLittleEndian(payload, angular_x);
  appendLittleEndian(payload, angular_y);
  appendLittleEndian(payload, angular_z);
  return buildBcpFrame(BCP_ID_CHASSIS_CTRL, payload);
}

std::vector<uint8_t> StandardRobotPpRos2Node::buildBcpGimbalFrame() const
{
  std::vector<uint8_t> payload;
  payload.reserve(sizeof(uint8_t) + 6 * sizeof(int32_t));

  const int32_t yaw = static_cast<int32_t>(std::lround(bcp_cmd_cache_.gimbal_yaw * 1000.0));
  const int32_t pitch = static_cast<int32_t>(std::lround(bcp_cmd_cache_.gimbal_pitch * 1000.0));
  const int32_t roll_or_compat = 0;
  const int32_t yaw_speed = 0;
  const int32_t pitch_speed = 0;
  const int32_t roll_speed = 0;

  appendLittleEndian(payload, bcp_gimbal_ctrl_mode_);
  appendLittleEndian(payload, yaw);
  appendLittleEndian(payload, pitch);
  appendLittleEndian(payload, roll_or_compat);
  appendLittleEndian(payload, yaw_speed);
  appendLittleEndian(payload, pitch_speed);
  appendLittleEndian(payload, roll_speed);
  return buildBcpFrame(BCP_ID_GIMBAL, payload);
}

std::vector<uint8_t> StandardRobotPpRos2Node::buildBcpBarrelFrame() const
{
  std::vector<uint8_t> payload;
  payload.reserve(sizeof(uint8_t) + sizeof(int32_t) + sizeof(int16_t));

  appendLittleEndian(payload, bcp_cmd_cache_.shoot_fire);
  appendLittleEndian(payload, bcp_default_bullet_vel_);
  appendLittleEndian(payload, bcp_default_remain_bullet_);
  return buildBcpFrame(BCP_ID_BARREL, payload);
}

bool StandardRobotPpRos2Node::receiveBcpFrame(std::vector<uint8_t> & frame)
{
  enum class RxState
  {
    WAIT_HEAD,
    WAIT_ADDR,
    WAIT_ID,
    WAIT_LEN,
    WAIT_DATA,
    WAIT_SUM,
    WAIT_ADD
  };

  auto resetParser = [&](RxState & state, uint8_t current_byte) {
    frame.clear();
    if (current_byte == BCP_HEAD) {
      frame.push_back(current_byte);
      state = RxState::WAIT_ADDR;
    } else {
      state = RxState::WAIT_HEAD;
    }
  };

  frame.clear();
  frame.reserve(64);

  RxState state = RxState::WAIT_HEAD;
  uint8_t payload_len = 0U;
  size_t payload_received = 0U;

  while (rclcpp::ok() && is_usb_ok_) {
    std::vector<uint8_t> one_byte(1, 0U);
    const int received_len = serial_driver_->port()->receive(one_byte);
    if (received_len <= 0) {
      return false;
    }

    const uint8_t rx_byte = one_byte[0];
    switch (state) {
      case RxState::WAIT_HEAD:
        if (rx_byte == BCP_HEAD) {
          frame.push_back(rx_byte);
          state = RxState::WAIT_ADDR;
        } else if (debug_) {
          publishNamedDebugValue("bcp_last_sof", static_cast<double>(rx_byte));
        }
        break;

      case RxState::WAIT_ADDR:
        if (rx_byte == bcp_rx_addr_) {
          frame.push_back(rx_byte);
          state = RxState::WAIT_ID;
        } else {
          if (debug_) {
            publishNamedDebugValue("bcp_rx_addr_actual", static_cast<double>(rx_byte));
            publishNamedDebugValue("bcp_rx_addr_expected", static_cast<double>(bcp_rx_addr_));
          }
          resetParser(state, rx_byte);
        }
        break;

      case RxState::WAIT_ID:
        frame.push_back(rx_byte);
        state = RxState::WAIT_LEN;
        break;

      case RxState::WAIT_LEN:
        frame.push_back(rx_byte);
        payload_len = rx_byte;
        payload_received = 0U;
        state = payload_len == 0U ? RxState::WAIT_SUM : RxState::WAIT_DATA;
        break;

      case RxState::WAIT_DATA:
        frame.push_back(rx_byte);
        ++payload_received;
        if (payload_received >= payload_len) {
          state = RxState::WAIT_SUM;
        }
        break;

      case RxState::WAIT_SUM:
        frame.push_back(rx_byte);
        state = RxState::WAIT_ADD;
        break;

      case RxState::WAIT_ADD:
        frame.push_back(rx_byte);
        if (verifyBcpFrame(frame)) {
          return true;
        }
        resetParser(state, rx_byte);
        break;
    }
  }

  return false;
}

bool StandardRobotPpRos2Node::receiveBcpBytes(size_t expected_size, std::vector<uint8_t> & buffer)
{
  buffer.assign(expected_size, 0U);
  size_t offset = 0;

  while (offset < expected_size) {
    std::vector<uint8_t> chunk(expected_size - offset);
    const int received_len = serial_driver_->port()->receive(chunk);
    if (received_len <= 0) {
      return false;
    }

    const size_t received_size =
      std::min(expected_size - offset, static_cast<size_t>(received_len));
    std::copy_n(chunk.begin(), received_size, buffer.begin() + offset);
    offset += received_size;
  }

  return true;
}

bool StandardRobotPpRos2Node::verifyBcpFrame(const std::vector<uint8_t> & frame)
{
  if (frame.size() < 6 || frame[0] != BCP_HEAD) {
    if (debug_ && !frame.empty()) {
      publishNamedDebugValue("bcp_invalid_frame_head", static_cast<double>(frame[0]));
    }
    return false;
  }

  if (frame[1] != bcp_rx_addr_) {
    if (debug_) {
      publishNamedDebugValue("bcp_rx_addr_actual", static_cast<double>(frame[1]));
      publishNamedDebugValue("bcp_rx_addr_expected", static_cast<double>(bcp_rx_addr_));
    }
    return false;
  }

  const size_t expected_len = static_cast<size_t>(frame[3]) + 6;
  if (frame.size() != expected_len) {
    if (debug_) {
      publishNamedDebugValue("bcp_frame_size_actual", static_cast<double>(frame.size()));
      publishNamedDebugValue("bcp_frame_size_expected", static_cast<double>(expected_len));
    }
    return false;
  }

  int sumcheck = frame[0] + frame[1];
  int addcheck = frame[0] + sumcheck;
  for (size_t idx = 2; idx < frame.size() - 2; ++idx) {
    sumcheck += frame[idx];
    addcheck += sumcheck;
  }

  const auto expected_sum = static_cast<uint8_t>(sumcheck & 0xFF);
  const auto expected_add = static_cast<uint8_t>(addcheck & 0xFF);
  const bool sumcheck_ok = frame[frame.size() - 2] == expected_sum;
  const bool addcheck_ok = frame[frame.size() - 1] == expected_add;

  if (debug_ && !sumcheck_ok) {
    publishNamedDebugValue(
      "bcp_checksum_actual_sum", static_cast<double>(frame[frame.size() - 2]));
    publishNamedDebugValue("bcp_checksum_expected_sum", static_cast<double>(expected_sum));
    publishNamedDebugValue(
      "bcp_checksum_actual_add", static_cast<double>(frame[frame.size() - 1]));
    publishNamedDebugValue("bcp_checksum_expected_add", static_cast<double>(expected_add));
    return false;
  }

  if (debug_ && !addcheck_ok) {
    publishNamedDebugValue(
      "bcp_checksum_actual_add", static_cast<double>(frame[frame.size() - 1]));
    publishNamedDebugValue("bcp_checksum_expected_add", static_cast<double>(expected_add));
    publishNamedDebugValue("bcp_addcheck_mismatch_accepted", 1.0);
  }

  // Keep compatibility with the legacy Python parser, which still processes frames when
  // the additive checksum mismatches but the frame head, address, length, and sumcheck match.
  return true;
}

void StandardRobotPpRos2Node::handleBcpFrame(const std::vector<uint8_t> & frame)
{
  const uint8_t id = frame[2];
  const uint8_t payload_len = frame[3];
  if (debug_) {
    publishNamedDebugValue("bcp_last_rx_id", static_cast<double>(id));
    publishNamedDebugValue("bcp_last_rx_len", static_cast<double>(payload_len));
  }
  std::vector<uint8_t> payload(frame.begin() + 4, frame.begin() + 4 + payload_len);

  switch (id) {
    case BCP_ID_CHASSIS_IMU:
      handleBcpChassisImuFrame(payload);
      break;
    case BCP_ID_CHASSIS_ODOM:
      handleBcpChassisOdomFrame(payload);
      break;
    case BCP_ID_GIMBAL:
      handleBcpGimbalFrame(payload);
      break;
    case BCP_ID_GAME_STATUS:
      handleBcpGameStatusFrame(payload);
      break;
    case BCP_ID_ROBOT_HP:
      handleBcpRobotHpFrame(payload);
      break;
    case BCP_ID_ICRA_ZONE:
      handleBcpIcraZoneFrame(payload);
      break;
    default:
      if (debug_) {
        publishNamedDebugValue("bcp_unhandled_id", static_cast<double>(id));
        publishNamedDebugValue("bcp_unhandled_len", static_cast<double>(payload_len));
      }
      break;
  }
}

void StandardRobotPpRos2Node::handleBcpChassisImuFrame(const std::vector<uint8_t> & payload)
{
  if (payload.size() < 9 * sizeof(int32_t)) {
    return;
  }

  const double yaw_deg = static_cast<double>(readLittleEndian<int32_t>(payload, 0)) / 10000.0;
  const double pitch_deg = static_cast<double>(readLittleEndian<int32_t>(payload, 4)) / 10000.0;
  const double roll_deg = static_cast<double>(readLittleEndian<int32_t>(payload, 8)) / 10000.0;
  const double angle_x_deg = static_cast<double>(readLittleEndian<int32_t>(payload, 12)) / 10000.0;
  const double angle_y_deg = static_cast<double>(readLittleEndian<int32_t>(payload, 16)) / 10000.0;
  const double angle_z_deg = static_cast<double>(readLittleEndian<int32_t>(payload, 20)) / 10000.0;
  const double acc_x = static_cast<double>(readLittleEndian<int32_t>(payload, 24)) / 10000.0;
  const double acc_y = static_cast<double>(readLittleEndian<int32_t>(payload, 28)) / 10000.0;
  const double acc_z = static_cast<double>(readLittleEndian<int32_t>(payload, 32)) / 10000.0;

  if (!isPlausibleBcpImuSample(
        yaw_deg, pitch_deg, roll_deg, angle_x_deg, angle_y_deg, angle_z_deg, acc_x, acc_y, acc_z))
  {
    if (debug_) {
      publishNamedDebugValue("bcp_imu_outlier_yaw", yaw_deg);
      publishNamedDebugValue("bcp_imu_outlier_pitch", pitch_deg);
      publishNamedDebugValue("bcp_imu_outlier_roll", roll_deg);
    }
    return;
  }

  bcp_last_imu_yaw_ = yaw_deg;
  bcp_last_imu_pitch_ = pitch_deg;
  bcp_last_imu_roll_ = roll_deg;
  last_chassis_imu_time_ms_.store(nowSteadyMs());
  last_gimbal_pitch_odom_joint_ = static_cast<float>(pitch_deg * BCP_PI / 180.0);
  last_gimbal_yaw_odom_joint_ = static_cast<float>(yaw_deg * BCP_PI / 180.0);

  publishBcpImuFromEulerDegrees(
    yaw_deg, pitch_deg, roll_deg, angle_x_deg, angle_y_deg, angle_z_deg, acc_x, acc_y, acc_z);
  publishBcpJointState();
}

void StandardRobotPpRos2Node::handleBcpChassisOdomFrame(const std::vector<uint8_t> & payload)
{
  if (payload.size() < 6 * sizeof(int32_t)) {
    return;
  }

  geometry_msgs::msg::Twist msg;
  msg.linear.x = static_cast<double>(readLittleEndian<int32_t>(payload, 0)) / 10000.0;
  msg.linear.y = static_cast<double>(readLittleEndian<int32_t>(payload, 4)) / 10000.0;
  msg.angular.z = static_cast<double>(readLittleEndian<int32_t>(payload, 8)) / 10000.0;
  robot_motion_pub_->publish(msg);
}

void StandardRobotPpRos2Node::handleBcpGimbalFrame(const std::vector<uint8_t> & payload)
{
  if (payload.size() < 1 + 6 * sizeof(int32_t)) {
    return;
  }

  const double yaw_deg = static_cast<double>(readLittleEndian<int32_t>(payload, 1)) / 1000.0;
  const double pitch_deg = static_cast<double>(readLittleEndian<int32_t>(payload, 5)) / 1000.0;
  const double roll_deg = static_cast<double>(readLittleEndian<int32_t>(payload, 9)) / 1000.0;
  const double yaw_speed_deg =
    static_cast<double>(readLittleEndian<int32_t>(payload, 13)) / 1000.0;
  const double pitch_speed_deg =
    static_cast<double>(readLittleEndian<int32_t>(payload, 17)) / 1000.0;
  const double roll_speed_deg =
    static_cast<double>(readLittleEndian<int32_t>(payload, 21)) / 1000.0;

  if (!isPlausibleBcpImuSample(
        yaw_deg, pitch_deg, roll_deg, roll_speed_deg, pitch_speed_deg, yaw_speed_deg, 0.0, 0.0,
        0.0))
  {
    if (debug_) {
      publishNamedDebugValue("bcp_imu_outlier_yaw", yaw_deg);
      publishNamedDebugValue("bcp_imu_outlier_pitch", pitch_deg);
      publishNamedDebugValue("bcp_imu_outlier_roll", roll_deg);
    }
    return;
  }

  bcp_last_gimbal_yaw_ = yaw_deg;
  bcp_last_gimbal_pitch_ = pitch_deg;
  publishBcpJointState();

  const auto now_ms = nowSteadyMs();
  if (now_ms - last_chassis_imu_time_ms_.load() > BCP_RX_TIMEOUT_MS) {
    bcp_last_imu_yaw_ = yaw_deg;
    bcp_last_imu_pitch_ = pitch_deg;
    bcp_last_imu_roll_ = roll_deg;
    publishBcpImuFromEulerDegrees(
      yaw_deg, pitch_deg, roll_deg, roll_speed_deg, pitch_speed_deg, yaw_speed_deg, 0.0, 0.0,
      0.0);
    if (debug_) {
      publishNamedDebugValue("bcp_imu_fallback_from_gimbal", 1.0);
    }
  }
}


void StandardRobotPpRos2Node::handleBcpGameStatusFrame(const std::vector<uint8_t> & payload)
{
  if (payload.size() < 4) {
    return;
  }

  pb_rm_interfaces::msg::GameStatus msg;
  msg.game_progress = readLittleEndian<uint8_t>(payload, 1);
  msg.stage_remain_time = static_cast<int32_t>(readLittleEndian<uint16_t>(payload, 2)) / 100;
  game_status_pub_->publish(msg);
}

void StandardRobotPpRos2Node::handleBcpRobotHpFrame(const std::vector<uint8_t> & payload)
{
  if (payload.size() < 16 * sizeof(uint16_t)) {
    return;
  }

  pb_rm_interfaces::msg::GameRobotHP msg;
  msg.red_1_robot_hp = readLittleEndian<uint16_t>(payload, 0);
  msg.red_2_robot_hp = readLittleEndian<uint16_t>(payload, 2);
  msg.red_3_robot_hp = readLittleEndian<uint16_t>(payload, 4);
  msg.red_4_robot_hp = readLittleEndian<uint16_t>(payload, 6);
  msg.red_7_robot_hp = readLittleEndian<uint16_t>(payload, 10);
  msg.red_outpost_hp = readLittleEndian<uint16_t>(payload, 12);
  msg.red_base_hp = readLittleEndian<uint16_t>(payload, 14);
  msg.blue_1_robot_hp = readLittleEndian<uint16_t>(payload, 16);
  msg.blue_2_robot_hp = readLittleEndian<uint16_t>(payload, 18);
  msg.blue_3_robot_hp = readLittleEndian<uint16_t>(payload, 20);
  msg.blue_4_robot_hp = readLittleEndian<uint16_t>(payload, 22);
  msg.blue_7_robot_hp = readLittleEndian<uint16_t>(payload, 26);
  msg.blue_outpost_hp = readLittleEndian<uint16_t>(payload, 28);
  msg.blue_base_hp = readLittleEndian<uint16_t>(payload, 30);
  all_robot_hp_pub_->publish(msg);
}

void StandardRobotPpRos2Node::handleBcpIcraZoneFrame(const std::vector<uint8_t> & payload)
{
  if (payload.size() < 16U) {
    return;
  }

  const uint8_t f1_zone_status = clampOccupiedState(readLittleEndian<uint8_t>(payload, 0));
  const uint8_t f1_zone_buff_status = readLittleEndian<uint8_t>(payload, 1);
  const uint8_t f2_zone_status = clampOccupiedState(readLittleEndian<uint8_t>(payload, 2));
  const uint8_t f2_zone_buff_status = readLittleEndian<uint8_t>(payload, 3);
  const uint8_t f3_zone_status = clampOccupiedState(readLittleEndian<uint8_t>(payload, 4));
  const uint8_t f3_zone_buff_status = readLittleEndian<uint8_t>(payload, 5);
  const uint8_t f4_zone_status = clampOccupiedState(readLittleEndian<uint8_t>(payload, 6));
  const uint8_t f4_zone_buff_status = readLittleEndian<uint8_t>(payload, 7);
  const uint8_t f5_zone_status = clampOccupiedState(readLittleEndian<uint8_t>(payload, 8));
  const uint8_t f5_zone_buff_status = readLittleEndian<uint8_t>(payload, 9);
  const uint8_t f6_zone_status = clampOccupiedState(readLittleEndian<uint8_t>(payload, 10));
  const uint8_t f6_zone_buff_status = readLittleEndian<uint8_t>(payload, 11);
  const uint8_t red1_bullet_left = readLittleEndian<uint8_t>(payload, 12);
  const uint8_t red2_bullet_left = readLittleEndian<uint8_t>(payload, 13);
  const uint8_t blue1_bullet_left = readLittleEndian<uint8_t>(payload, 14);
  const uint8_t blue2_bullet_left = readLittleEndian<uint8_t>(payload, 15);

  // This is an inferred compatibility mapping from BCP ICRA zone slots to PolarBear's
  // referee event/RFID topics. Only the fields with reasonably close semantics are mapped.
  pb_rm_interfaces::msg::EventData event_msg;
  event_msg.non_overlapping_supply_zone = f1_zone_status;
  event_msg.overlapping_supply_zone = f2_zone_status;
  event_msg.central_highland = f3_zone_status;
  event_msg.trapezoidal_highland = f4_zone_status;
  event_msg.center_gain_zone = f5_zone_status;
  event_data_pub_->publish(event_msg);

  pb_rm_interfaces::msg::RfidStatus rfid_msg;
  rfid_msg.friendly_supply_zone_non_exchange = isFriendlyActive(f1_zone_status);
  rfid_msg.friendly_supply_zone_exchange = isFriendlyActive(f2_zone_status);
  rfid_msg.central_highland_gain_point = isFriendlyActive(f3_zone_status);
  rfid_msg.enemy_central_highland_gain_point = isEnemyActive(f3_zone_status);
  rfid_msg.friendly_trapezoidal_highland_gain_point = isFriendlyActive(f4_zone_status);
  rfid_msg.enemy_trapezoidal_highland_gain_point = isEnemyActive(f4_zone_status);
  rfid_msg.center_gain_point = f5_zone_status != 0U;
  rfid_status_pub_->publish(rfid_msg);

  if (debug_) {
    publishNamedDebugValue("icra_f1_buff_status", static_cast<double>(f1_zone_buff_status));
    publishNamedDebugValue("icra_f2_buff_status", static_cast<double>(f2_zone_buff_status));
    publishNamedDebugValue("icra_f3_buff_status", static_cast<double>(f3_zone_buff_status));
    publishNamedDebugValue("icra_f4_buff_status", static_cast<double>(f4_zone_buff_status));
    publishNamedDebugValue("icra_f5_buff_status", static_cast<double>(f5_zone_buff_status));
    publishNamedDebugValue("icra_f6_zone_status", static_cast<double>(f6_zone_status));
    publishNamedDebugValue("icra_f6_buff_status", static_cast<double>(f6_zone_buff_status));
    publishNamedDebugValue("icra_red1_bullet_left", static_cast<double>(red1_bullet_left));
    publishNamedDebugValue("icra_red2_bullet_left", static_cast<double>(red2_bullet_left));
    publishNamedDebugValue("icra_blue1_bullet_left", static_cast<double>(blue1_bullet_left));
    publishNamedDebugValue("icra_blue2_bullet_left", static_cast<double>(blue2_bullet_left));
  }
}

void StandardRobotPpRos2Node::publishBcpJointState()
{
  sensor_msgs::msg::JointState joint_msg;
  joint_msg.header.stamp = now();
  joint_msg.name = {
    "gimbal_pitch_joint",
    "gimbal_yaw_joint",
    "gimbal_pitch_odom_joint",
    "gimbal_yaw_odom_joint",
  };
  joint_msg.position = {
    -bcp_last_gimbal_pitch_ * BCP_PI / 180.0,
    bcp_last_gimbal_yaw_ * BCP_PI / 180.0,
    last_gimbal_pitch_odom_joint_,
    last_gimbal_yaw_odom_joint_,
  };
  joint_state_pub_->publish(joint_msg);
}

void StandardRobotPpRos2Node::cmdVelCallback(const geometry_msgs::msg::Twist::SharedPtr msg)
{
  // Convert ROS base_link convention (x forward, y left) to the verified chassis convention.
  // On the real robot:
  // - chassis vx positive moves left
  // - chassis vy positive moves backward
  bcp_cmd_cache_.vx = msg->linear.y;
  bcp_cmd_cache_.vy = -msg->linear.x;
  bcp_cmd_cache_.wz = msg->angular.z;
  last_cmd_update_time_ms_.store(nowSteadyMs());
}

void StandardRobotPpRos2Node::cmdGimbalJointCallback(
  const sensor_msgs::msg::JointState::SharedPtr msg)
{
  if (msg->name.size() != msg->position.size()) {
    RCLCPP_ERROR(
      get_logger(), "JointState message name and position arrays are of different sizes");
    return;
  }

  for (size_t i = 0; i < msg->name.size(); ++i) {
    if (msg->name[i] == "gimbal_pitch_joint") {
      bcp_cmd_cache_.gimbal_pitch = static_cast<float>(msg->position[i]);
    } else if (msg->name[i] == "gimbal_yaw_joint") {
      bcp_cmd_cache_.gimbal_yaw = static_cast<float>(msg->position[i]);
    }
  }
  last_cmd_update_time_ms_.store(nowSteadyMs());
}

void StandardRobotPpRos2Node::visionTargetCallback(
  const auto_aim_interfaces::msg::Target::SharedPtr msg)
{
  bcp_cmd_cache_.tracking = msg->tracking;
  last_cmd_update_time_ms_.store(nowSteadyMs());
}

void StandardRobotPpRos2Node::cmdShootCallback(const example_interfaces::msg::UInt8::SharedPtr msg)
{
  bcp_cmd_cache_.shoot_fric_on = true;
  bcp_cmd_cache_.shoot_fire = msg->data;
  last_cmd_update_time_ms_.store(nowSteadyMs());
}

void StandardRobotPpRos2Node::setParam(const rclcpp::Parameter & param)
{
  if (!initial_set_param_) {
    auto node_graph = this->get_node_graph_interface();
    auto node_names = node_graph->get_node_names();
    std::vector<std::string> possible_detectors = {
      "armor_detector_openvino", "armor_detector_opencv"};

    for (const auto & name : possible_detectors) {
      for (const auto & node_name : node_names) {
        if (node_name.find(name) != std::string::npos) {
          detector_node_name_ = node_name;
          break;
        }
      }
      if (!detector_node_name_.empty()) {
        break;
      }
    }

    if (detector_node_name_.empty()) {
      RCLCPP_WARN_THROTTLE(get_logger(), *this->get_clock(), 1000, "No detector node found!");
      return;
    }

    detector_param_client_ =
      std::make_shared<rclcpp::AsyncParametersClient>(this, detector_node_name_);
    if (!detector_param_client_->service_is_ready()) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *this->get_clock(), 1000, "Service not ready, skipping parameter set");
      return;
    }
  }

  if (
    !set_param_future_.valid() ||
    set_param_future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    RCLCPP_INFO(get_logger(), "Setting detect_color to %ld...", param.as_int());
    set_param_future_ = detector_param_client_->set_parameters(
      {param}, [this, param](const ResultFuturePtr & results) {
        for (const auto & result : results.get()) {
          if (!result.successful) {
            RCLCPP_ERROR(get_logger(), "Failed to set parameter: %s", result.reason.c_str());
            return;
          }
        }
        RCLCPP_INFO(get_logger(), "Successfully set detect_color to %ld!", param.as_int());
        initial_set_param_ = true;
      });
  }
}

bool StandardRobotPpRos2Node::getDetectColor(uint8_t robot_id, uint8_t & color)
{
  if (robot_id == 0 || (robot_id > 11 && robot_id < 101)) {
    RCLCPP_WARN_THROTTLE(
      get_logger(), *this->get_clock(), 1000, "Invalid robot ID: %d. Color not set.", robot_id);
    return false;
  }
  color = (robot_id >= 100) ? 0 : 1;
  return true;
}

bool StandardRobotPpRos2Node::callTriggerService(const std::string & service_name)
{
  auto client = this->create_client<std_srvs::srv::Trigger>(service_name);
  auto request = std::make_shared<std_srvs::srv::Trigger::Request>();

  auto start_time = std::chrono::steady_clock::now();
  while (!client->wait_for_service(0.1s)) {
    if (!rclcpp::ok()) {
      RCLCPP_ERROR(
        get_logger(), "Interrupted while waiting for the service: %s", service_name.c_str());
      return false;
    }
    auto elapsed_time = std::chrono::steady_clock::now() - start_time;
    if (elapsed_time > std::chrono::seconds(5)) {
      RCLCPP_ERROR(
        get_logger(), "Service %s not available after 5 seconds, giving up.", service_name.c_str());
      return false;
    }
    RCLCPP_INFO(get_logger(), "Service %s not available, waiting again...", service_name.c_str());
  }

  auto result = client->async_send_request(request);
  if (
    rclcpp::spin_until_future_complete(this->shared_from_this(), result) ==
    rclcpp::FutureReturnCode::SUCCESS) {
    RCLCPP_INFO(
      get_logger(), "Service %s call succeeded: %s", service_name.c_str(),
      result.get()->success ? "true" : "false");
    return result.get()->success;
  }

  RCLCPP_ERROR(get_logger(), "Service %s call failed", service_name.c_str());
  return false;
}

}  // namespace standard_robot_pp_ros2

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(standard_robot_pp_ros2::StandardRobotPpRos2Node)
