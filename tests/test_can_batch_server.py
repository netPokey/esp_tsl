import json
import tempfile
import unittest
from pathlib import Path

from can_batch_server import append_batch


class CanBatchServerTest(unittest.TestCase):
    def test_append_batch_records_batch_and_expanded_frames(self):
        payload = {
            "device_id": "esp32-test",
            "uptime_ms": 12345,
            "batch_seq": 7,
            "frames": [
                {"seq": 1, "bus": "CAN_A", "ts": 100, "id": 297, "dlc": 8, "data": "87 20 5A 60 00 20 FF 3F"},
                {"seq": 2, "bus": "CAN_B", "ts": 101, "id": 306, "dlc": 6, "data": "CB 8B E1 FF F5 3F"},
            ],
        }

        with tempfile.TemporaryDirectory() as tmp:
            result = append_batch(Path(tmp), payload, server_ts="2026-06-07T00:00:00Z")
            batch_lines = (Path(tmp) / "can_batches.ndjson").read_text(encoding="utf-8").splitlines()
            frame_lines = (Path(tmp) / "can_frames.ndjson").read_text(encoding="utf-8").splitlines()

        self.assertEqual(result, {"ok": True, "frames": 2})
        self.assertEqual(len(batch_lines), 1)
        self.assertEqual(len(frame_lines), 2)
        self.assertEqual(json.loads(batch_lines[0])["payload"], payload)
        self.assertEqual(
            json.loads(frame_lines[0]),
            {
                "server_ts": "2026-06-07T00:00:00Z",
                "device_id": "esp32-test",
                "batch_seq": 7,
                "seq": 1,
                "bus": "CAN_A",
                "ts": 100,
                "id": 297,
                "dlc": 8,
                "data": "87 20 5A 60 00 20 FF 3F",
            },
        )


if __name__ == "__main__":
    unittest.main()
