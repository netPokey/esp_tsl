#!/usr/bin/env python3
import argparse
import json
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


def utc_now_iso():
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


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
    append_json_line(log_dir / "can_batches.ndjson", {"server_ts": server_ts, "payload": payload})

    device_id = payload.get("device_id", "unknown")
    batch_seq = payload.get("batch_seq", 0)
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
        if self.path != "/can/batch":
            self.send_json(404, {"ok": False, "error": "not found"})
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
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
    print(f"Writing logs to {CanBatchHandler.log_dir}")
    server.serve_forever()


if __name__ == "__main__":
    main()
