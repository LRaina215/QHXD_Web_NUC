from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from typing import Any


@dataclass(slots=True)
class Vector3Sample:
    x: float
    y: float
    z: float


@dataclass(slots=True)
class QuaternionSample:
    x: float
    y: float
    z: float
    w: float


@dataclass(slots=True)
class NormalizedImuSample:
    frame_id: str
    timestamp: str
    orientation: QuaternionSample
    angular_velocity: Vector3Sample
    linear_acceleration: Vector3Sample
    source: str = "rtt"

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)

    def as_payload(self) -> dict[str, Any]:
        return {
            "source": self.source,
            "updated_at": self.timestamp,
            "imu": self.as_dict(),
        }


def ros_time_to_iso(sec: int, nanosec: int) -> str:
    timestamp = sec + nanosec / 1_000_000_000
    return datetime.fromtimestamp(timestamp, tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")
