import json
import tempfile
import unittest
from pathlib import Path

from nuc_state_uploader.config import CollectorConfig
from nuc_state_uploader.state_collector import build_collector
from nuc_state_uploader.state_mapper import build_payload


class StateMapperTest(unittest.TestCase):
    def test_build_payload_maps_internal_fields(self) -> None:
        raw_state = {
            "pose": {
                "x": "1.2",
                "y": 3,
                "yaw": "0.5",
                "frame_id": "map",
                "timestamp": "2026-04-12T15:20:30Z",
            },
            "navigation": {
                "mode": "manual",
                "status": "moving",
                "goal_id": "wp-a",
                "remaining_distance_m": "8.8",
            },
            "task": {
                "id": "task-1",
                "type": "goto",
                "status": "executing",
                "progress_percent": 133,
                "source": "nuc",
            },
            "device": {
                "battery_percent": -10,
                "emergency_stop": "true",
                "fault_code": "",
                "online": "yes",
            },
            "environment": {
                "temperature_c": "24.5",
                "humidity_percent": "48.0",
                "status": "ok",
            },
            "alerts": [
                {
                    "id": "alert-1",
                    "severity": "fatal",
                    "text": "motor overloaded",
                    "source": "motor",
                    "time": "2026-04-12T15:20:31Z",
                    "ack": "false",
                }
            ],
            "updated_at": "2026-04-12T15:20:32Z",
        }

        payload = build_payload(raw_state)

        self.assertEqual(payload["robot_pose"]["x"], 1.2)
        self.assertEqual(payload["nav_status"]["mode"], "manual")
        self.assertEqual(payload["nav_status"]["state"], "running")
        self.assertEqual(payload["task_status"]["task_type"], "go_to_waypoint")
        self.assertEqual(payload["task_status"]["state"], "running")
        self.assertEqual(payload["task_status"]["progress"], 100)
        self.assertEqual(payload["device_status"]["battery_percent"], 0)
        self.assertTrue(payload["device_status"]["emergency_stop"])
        self.assertIsNone(payload["device_status"]["fault_code"])
        self.assertEqual(payload["env_sensor"]["status"], "nominal")
        self.assertEqual(payload["alerts"][0]["level"], "critical")
        self.assertFalse(payload["alerts"][0]["acknowledged"])

    def test_build_payload_falls_back_to_defaults(self) -> None:
        payload = build_payload({})

        self.assertEqual(payload["robot_pose"]["frame_id"], "map")
        self.assertEqual(payload["nav_status"]["state"], "idle")
        self.assertEqual(payload["task_status"]["task_id"], "task-idle")
        self.assertEqual(payload["device_status"]["battery_percent"], 100)
        self.assertEqual(payload["env_sensor"]["status"], "offline")
        self.assertEqual(payload["alerts"], [])

    def test_file_collector_reads_json_object(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            state_file = Path(tmp_dir) / "state.json"
            state_file.write_text(json.dumps({"pose": {"x": 1.0}}), encoding="utf-8")

            collector = build_collector(
                CollectorConfig(type="file", state_file=str(state_file), frame_id="map", source_name="nuc")
            )
            raw_state = collector.collect(0)

            self.assertEqual(raw_state["pose"]["x"], 1.0)


if __name__ == "__main__":
    unittest.main()
