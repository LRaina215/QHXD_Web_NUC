from __future__ import annotations

import json
import math
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any, Protocol

import rclpy
from geometry_msgs.msg import Twist, Vector3
from sensor_msgs.msg import Imu

from .config import RttCollectorConfig
from .imu_bridge import EulerDegSample, NormalizedImuSample, QuaternionSample, Vector3Sample, ros_time_to_iso


def _normalize_optional_str(value: Any) -> str | None:
    if value is None:
        return None
    text = str(value).strip()
    return text or None


def _normalize_optional_float(value: Any) -> float | None:
    if value is None:
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _normalize_optional_int(value: Any, min_value: int = 0, max_value: int = 100) -> int | None:
    if value is None:
        return None
    try:
        result = int(float(value))
    except (TypeError, ValueError):
        return None
    return min(max(result, min_value), max_value)


def _normalize_bool(value: Any, default: bool = False) -> bool:
    if isinstance(value, bool):
        return value
    if value is None:
        return default
    text = str(value).strip().lower()
    if text in {"1", "true", "yes", "y", "on"}:
        return True
    if text in {"0", "false", "no", "n", "off"}:
        return False
    return default


@dataclass(slots=True)
class NormalizedLowLevelState:
    battery_percent: int | None
    emergency_stop: bool
    fault_code: str | None
    online: bool
    velocity_mps: float | None = None
    temperature_c: float | None = None
    humidity_percent: float | None = None
    env_status: str = "offline"
    source: str = "rtt"

    def as_internal_sections(self) -> dict[str, dict[str, Any]]:
        return {
            "device": {
                "battery_percent": self.battery_percent,
                "emergency_stop": self.emergency_stop,
                "fault_code": self.fault_code,
                "online": self.online,
                "velocity_mps": self.velocity_mps,
                "source": self.source,
            },
            "environment": {
                "temperature_c": self.temperature_c,
                "humidity_percent": self.humidity_percent,
                "status": self.env_status,
                "source": self.source,
            },
        }

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


class LowLevelCollector(Protocol):
    def collect(self, seq: int) -> NormalizedLowLevelState:
        ...

    def latest_imu_sample(self) -> NormalizedImuSample | None:
        ...


@dataclass(slots=True)
class MockRttStateCollector:
    source: str = "rtt"

    def collect(self, seq: int) -> NormalizedLowLevelState:
        velocity = round(abs(math.sin(seq / 5.0)) * 0.9, 3)
        fault_code = None
        if seq and seq % 30 == 0:
            fault_code = "mock-rtt-heartbeat-check"

        return NormalizedLowLevelState(
            battery_percent=max(20, 98 - seq),
            emergency_stop=False,
            fault_code=fault_code,
            online=True,
            velocity_mps=velocity,
            temperature_c=None,
            humidity_percent=None,
            env_status="offline",
            source=self.source,
        )

    def latest_imu_sample(self) -> NormalizedImuSample | None:
        return None


@dataclass(slots=True)
class FileRttStateCollector:
    state_file: Path
    source: str = "rtt"

    def collect(self, seq: int) -> NormalizedLowLevelState:
        del seq
        with self.state_file.open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
        if not isinstance(payload, dict):
            raise ValueError(f"rtt state file must contain a JSON object: {self.state_file}")

        return NormalizedLowLevelState(
            battery_percent=_normalize_optional_int(payload.get("battery_percent")),
            emergency_stop=_normalize_bool(payload.get("emergency_stop"), False),
            fault_code=_normalize_optional_str(payload.get("fault_code")),
            online=_normalize_bool(payload.get("online"), False),
            velocity_mps=_normalize_optional_float(
                payload.get("velocity_mps") if "velocity_mps" in payload else payload.get("velocity")
            ),
            temperature_c=_normalize_optional_float(payload.get("temperature_c")),
            humidity_percent=_normalize_optional_float(payload.get("humidity_percent")),
            env_status=_normalize_optional_str(payload.get("env_status") or payload.get("status")) or "offline",
            source=_normalize_optional_str(payload.get("source")) or self.source,
        )

    def latest_imu_sample(self) -> NormalizedImuSample | None:
        return None


@dataclass(slots=True)
class Ros2TopicRttStateCollector:
    imu_topic: str = "/serial/imu"
    receive_topic: str = "/serial/receive"
    motion_topic: str = "/serial/robot_motion"
    source: str = "rtt"
    sample_timeout_sec: float = 0.3
    freshness_timeout_sec: float = 2.0
    _node: Any = field(init=False, repr=False)
    _latest_imu: NormalizedImuSample | None = field(init=False, default=None, repr=False)
    _latest_receive: Vector3 | None = field(init=False, default=None, repr=False)
    _latest_motion: Twist | None = field(init=False, default=None, repr=False)
    _latest_imu_received_monotonic: float = field(init=False, default=0.0, repr=False)
    _latest_receive_received_monotonic: float = field(init=False, default=0.0, repr=False)
    _latest_motion_received_monotonic: float = field(init=False, default=0.0, repr=False)

    def __post_init__(self) -> None:
        if not rclpy.ok():
            rclpy.init(args=None)
        node_name = f"nuc_rtt_state_collector_{int(time.time() * 1000)}"
        self._node = rclpy.create_node(node_name)
        self._node.create_subscription(Imu, self.imu_topic, self._on_imu, 10)
        self._node.create_subscription(Vector3, self.receive_topic, self._on_receive, 10)
        self._node.create_subscription(Twist, self.motion_topic, self._on_motion, 10)

    def collect(self, seq: int) -> NormalizedLowLevelState:
        del seq
        self._spin_for_messages()
        now = time.monotonic()
        imu_fresh = self._is_fresh(self._latest_imu_received_monotonic, now)
        motion_fresh = self._is_fresh(self._latest_motion_received_monotonic, now)
        velocity = None
        if motion_fresh and self._latest_motion is not None:
            velocity = round(
                math.hypot(self._latest_motion.linear.x, self._latest_motion.linear.y),
                3,
            )

        return NormalizedLowLevelState(
            battery_percent=None,
            emergency_stop=False,
            fault_code=None,
            online=imu_fresh or motion_fresh,
            velocity_mps=velocity,
            temperature_c=None,
            humidity_percent=None,
            env_status="offline",
            source=self.source,
        )

    def latest_imu_sample(self) -> NormalizedImuSample | None:
        self._spin_for_messages()
        if not self._is_fresh(self._latest_imu_received_monotonic, time.monotonic()):
            return None
        return self._latest_imu

    def close(self) -> None:
        self._node.destroy_node()

    def _spin_for_messages(self) -> None:
        deadline = time.monotonic() + max(self.sample_timeout_sec, 0.0)
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            rclpy.spin_once(self._node, timeout_sec=min(remaining, 0.05))

    def _is_fresh(self, received_monotonic: float, now: float) -> bool:
        if received_monotonic <= 0:
            return False
        return (now - received_monotonic) <= self.freshness_timeout_sec

    def _on_imu(self, message: Imu) -> None:
        stamp = message.header.stamp
        if stamp.sec == 0 and stamp.nanosec == 0:
            timestamp = ros_time_to_iso(int(time.time()), 0)
        else:
            timestamp = ros_time_to_iso(stamp.sec, stamp.nanosec)

        self._latest_imu = NormalizedImuSample(
            frame_id=message.header.frame_id or "imu_link",
            timestamp=timestamp,
            orientation=QuaternionSample(
                x=float(message.orientation.x),
                y=float(message.orientation.y),
                z=float(message.orientation.z),
                w=float(message.orientation.w),
            ),
            angular_velocity=Vector3Sample(
                x=float(message.angular_velocity.x),
                y=float(message.angular_velocity.y),
                z=float(message.angular_velocity.z),
            ),
            linear_acceleration=Vector3Sample(
                x=float(message.linear_acceleration.x),
                y=float(message.linear_acceleration.y),
                z=float(message.linear_acceleration.z),
            ),
            euler_deg=self._build_euler_deg(time.monotonic()),
            source=self.source,
        )
        self._latest_imu_received_monotonic = time.monotonic()

    def _on_receive(self, message: Vector3) -> None:
        self._latest_receive = message
        self._latest_receive_received_monotonic = time.monotonic()
        if self._latest_imu is not None:
            self._latest_imu.euler_deg = self._build_euler_deg(time.monotonic())

    def _on_motion(self, message: Twist) -> None:
        self._latest_motion = message
        self._latest_motion_received_monotonic = time.monotonic()

    def _build_euler_deg(self, now: float) -> EulerDegSample | None:
        if self._latest_receive is None or not self._is_fresh(self._latest_receive_received_monotonic, now):
            return None
        return EulerDegSample(
            yaw=float(self._latest_receive.x),
            pitch=float(self._latest_receive.y),
            roll=float(self._latest_receive.z),
        )


def _build_ros2_topic_collector(config: RttCollectorConfig) -> Ros2TopicRttStateCollector:
    return Ros2TopicRttStateCollector(
        imu_topic=config.imu_topic,
        receive_topic=config.receive_topic,
        motion_topic=config.motion_topic,
        source=config.source_name,
        sample_timeout_sec=config.sample_timeout_sec,
        freshness_timeout_sec=config.freshness_timeout_sec,
    )


def build_rtt_collector(config: RttCollectorConfig) -> LowLevelCollector:
    if config.type == "mock":
        return MockRttStateCollector(source=config.source_name)
    if config.type == "file":
        if not config.state_file:
            raise ValueError("rtt_collector.state_file is required when rtt_collector.type=file")
        return FileRttStateCollector(state_file=Path(config.state_file), source=config.source_name)
    if config.type == "ros2":
        return _build_ros2_topic_collector(config)
    raise ValueError(f"unsupported rtt_collector.type: {config.type}")
