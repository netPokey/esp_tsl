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

    def test_analyzer_payload_preserves_repeated_frames_and_bus(self):
        payload = {
            "device_id": "can-analyzer-test",
            "batch_seq": 8,
            "frames": [
                {"seq": 10, "bus": "CAN_A", "ts": 100, "id": 0x107, "dlc": 8, "data": "00 01 02 03 04 05 06 07"},
                {"seq": 11, "bus": "CAN_B", "ts": 101, "id": 0x107, "dlc": 8, "data": "10 11 12 13 14 15 16 17"},
            ],
        }
        with tempfile.TemporaryDirectory() as tmp:
            result = append_batch(Path(tmp), payload, server_ts="2026-08-04T00:00:00Z")
            lines = [json.loads(line) for line in (Path(tmp) / "can_frames.ndjson").read_text(encoding="utf-8").splitlines()]

        self.assertEqual({"ok": True, "frames": 2}, result)
        self.assertEqual([10, 11], [line["seq"] for line in lines])
        self.assertEqual(["CAN_A", "CAN_B"], [line["bus"] for line in lines])
        self.assertEqual([0x107, 0x107], [line["id"] for line in lines])
        self.assertNotEqual(lines[0]["data"], lines[1]["data"])


if __name__ == "__main__":
    unittest.main()
