#!/usr/bin/env python3
import argparse
import json
import struct
import threading
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


def utc_now_iso():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


WRITE_LOCK = threading.Lock()
BIN_HEADER_SIZE = 24
BIN_RECORD_SIZE = 24
BIN_MAX_BODY = BIN_HEADER_SIZE + 63 + 200 * BIN_RECORD_SIZE


def parse_binary_batch(body):
    if not isinstance(body, (bytes, bytearray)) or len(body) < BIN_HEADER_SIZE:
        raise ValueError("invalid_length")
    if len(body) > BIN_MAX_BODY:
        raise ValueError("body_too_large")
    if body[:4] != b"CBIN":
        raise ValueError("invalid_magic")
    version, flags, header_len, record_len, count, batch_seq, uptime_ms, device_len = struct.unpack_from(
        "<BBHHHIIB", body, 4
    )
    if version != 1:
        raise ValueError("unsupported_version")
    if flags != 0 or body[21:24] != b"\0\0\0":
        raise ValueError("invalid_flags")
    if not 1 <= device_len <= 63 or header_len != BIN_HEADER_SIZE + device_len:
        raise ValueError("invalid_header")
    if record_len != BIN_RECORD_SIZE or not 1 <= count <= 200:
        raise ValueError("invalid_header")
    if len(body) != header_len + count * record_len:
        raise ValueError("invalid_length")
    try:
        device_id = body[BIN_HEADER_SIZE:header_len].decode("ascii")
    except UnicodeDecodeError as exc:
        raise ValueError("invalid_device_id") from exc
    if any(ord(char) < 0x21 or ord(char) > 0x7E for char in device_id):
        raise ValueError("invalid_device_id")

    frames = []
    for index in range(count):
        offset = header_len + index * record_len
        seq, timestamp_ms, can_id, bus, dlc, frame_flags, reserved = struct.unpack_from("<IIIBBBB", body, offset)
        data = body[offset + 16:offset + 24]
        if bus not in (0, 1) or dlc > 8 or can_id > 0x7FF or frame_flags or reserved:
            raise ValueError("invalid_record")
        if any(data[dlc:]):
            raise ValueError("invalid_padding")
        frames.append({
            "seq": seq,
            "bus": "CAN_A" if bus == 0 else "CAN_B",
            "ts": timestamp_ms,
            "id": can_id,
            "dlc": dlc,
            "data": " ".join(f"{value:02X}" for value in data[:dlc]),
        })
    return {"device_id": device_id, "uptime_ms": uptime_ms, "batch_seq": batch_seq, "frames": frames}


def append_json_line(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n")


def append_batch(log_dir, payload, server_ts=None):
    server_ts = server_ts or utc_now_iso()
    frames = payload.get("frames", [])
    if not isinstance(frames, list):
        raise ValueError("frames must be a list")

    log_dir = Path(log_dir)
    device_id = payload.get("device_id", "unknown")
    batch_seq = payload.get("batch_seq", 0)
    with WRITE_LOCK:
        append_json_line(log_dir / "can_batches.ndjson", {"server_ts": server_ts, "payload": payload})
        for frame in frames:
            append_json_line(
                log_dir / "can_frames.ndjson",
                {
                    "server_ts": server_ts,
                    "device_id": device_id,
                    "batch_seq": batch_seq,
                    "seq": frame.get("seq", 0),
                    "bus": frame.get("bus", "UNKNOWN"),
                    "ts": frame.get("ts", 0),
                    "id": frame.get("id", 0),
                    "dlc": frame.get("dlc", 0),
                    "data": frame.get("data", ""),
                },
            )

    return {"ok": True, "frames": len(frames)}


class CanBatchHandler(BaseHTTPRequestHandler):
    log_dir = Path("can_logs")

    def do_POST(self):
        if self.path not in ("/can/batch", "/can/batch-bin"):
            self.send_json(404, {"ok": False, "error": "not found"})
            return
        length_text = self.headers.get("Content-Length")
        if length_text is None:
            self.send_json(411, {"ok": False, "error": "content_length_required"})
            return
        try:
            length = int(length_text)
        except ValueError:
            self.send_json(400, {"ok": False, "error": "invalid_length"})
            return
        if self.path == "/can/batch-bin" and length > BIN_MAX_BODY:
            self.send_json(413, {"ok": False, "error": "body_too_large"})
            return

        try:
            body = self.rfile.read(length)
            if len(body) != length:
                raise ValueError("invalid_length")
            if self.path == "/can/batch-bin":
                content_type = self.headers.get("Content-Type", "").split(";", 1)[0].strip()
                if content_type not in ("application/vnd.teslacan.can-batch-v1+octet-stream", "application/octet-stream"):
                    self.send_json(415, {"ok": False, "error": "unsupported_media_type"})
                    return
                payload = parse_binary_batch(body)
            else:
                payload = json.loads(body.decode("utf-8"))
            result = append_batch(self.log_dir, payload)
            self.send_json(200, result)
        except Exception as exc:
            self.send_json(400, {"ok": False, "error": str(exc)})

    def do_GET(self):
        if self.path == "/health":
            self.send_json(200, {"ok": True})
            return
        self.send_json(404, {"ok": False, "error": "not found"})

    def send_json(self, status, payload):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        print(f"[{utc_now_iso()}] {self.client_address[0]} {fmt % args}")


def main():
    parser = argparse.ArgumentParser(description="Receive ESP32 Tesla CAN batch uploads.")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=48601)
    parser.add_argument("--log-dir", default="can_logs")
    args = parser.parse_args()

    CanBatchHandler.log_dir = Path(args.log_dir)
    server = ThreadingHTTPServer((args.host, args.port), CanBatchHandler)
    print(f"CAN batch server listening on http://{args.host}:{args.port}/can/batch")
    print(f"Binary endpoint: http://{args.host}:{args.port}/can/batch-bin")
    print(f"Writing logs to {CanBatchHandler.log_dir}")
    server.serve_forever()


if __name__ == "__main__":
    main()
