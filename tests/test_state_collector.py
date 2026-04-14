import tempfile
import unittest
from pathlib import Path

from nuc_state_uploader.config import CollectorConfig, RttCollectorConfig
from nuc_state_uploader.rtt_state_collector import build_rtt_collector
from nuc_state_uploader.runtime_state import RuntimeStateStore
from nuc_state_uploader.state_collector import MockStateCollector, build_collector
from nuc_state_uploader.state_mapper import build_payload


class MockStateCollectorTest(unittest.TestCase):
    def test_runtime_overrides_do_not_freeze_payload_updated_at(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            runtime_path = Path(tmp_dir) / "mission_state.json"
            store = RuntimeStateStore(runtime_path)
            state = store.reset()
            state["updated_at"] = "2026-04-14T00:00:00Z"
            store.save(state)

            collector = MockStateCollector(runtime_store=store)
            raw_state = collector.collect(3)
            payload = build_payload(raw_state)

            self.assertIsNone(raw_state["updated_at"])
            self.assertNotEqual(payload["updated_at"], "2026-04-14T00:00:00Z")

    def test_rtt_file_collector_normalizes_low_level_state(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            state_file = Path(tmp_dir) / "rtt_state.json"
            state_file.write_text(
                """
                {
                  "battery_percent": "76",
                  "emergency_stop": "true",
                  "fault_code": "motor-overcurrent",
                  "online": "yes",
                  "velocity": "0.62",
                  "temperature_c": null,
                  "humidity_percent": null,
                  "status": "offline"
                }
                """.strip(),
                encoding="utf-8",
            )

            collector = build_rtt_collector(
                RttCollectorConfig(type="file", state_file=str(state_file), source_name="rtt")
            )
            low_level_state = collector.collect(0)

            self.assertEqual(low_level_state.battery_percent, 76)
            self.assertTrue(low_level_state.emergency_stop)
            self.assertEqual(low_level_state.fault_code, "motor-overcurrent")
            self.assertTrue(low_level_state.online)
            self.assertEqual(low_level_state.velocity_mps, 0.62)
            self.assertEqual(low_level_state.env_status, "offline")

    def test_main_collector_merges_rtt_low_level_state_into_raw_state(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            state_file = Path(tmp_dir) / "rtt_state.json"
            state_file.write_text(
                """
                {
                  "battery_percent": 64,
                  "emergency_stop": false,
                  "fault_code": null,
                  "online": true,
                  "velocity_mps": 0.45,
                  "temperature_c": null,
                  "humidity_percent": null,
                  "status": "offline"
                }
                """.strip(),
                encoding="utf-8",
            )

            collector = build_collector(
                CollectorConfig(type="mock", frame_id="map", source_name="nuc"),
                RttCollectorConfig(type="file", state_file=str(state_file), source_name="rtt"),
            )
            raw_state = collector.collect(3)
            payload = build_payload(raw_state)

            self.assertEqual(raw_state["device"]["battery_percent"], 64)
            self.assertEqual(raw_state["device"]["velocity_mps"], 0.45)
            self.assertEqual(payload["device_status"]["battery_percent"], 64)
            self.assertTrue(payload["device_status"]["online"])
            self.assertEqual(payload["env_sensor"]["status"], "offline")

    def test_rtt_file_collector_normalizes_boundaries_and_empty_values(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            state_file = Path(tmp_dir) / "rtt_state.json"
            state_file.write_text(
                """
                {
                  "battery_percent": 150,
                  "emergency_stop": "yes",
                  "fault_code": "",
                  "online": 0,
                  "velocity_mps": "0.00",
                  "temperature_c": null,
                  "humidity_percent": null,
                  "status": "offline"
                }
                """.strip(),
                encoding="utf-8",
            )

            collector = build_rtt_collector(
                RttCollectorConfig(type="file", state_file=str(state_file), source_name="rtt")
            )
            low_level_state = collector.collect(0)

            self.assertEqual(low_level_state.battery_percent, 100)
            self.assertTrue(low_level_state.emergency_stop)
            self.assertIsNone(low_level_state.fault_code)
            self.assertFalse(low_level_state.online)
            self.assertEqual(low_level_state.velocity_mps, 0.0)
            self.assertEqual(low_level_state.env_status, "offline")


if __name__ == "__main__":
    unittest.main()
