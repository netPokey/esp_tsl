#!/usr/bin/env python3
import argparse
import json
import re
from pathlib import Path

MESSAGE_RE = re.compile(r"^BO_\s+(\d+)\s+([^:]+):\s+(\d+)\s+")
SIGNAL_RE = re.compile(r"^\s+SG_\s+([^\s:]+)\s*:")

MEANING_PREFIXES = {
    "BMS": "电池与高压系统",
    "PCS": "电源转换与充电",
    "CP": "充电接口",
    "SCCM": "方向盘与拨杆",
    "EPAS": "转向助力",
    "EPAS3P": "转向助力",
    "EPAS3S": "转向助力",
    "DAS": "驾驶辅助",
    "APP": "自动驾驶与感知",
    "APSB": "自动驾驶告警",
    "PARK": "泊车雷达",
    "VCFRONT": "前车身控制器",
    "VCLEFT": "左车身控制器",
    "VCRIGHT": "右车身控制器",
    "VCSEC": "安全与钥匙系统",
    "VC": "车身控制",
    "GTW": "网关与整车配置",
    "UI": "车机与用户界面",
    "UIS": "车机日志",
    "IBST": "刹车助力",
    "EPBR": "电子驻车制动",
    "DIR": "驱动与扭矩",
    "DI": "驱动逆变器",
    "RCM": "约束控制与碰撞",
    "THC": "热管理",
    "HVP": "高压控制",
    "DCDC": "低压电源转换",
    "ADSP": "音频与座舱处理",
}


def module_from_frame(frame_name):
    return frame_name.split("_", 1)[0] if "_" in frame_name else frame_name


def meaning_for_module(module):
    return MEANING_PREFIXES.get(module, "未分类")


def parse_dbc_files(paths):
    frames = {}
    for path in paths:
        current = None
        with Path(path).open(encoding="utf-8", errors="ignore") as handle:
            for line in handle:
                message_match = MESSAGE_RE.match(line)
                if message_match:
                    frame_id = int(message_match.group(1))
                    frame_name = message_match.group(2)
                    module = module_from_frame(frame_name)
                    current = {
                        "id": frame_id,
                        "hex": f"0x{frame_id:X}",
                        "dbc": Path(path).name,
                        "frame_name": frame_name,
                        "dlc": int(message_match.group(3)),
                        "module": module,
                        "meaning": meaning_for_module(module),
                        "signals": [],
                        "signal_count": 0,
                    }
                    frames[str(frame_id)] = current
                    continue

                if current is None:
                    continue

                signal_match = SIGNAL_RE.match(line)
                if signal_match:
                    current["signals"].append(signal_match.group(1))
                    current["signal_count"] = len(current["signals"])

    return frames


def build_compact_js_map(frames, max_signals):
    compact = {}
    for key, frame in sorted(frames.items(), key=lambda item: int(item[0])):
        compact[key] = [
            frame["frame_name"],
            frame["meaning"],
            frame["signals"][:max_signals],
            frame["dbc"],
        ]
    return "const canSignalMap=" + json.dumps(compact, ensure_ascii=False, separators=(",", ":")) + ";"


def build_web_ui_header(frames, max_signals):
    return '#pragma once\n\n#define CAN_SIGNAL_MAP_JS R"rawliteral(' + build_compact_js_map(frames, max_signals) + ')rawliteral"\n'


def main():
    parser = argparse.ArgumentParser(description="Generate Tesla CAN signal map from DBC files.")
    parser.add_argument("--dbc", action="append", required=True, help="DBC file path. May be repeated.")
    parser.add_argument("--json", required=True, help="Output JSON path.")
    parser.add_argument("--js", required=True, help="Output compact JavaScript path.")
    parser.add_argument("--max-signals", type=int, default=8, help="Signals kept in the compact JS map.")
    args = parser.parse_args()

    frames = parse_dbc_files([Path(path) for path in args.dbc])
    json_path = Path(args.json)
    js_path = Path(args.js)
    json_path.parent.mkdir(parents=True, exist_ok=True)
    js_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(frames, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    js_path.write_text(build_web_ui_header(frames, args.max_signals), encoding="utf-8")


if __name__ == "__main__":
    main()
