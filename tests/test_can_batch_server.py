import json
import tempfile
import unittest
import struct
from pathlib import Path

from can_batch_server import append_batch, parse_binary_batch


def binary_fixture():
    device = b"can-analyzer-test"
    header = bytearray(24 + len(device))
    header[:4] = b"CBIN"
    struct.pack_into("<BBHHHIIB", header, 4, 1, 0, len(header), 24, 2, 8, 1234, len(device))
    header[24:] = device
    records = bytearray(48)
    struct.pack_into("<IIIBBBB", records, 0, 10, 100, 0x107, 0, 3, 0, 0)
    records[16:19] = b"\x01\x02\x03"
    struct.pack_into("<IIIBBBB", records, 24, 11, 101, 0x212, 1, 2, 0, 0)
    records[40:42] = b"\xAA\xBB"
    return bytes(header + records)


class CanBatchServerTest(unittest.TestCase):
    def test_parse_binary_batch_matches_json_contract(self):
        payload = parse_binary_batch(binary_fixture())
        self.assertEqual("can-analyzer-test", payload["device_id"])
        self.assertEqual(8, payload["batch_seq"])
        self.assertEqual(1234, payload["uptime_ms"])
        self.assertEqual(
            [
                {"seq": 10, "bus": "CAN_A", "ts": 100, "id": 0x107, "dlc": 3, "data": "01 02 03"},
                {"seq": 11, "bus": "CAN_B", "ts": 101, "id": 0x212, "dlc": 2, "data": "AA BB"},
            ], payload["frames"])

    def test_binary_parser_rejects_invalid_fields(self):
        cases = []
        bad = bytearray(binary_fixture()); bad[:4] = b"NOPE"; cases.append((bad, "invalid_magic"))
        bad = bytearray(binary_fixture()); bad[4] = 2; cases.append((bad, "unsupported_version"))
        bad = bytearray(binary_fixture()); bad[24 + len(b"can-analyzer-test") + 12] = 8; cases.append((bad, "invalid_record"))
        bad = bytearray(binary_fixture()); bad[24 + len(b"can-analyzer-test") + 19] = 1; cases.append((bad, "invalid_padding"))
        for body, error in cases:
            with self.subTest(error=error), self.assertRaisesRegex(ValueError, error):
                parse_binary_batch(bytes(body))

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
