from __future__ import annotations

from datetime import datetime, timezone
from typing import Any


NAV_STATE_MAP = {
    "idle": "idle",
    "stopped": "idle",
    "running": "running",
    "moving": "running",
    "paused": "paused",
    "completed": "completed",
    "success": "completed",
    "failed": "failed",
    "error": "failed",
    "offline": "offline",
}

TASK_TYPE_MAP = {
    "placeholder": "placeholder",
    "go_to_waypoint": "go_to_waypoint",
    "goto": "go_to_waypoint",
    "start_patrol": "start_patrol",
    "patrol": "start_patrol",
    "return_home": "return_home",
    "home": "return_home",
}

TASK_STATE_MAP = {
    "idle": "idle",
    "pending": "pending",
    "queued": "pending",
    "running": "running",
    "executing": "running",
    "paused": "paused",
    "completed": "completed",
    "success": "completed",
    "failed": "failed",
    "error": "failed",
    "cancelled": "cancelled",
    "canceled": "cancelled",
}

ENV_STATUS_MAP = {
    "mock": "mock",
    "nominal": "nominal",
    "normal": "nominal",
    "ok": "nominal",
    "warning": "warning",
    "fault": "fault",
    "error": "fault",
    "offline": "offline",
}

ALERT_LEVEL_MAP = {
    "info": "info",
    "warning": "warning",
    "warn": "warning",
    "error": "error",
    "critical": "critical",
    "fatal": "critical",
}


def utc_now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _normalize_enum(value: Any, mapping: dict[str, str], default: str) -> str:
    if value is None:
        return default
    normalized = str(value).strip().lower()
    return mapping.get(normalized, default)


def _normalize_optional_str(value: Any) -> str | None:
    if value is None:
        return None
    text = str(value).strip()
    return text or None


def _normalize_float(value: Any, default: float | None = None) -> float | None:
    if value is None:
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _normalize_int(value: Any, default: int = 0, min_value: int | None = None, max_value: int | None = None) -> int:
    try:
        result = int(float(value))
    except (TypeError, ValueError):
        result = default
    if min_value is not None:
        result = max(min_value, result)
    if max_value is not None:
        result = min(max_value, result)
    return result


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


def _pick_first_present(*values: Any) -> Any:
    for value in values:
        if value is not None:
            return value
    return None


def build_payload(raw_state: dict[str, Any], default_source: str = "nuc") -> dict[str, Any]:
    now = utc_now_iso()
    pose = raw_state.get("pose") or raw_state.get("robot_pose") or {}
    navigation = raw_state.get("navigation") or raw_state.get("nav_status") or {}
    task = raw_state.get("task") or raw_state.get("task_status") or {}
    device = raw_state.get("device") or raw_state.get("device_status") or {}
    environment = raw_state.get("environment") or raw_state.get("env_sensor") or {}
    alerts = raw_state.get("alerts") or []

    payload = {
        "robot_pose": {
            "x": _normalize_float(pose.get("x"), 0.0),
            "y": _normalize_float(pose.get("y"), 0.0),
            "yaw": _normalize_float(pose.get("yaw"), 0.0),
            "frame_id": _normalize_optional_str(pose.get("frame_id")) or "map",
            "timestamp": _normalize_optional_str(pose.get("timestamp")) or now,
        },
        "nav_status": {
            "mode": "manual"
            if str(navigation.get("mode", "auto")).strip().lower() == "manual"
            else "auto",
            "state": _normalize_enum(
                _pick_first_present(navigation.get("status"), navigation.get("state")),
                NAV_STATE_MAP,
                "idle",
            ),
            "current_goal": _normalize_optional_str(
                _pick_first_present(navigation.get("goal_id"), navigation.get("current_goal"))
            ),
            "remaining_distance": _normalize_float(
                _pick_first_present(
                    navigation.get("remaining_distance_m"),
                    navigation.get("remaining_distance"),
                ),
                None,
            ),
        },
        "task_status": {
            "task_id": _normalize_optional_str(_pick_first_present(task.get("id"), task.get("task_id")))
            or "task-idle",
            "task_type": _normalize_enum(
                _pick_first_present(task.get("type"), task.get("task_type")),
                TASK_TYPE_MAP,
                "placeholder",
            ),
            "state": _normalize_enum(
                _pick_first_present(task.get("status"), task.get("state")),
                TASK_STATE_MAP,
                "idle",
            ),
            "progress": _normalize_int(
                _pick_first_present(task.get("progress_percent"), task.get("progress")),
                default=0,
                min_value=0,
                max_value=100,
            ),
            "source": _normalize_optional_str(task.get("source")) or default_source,
        },
        "device_status": {
            "battery_percent": _normalize_int(
                device.get("battery_percent"),
                default=100,
                min_value=0,
                max_value=100,
            ),
            "emergency_stop": _normalize_bool(device.get("emergency_stop"), False),
            "fault_code": _normalize_optional_str(device.get("fault_code")),
            "online": _normalize_bool(device.get("online"), True),
        },
        "env_sensor": {
            "temperature_c": _normalize_float(environment.get("temperature_c"), None),
            "humidity_percent": _normalize_float(environment.get("humidity_percent"), None),
            "status": _normalize_enum(environment.get("status"), ENV_STATUS_MAP, "offline"),
        },
        "alerts": [_normalize_alert(alert, now) for alert in alerts if isinstance(alert, dict)],
        "updated_at": _normalize_optional_str(raw_state.get("updated_at")) or now,
    }
    return payload


def _normalize_alert(alert: dict[str, Any], now: str) -> dict[str, Any]:
    return {
        "alert_id": _normalize_optional_str(alert.get("id") or alert.get("alert_id")) or "alert-unknown",
        "level": _normalize_enum(alert.get("severity") or alert.get("level"), ALERT_LEVEL_MAP, "info"),
        "message": _normalize_optional_str(alert.get("text") or alert.get("message")) or "unspecified alert",
        "source": _normalize_optional_str(alert.get("source")) or "nuc",
        "timestamp": _normalize_optional_str(alert.get("time") or alert.get("timestamp")) or now,
        "acknowledged": _normalize_bool(alert.get("ack") if "ack" in alert else alert.get("acknowledged"), False),
    }
