import tempfile
import unittest
from pathlib import Path

from nuc_state_uploader.runtime_state import RuntimeStateStore
from nuc_state_uploader.state_collector import MockStateCollector
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


if __name__ == "__main__":
    unittest.main()
