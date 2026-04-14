from __future__ import annotations

import json
import math
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Protocol

from .config import RttCollectorConfig


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


def build_rtt_collector(config: RttCollectorConfig) -> LowLevelCollector:
    if config.type == "mock":
        return MockRttStateCollector(source=config.source_name)
    if config.type == "file":
        if not config.state_file:
            raise ValueError("rtt_collector.state_file is required when rtt_collector.type=file")
        return FileRttStateCollector(state_file=Path(config.state_file), source=config.source_name)
    raise ValueError(f"unsupported rtt_collector.type: {config.type}")
