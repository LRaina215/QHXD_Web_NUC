import unittest

from nuc_state_uploader.runtime_state import RuntimeStateStore


class RuntimeStateStoreTest(unittest.TestCase):
    def test_go_to_waypoint_updates_runtime_state(self) -> None:
        store = RuntimeStateStore("/tmp/qhxd_nuc_runtime_state_test.json")
        store.reset()

        response = store.apply_command(
            "go_to_waypoint",
            {"waypoint_id": "wp-bridge-001"},
            "web",
            "dashboard",
        )
        state = store.load()

        self.assertTrue(response["data"]["accepted"])
        self.assertEqual(response["data"]["command"], "go_to_waypoint")
        self.assertEqual(response["data"]["current_goal"], "wp-bridge-001")
        self.assertEqual(state["navigation"]["goal_id"], "wp-bridge-001")
        self.assertEqual(state["task"]["type"], "go_to_waypoint")
        self.assertEqual(state["task"]["status"], "running")

    def test_pause_and_resume_preserve_task(self) -> None:
        store = RuntimeStateStore("/tmp/qhxd_nuc_runtime_state_test.json")
        store.reset()
        store.apply_command("go_to_waypoint", {"waypoint_id": "wp-bridge-001"}, "web", "dashboard")

        pause_response = store.apply_command("pause_task", {}, "web", "dashboard")
        resume_response = store.apply_command("resume_task", {}, "web", "dashboard")
        state = store.load()

        self.assertTrue(pause_response["data"]["accepted"])
        self.assertEqual(pause_response["data"]["task_status"]["state"], "paused")
        self.assertTrue(resume_response["data"]["accepted"])
        self.assertEqual(resume_response["data"]["task_status"]["state"], "running")
        self.assertEqual(state["task"]["type"], "go_to_waypoint")
        self.assertEqual(state["navigation"]["status"], "running")


if __name__ == "__main__":
    unittest.main()
