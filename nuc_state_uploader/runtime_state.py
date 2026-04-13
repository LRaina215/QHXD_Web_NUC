from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path
from typing import Any

from .state_mapper import utc_now_iso


COMMAND_ALIASES = {
    "pause": "pause_task",
    "resume": "resume_task",
}

SUPPORTED_COMMANDS = {
    "go_to_waypoint",
    "start_patrol",
    "pause_task",
    "resume_task",
    "return_home",
}


def default_runtime_state() -> dict[str, Any]:
    return {
        "task": {
            "id": "task-idle",
            "type": "placeholder",
            "status": "idle",
            "progress_percent": 0,
            "source": "nuc",
        },
        "navigation": {
            "mode": "auto",
            "status": "idle",
            "goal_id": None,
            "remaining_distance_m": None,
        },
        "alerts": [],
        "updated_at": utc_now_iso(),
        "command_history": [],
    }


class RuntimeStateStore:
    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)

    def load(self) -> dict[str, Any]:
        if not self.path.exists():
            return default_runtime_state()
        try:
            with self.path.open("r", encoding="utf-8") as handle:
                payload = json.load(handle)
        except (json.JSONDecodeError, OSError):
            return default_runtime_state()
        if not isinstance(payload, dict):
            return default_runtime_state()
        return _merge_defaults(payload)

    def save(self, payload: dict[str, Any]) -> None:
        merged = _merge_defaults(payload)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        fd, tmp_name = tempfile.mkstemp(
            prefix=f"{self.path.name}.",
            suffix=".tmp",
            dir=str(self.path.parent),
        )
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as handle:
                json.dump(merged, handle, ensure_ascii=False, indent=2)
            Path(tmp_name).replace(self.path)
        finally:
            tmp_path = Path(tmp_name)
            if tmp_path.exists():
                tmp_path.unlink()

    def reset(self) -> dict[str, Any]:
        state = default_runtime_state()
        self.save(state)
        return state

    def apply_command(
        self,
        command: str,
        payload: dict[str, Any] | None,
        source: str,
        requested_by: str | None,
    ) -> dict[str, Any]:
        normalized_command = COMMAND_ALIASES.get(command, command)
        now = utc_now_iso()
        state = self.load()
        request_payload = payload or {}
        task = dict(state.get("task", {}))
        navigation = dict(state.get("navigation", {}))

        accepted = normalized_command in SUPPORTED_COMMANDS
        detail = f"NUC 暂不支持 {command} 命令。"

        if accepted:
            if normalized_command == "go_to_waypoint":
                goal = str(request_payload.get("waypoint_id") or request_payload.get("goal_id") or "unknown-waypoint")
                task = {
                    "id": "nuc-task-go-to-waypoint",
                    "type": "go_to_waypoint",
                    "status": "running",
                    "progress_percent": 10,
                    "source": "nuc",
                }
                navigation.update(
                    {
                        "mode": "auto",
                        "status": "running",
                        "goal_id": goal,
                        "remaining_distance_m": 5.0,
                    }
                )
                detail = "NUC 已受理 go_to_waypoint 命令。"
            elif normalized_command == "start_patrol":
                patrol_id = str(request_payload.get("patrol_id") or "default-patrol")
                task = {
                    "id": "nuc-task-start-patrol",
                    "type": "start_patrol",
                    "status": "running",
                    "progress_percent": 5,
                    "source": "nuc",
                }
                navigation.update(
                    {
                        "mode": "auto",
                        "status": "running",
                        "goal_id": patrol_id,
                        "remaining_distance_m": 8.0,
                    }
                )
                detail = "NUC 已受理 start_patrol 命令。"
            elif normalized_command == "pause_task":
                task["status"] = "paused"
                navigation["status"] = "paused"
                detail = "NUC 已受理 pause_task 命令。"
            elif normalized_command == "resume_task":
                task["status"] = "running"
                navigation["status"] = "running"
                detail = "NUC 已受理 resume_task 命令。"
            elif normalized_command == "return_home":
                task = {
                    "id": "nuc-task-return-home",
                    "type": "return_home",
                    "status": "running",
                    "progress_percent": 15,
                    "source": "nuc",
                }
                navigation.update(
                    {
                        "mode": "auto",
                        "status": "running",
                        "goal_id": "home",
                        "remaining_distance_m": 3.0,
                    }
                )
                detail = "NUC 已受理 return_home 命令。"

        current_goal = navigation.get("goal_id")
        nav_state = navigation.get("status", "idle")

        history = list(state.get("command_history", []))
        history.append(
            {
                "command": normalized_command,
                "source": source,
                "requested_by": requested_by,
                "payload": request_payload,
                "received_at": now,
                "accepted": accepted,
            }
        )

        state.update(
            {
                "task": task,
                "navigation": navigation,
                "updated_at": now,
                "command_history": history[-20:],
            }
        )
        self.save(state)

        return {
            "success": True,
            "data": {
                "accepted": accepted,
                "command": normalized_command,
                "task_status": {
                    "task_id": task.get("id", "task-idle"),
                    "task_type": task.get("type", "placeholder"),
                    "state": task.get("status", "idle"),
                    "progress": int(task.get("progress_percent", 0)),
                    "source": task.get("source", "nuc"),
                },
                "current_goal": current_goal,
                "nav_state": nav_state,
                "received_at": now,
                "detail": detail,
            },
        }


def _merge_defaults(payload: dict[str, Any]) -> dict[str, Any]:
    merged = default_runtime_state()
    for key, value in payload.items():
        merged[key] = value
    if not isinstance(merged.get("task"), dict):
        merged["task"] = default_runtime_state()["task"]
    if not isinstance(merged.get("navigation"), dict):
        merged["navigation"] = default_runtime_state()["navigation"]
    if not isinstance(merged.get("alerts"), list):
        merged["alerts"] = []
    if not isinstance(merged.get("command_history"), list):
        merged["command_history"] = []
    return merged
