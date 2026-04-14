from __future__ import annotations

import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Protocol

from .config import CollectorConfig, RttCollectorConfig
from .imu_bridge import NormalizedImuSample
from .rtt_state_collector import LowLevelCollector, build_rtt_collector
from .runtime_state import RuntimeStateStore


class Collector(Protocol):
    def collect(self, seq: int) -> dict[str, Any]:
        ...

    def latest_imu_sample(self) -> NormalizedImuSample | None:
        ...


@dataclass(slots=True)
class MockStateCollector:
    frame_id: str = "map"
    runtime_store: RuntimeStateStore | None = None
    low_level_collector: LowLevelCollector | None = None

    def collect(self, seq: int) -> dict[str, Any]:
        angle = seq / 6.0
        distance = max(0.0, 6.0 - seq * 0.25)
        progress = min(seq * 5, 100)
        nav_state = "idle" if seq == 0 else "completed" if progress >= 100 else "running"
        task_state = "idle" if seq == 0 else "completed" if progress >= 100 else "running"
        task_type = "placeholder" if seq == 0 else "go_to_waypoint"
        alerts: list[dict[str, Any]] = []

        if distance < 1.5 and progress < 100:
            alerts.append(
                {
                    "id": "nav-slowdown",
                    "severity": "warning",
                    "text": "Approaching waypoint, speed reduced for stability.",
                    "source": "nuc-nav",
                    "time": None,
                    "ack": False,
                }
            )

        low_level_state = self.low_level_collector.collect(seq) if self.low_level_collector else None
        low_level_sections = (
            low_level_state.as_internal_sections()
            if low_level_state is not None
            else {
                "device": {
                    "battery_percent": max(30, 100 - seq),
                    "emergency_stop": False,
                    "fault_code": None,
                    "online": True,
                },
                "environment": {
                    "temperature_c": None,
                    "humidity_percent": None,
                    "status": "offline",
                },
            }
        )

        state = {
            "pose": {
                "x": round(2.0 + math.cos(angle), 3),
                "y": round(1.0 + math.sin(angle), 3),
                "yaw": round((seq / 8.0) % 6.283, 3),
                "frame_id": self.frame_id,
                "timestamp": None,
            },
            "navigation": {
                "mode": "auto",
                "status": nav_state,
                "goal_id": f"wp-{seq % 4}",
                "remaining_distance_m": round(distance, 2) if nav_state != "completed" else 0.0,
            },
            "task": {
                "id": "task-live-001" if seq else "task-idle",
                "type": task_type,
                "status": task_state if seq else "idle",
                "progress_percent": progress if seq else 0,
                "source": "nuc",
            },
            "device": low_level_sections["device"],
            "environment": low_level_sections["environment"],
            "alerts": alerts,
            "updated_at": None,
        }
        return self._apply_runtime_overrides(state)

    def _apply_runtime_overrides(self, state: dict[str, Any]) -> dict[str, Any]:
        if not self.runtime_store:
            return state

        runtime_state = self.runtime_store.load()
        runtime_task = runtime_state.get("task")
        runtime_navigation = runtime_state.get("navigation")
        runtime_alerts = runtime_state.get("alerts")
        if isinstance(runtime_task, dict):
            state["task"].update(runtime_task)
        if isinstance(runtime_navigation, dict):
            state["navigation"].update(runtime_navigation)
        if isinstance(runtime_alerts, list):
            state["alerts"] = runtime_alerts
        return state

    def latest_imu_sample(self) -> NormalizedImuSample | None:
        if not self.low_level_collector:
            return None
        latest = getattr(self.low_level_collector, "latest_imu_sample", None)
        if not callable(latest):
            return None
        return latest()


@dataclass(slots=True)
class FileStateCollector:
    state_file: Path

    def collect(self, seq: int) -> dict[str, Any]:
        del seq
        with self.state_file.open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
        if not isinstance(payload, dict):
            raise ValueError(f"state file must contain a JSON object: {self.state_file}")
        return payload

    def latest_imu_sample(self) -> NormalizedImuSample | None:
        return None


def build_collector(config: CollectorConfig, rtt_config: RttCollectorConfig | None = None) -> Collector:
    if config.type == "mock":
        return MockStateCollector(
            frame_id=config.frame_id,
            runtime_store=RuntimeStateStore(config.runtime_state_file),
            low_level_collector=build_rtt_collector(rtt_config or RttCollectorConfig()),
        )
    if config.type == "file":
        if not config.state_file:
            raise ValueError("collector.state_file is required when collector.type=file")
        return FileStateCollector(state_file=Path(config.state_file))
    raise ValueError(f"unsupported collector.type: {config.type}")
