from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


DEFAULT_CONFIG_PATH = Path("configs/default_config.json")


@dataclass(slots=True)
class Rk3588Config:
    base_url: str = "http://127.0.0.1:8000"
    state_endpoint: str = "/api/internal/nuc/state"
    mode_switch_endpoint: str = "/api/system/mode/switch"
    timeout_sec: float = 2.0
    retry_count: int = 1
    retry_backoff_sec: float = 0.5


@dataclass(slots=True)
class CollectorConfig:
    type: str = "mock"
    state_file: str | None = None
    frame_id: str = "map"
    source_name: str = "nuc"
    runtime_state_file: str = "runtime/mission_state.json"


@dataclass(slots=True)
class RttCollectorConfig:
    type: str = "mock"
    state_file: str | None = None
    source_name: str = "rtt"


@dataclass(slots=True)
class MissionServerConfig:
    host: str = "0.0.0.0"
    port: int = 8090
    path: str = "/api/internal/rk3588/mission"
    runtime_state_file: str = "runtime/mission_state.json"


@dataclass(slots=True)
class AppConfig:
    send_interval_sec: float = 1.0
    log_level: str = "INFO"
    dump_payload: bool = False
    rk3588: Rk3588Config = field(default_factory=Rk3588Config)
    collector: CollectorConfig = field(default_factory=CollectorConfig)
    rtt_collector: RttCollectorConfig = field(default_factory=RttCollectorConfig)
    mission_server: MissionServerConfig = field(default_factory=MissionServerConfig)

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "AppConfig":
        rk3588_data = data.get("rk3588", {})
        collector_data = data.get("collector", {})
        rtt_collector_data = data.get("rtt_collector", {})
        mission_server_data = data.get("mission_server", {})

        config = cls(
            send_interval_sec=float(data.get("send_interval_sec", 1.0)),
            log_level=str(data.get("log_level", "INFO")).upper(),
            dump_payload=bool(data.get("dump_payload", False)),
            rk3588=Rk3588Config(
                base_url=str(rk3588_data.get("base_url", "http://127.0.0.1:8000")).rstrip("/"),
                state_endpoint=str(rk3588_data.get("state_endpoint", "/api/internal/nuc/state")),
                mode_switch_endpoint=str(
                    rk3588_data.get("mode_switch_endpoint", "/api/system/mode/switch")
                ),
                timeout_sec=float(rk3588_data.get("timeout_sec", 2.0)),
                retry_count=max(0, int(rk3588_data.get("retry_count", 1))),
                retry_backoff_sec=max(0.0, float(rk3588_data.get("retry_backoff_sec", 0.5))),
            ),
            collector=CollectorConfig(
                type=str(collector_data.get("type", "mock")).lower(),
                state_file=collector_data.get("state_file"),
                frame_id=str(collector_data.get("frame_id", "map")),
                source_name=str(collector_data.get("source_name", "nuc")),
                runtime_state_file=str(
                    collector_data.get("runtime_state_file", "runtime/mission_state.json")
                ),
            ),
            rtt_collector=RttCollectorConfig(
                type=str(rtt_collector_data.get("type", "mock")).lower(),
                state_file=rtt_collector_data.get("state_file"),
                source_name=str(rtt_collector_data.get("source_name", "rtt")),
            ),
            mission_server=MissionServerConfig(
                host=str(mission_server_data.get("host", "0.0.0.0")),
                port=int(mission_server_data.get("port", 8090)),
                path=str(mission_server_data.get("path", "/api/internal/rk3588/mission")),
                runtime_state_file=str(
                    mission_server_data.get("runtime_state_file", "runtime/mission_state.json")
                ),
            ),
        )

        if config.send_interval_sec <= 0:
            raise ValueError("send_interval_sec must be greater than 0")
        if config.rk3588.timeout_sec <= 0:
            raise ValueError("rk3588.timeout_sec must be greater than 0")
        if config.mission_server.port <= 0:
            raise ValueError("mission_server.port must be greater than 0")

        return config


def load_config(path: str | Path | None = None) -> AppConfig:
    config_path = Path(path) if path else DEFAULT_CONFIG_PATH
    with config_path.open("r", encoding="utf-8") as handle:
        raw = json.load(handle)
    return AppConfig.from_dict(raw)
