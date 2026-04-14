import unittest

from nuc_state_uploader.imu_bridge import (
    EulerDegSample,
    NormalizedImuSample,
    QuaternionSample,
    Vector3Sample,
    ros_time_to_iso,
)
from nuc_state_uploader.state_collector import MockStateCollector


class _FakeLowLevelCollector:
    def __init__(self, imu_sample: NormalizedImuSample | None) -> None:
        self._imu_sample = imu_sample

    def collect(self, seq: int):  # pragma: no cover - not used by this test
        raise NotImplementedError

    def latest_imu_sample(self) -> NormalizedImuSample | None:
        return self._imu_sample


class ImuBridgeTest(unittest.TestCase):
    def test_ros_time_to_iso_formats_fractional_utc_time(self) -> None:
        self.assertEqual(ros_time_to_iso(1, 500_000_000), "1970-01-01T00:00:01.500000Z")

    def test_normalized_imu_payload_shape_matches_bridge_contract(self) -> None:
        sample = NormalizedImuSample(
            frame_id="gimbal_pitch_odom",
            timestamp="2026-04-15T00:00:00.123456Z",
            orientation=QuaternionSample(x=0.0, y=0.0, z=0.1, w=0.99),
            angular_velocity=Vector3Sample(x=0.1, y=0.2, z=0.3),
            linear_acceleration=Vector3Sample(x=1.0, y=2.0, z=3.0),
            euler_deg=EulerDegSample(yaw=11.0, pitch=-22.0, roll=33.0),
            source="rtt",
        )

        payload = sample.as_payload()

        self.assertEqual(payload["source"], "rtt")
        self.assertEqual(payload["updated_at"], "2026-04-15T00:00:00.123456Z")
        self.assertEqual(payload["imu"]["frame_id"], "gimbal_pitch_odom")
        self.assertEqual(payload["imu"]["angular_velocity"]["z"], 0.3)
        self.assertEqual(payload["imu"]["euler_deg"]["yaw"], 11.0)
        self.assertNotIn("source", payload["imu"])

    def test_mock_state_collector_delegates_latest_imu_to_low_level_collector(self) -> None:
        sample = NormalizedImuSample(
            frame_id="imu_link",
            timestamp="2026-04-15T00:00:00.000000Z",
            orientation=QuaternionSample(x=0.0, y=0.0, z=0.0, w=1.0),
            angular_velocity=Vector3Sample(x=0.0, y=0.0, z=0.0),
            linear_acceleration=Vector3Sample(x=0.0, y=0.0, z=9.8),
            source="rtt",
        )
        collector = MockStateCollector(low_level_collector=_FakeLowLevelCollector(sample))

        latest = collector.latest_imu_sample()

        self.assertIsNotNone(latest)
        self.assertEqual(latest.frame_id, "imu_link")


if __name__ == "__main__":
    unittest.main()
