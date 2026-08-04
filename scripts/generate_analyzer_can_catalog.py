#!/usr/bin/env python3
"""校验 Analyzer CAN 目录并生成固件与浏览器使用的静态文件。"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CATALOG = REPO_ROOT / "config" / "analyzer_can_catalog.json"
DEFAULT_HEADER = REPO_ROOT / "include" / "generated" / "analyzer_critical_ids.h"
DEFAULT_BROWSER = REPO_ROOT / "data" / "analyzer" / "can_catalog.js"
EXPECTED_IDS = {
    0x082, 0x0A9, 0x102, 0x103, 0x107, 0x118, 0x129, 0x132,
    0x145, 0x189, 0x1F9, 0x20C, 0x212, 0x21C, 0x229, 0x238,
    0x243, 0x249, 0x257, 0x25A, 0x25D, 0x266, 0x273, 0x292,
    0x293, 0x2B4, 0x2B6, 0x2E1, 0x2E5, 0x2F3, 0x31F, 0x321,
    0x332, 0x333, 0x334, 0x339, 0x33A, 0x352, 0x370, 0x37A,
    0x389, 0x399, 0x39B, 0x39D, 0x3A1, 0x3B3, 0x3B6, 0x3C2,
    0x3C3, 0x3D2, 0x3D8, 0x3DF, 0x3E2, 0x3E3, 0x3E9, 0x3EA,
    0x3F5, 0x3FD, 0x3FE, 0x401, 0x405, 0x498, 0x4E2, 0x4E3,
    0x4F3, 0x678, 0x679, 0x68C, 0x7FF,
}
EXPECTED_COUNT = len(EXPECTED_IDS)
REQUIRED_IDS = {0x107, 0x212, 0x389}
CONFLICT_IDS = {0x25D, 0x3FE}
SUPPORTED_IDS = {
    0x082, 0x0A9, 0x102, 0x103, 0x107, 0x118, 0x145, 0x20C,
    0x212, 0x21C, 0x238, 0x243, 0x249, 0x257, 0x25A, 0x266,
    0x2E1, 0x2E5, 0x2F3, 0x31F, 0x321, 0x339, 0x33A, 0x389,
    0x39D, 0x3B6, 0x3D2, 0x3D8, 0x3E2, 0x3E3, 0x3F5, 0x405,
    0x4F3, 0x679,
}
VALID_STATUSES = {"listed", "supported", "unsupported_conflict"}


class CatalogValidationError(ValueError):
    """目录内容不满足生成约束。"""


def _fail(message: str) -> None:
    raise CatalogValidationError(message)


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def validate_signal(signal: Any, dlc: int, message_hex: str, index: int) -> None:
    location = f"{message_hex}.signals[{index}]"
    if not isinstance(signal, dict):
        _fail(f"{location} 必须是对象")

    required = {"name", "start", "length", "byte_order", "signed", "factor", "offset", "enum"}
    missing = required - signal.keys()
    if missing:
        _fail(f"{location} 缺少字段: {', '.join(sorted(missing))}")

    if not isinstance(signal["name"], str) or not signal["name"].strip():
        _fail(f"{location}.name 必须是非空字符串")
    if not isinstance(signal["start"], int) or isinstance(signal["start"], bool) or signal["start"] < 0:
        _fail(f"{location}.start 必须是非负整数")
    if not isinstance(signal["length"], int) or isinstance(signal["length"], bool) or signal["length"] <= 0:
        _fail(f"{location}.length 必须是正整数")
    if signal["start"] + signal["length"] > dlc * 8:
        _fail(f"{location} 超出 DLC: start + length > {dlc * 8}")
    if signal["byte_order"] != "intel":
        _fail(f"{location}.byte_order 当前仅支持 intel")
    if not isinstance(signal["signed"], bool):
        _fail(f"{location}.signed 必须是布尔值")
    if not _is_number(signal["factor"]):
        _fail(f"{location}.factor 必须是有限数值")
    if not _is_number(signal["offset"]):
        _fail(f"{location}.offset 必须是有限数值")
    if not isinstance(signal["enum"], dict):
        _fail(f"{location}.enum 必须是对象")
    for raw, label in signal["enum"].items():
        try:
            parsed = int(raw, 10)
        except (TypeError, ValueError):
            _fail(f"{location}.enum 的键必须是十进制整数字符串")
        if str(parsed) != raw or (parsed < 0 and not signal["signed"]):
            _fail(f"{location}.enum 的键必须是与 signed 属性一致的规范十进制整数字符串")
        if not isinstance(label, str) or not label:
            _fail(f"{location}.enum[{raw!r}] 必须是非空字符串")


def validate_catalog(catalog: Any) -> dict[str, Any]:
    if not isinstance(catalog, dict):
        _fail("目录根节点必须是对象")
    messages = catalog.get("messages")
    if not isinstance(messages, list):
        _fail("messages 必须是数组")
    if len(messages) != EXPECTED_COUNT:
        _fail(f"目录必须正好包含 {EXPECTED_COUNT} 项，当前为 {len(messages)} 项")

    seen: set[int] = set()
    for index, message in enumerate(messages):
        location = f"messages[{index}]"
        if not isinstance(message, dict):
            _fail(f"{location} 必须是对象")
        required = {"id", "hex", "name", "dlc", "status", "signals"}
        missing = required - message.keys()
        if missing:
            _fail(f"{location} 缺少字段: {', '.join(sorted(missing))}")

        can_id = message["id"]
        if not isinstance(can_id, int) or isinstance(can_id, bool) or not 0 <= can_id <= 0x7FF:
            _fail(f"{location}.id 必须在 11-bit CAN 范围 0x000..0x7FF")
        if can_id in seen:
            _fail(f"CAN ID 重复: 0x{can_id:03X}")
        seen.add(can_id)

        expected_hex = f"0x{can_id:03X}"
        if message["hex"] != expected_hex:
            _fail(f"{location}.hex 必须为 {expected_hex}")
        if not isinstance(message["name"], str) or not message["name"].strip():
            _fail(f"{location}.name 必须是非空字符串")
        dlc = message["dlc"]
        if not isinstance(dlc, int) or isinstance(dlc, bool) or not 0 <= dlc <= 8:
            _fail(f"{location}.dlc 必须是 0..8 的整数")
        if not isinstance(message["status"], str) or message["status"] not in VALID_STATUSES:
            _fail(f"{location}.status 必须是 {sorted(VALID_STATUSES)} 之一")
        if message["status"] == "unsupported_conflict" and not message.get("note"):
            _fail(f"{location} 的冲突状态必须提供 note")
        if not isinstance(message["signals"], list):
            _fail(f"{location}.signals 必须是数组")
        if message["status"] == "supported" and not message["signals"]:
            _fail(f"{location} 的 supported 状态必须包含信号")
        signal_names: set[str] = set()
        for signal_index, signal in enumerate(message["signals"]):
            validate_signal(signal, dlc, expected_hex, signal_index)
            signal_name = signal["name"]
            if signal_name in signal_names:
                _fail(f"{expected_hex} 信号名重复: {signal_name}")
            signal_names.add(signal_name)

    if seen != EXPECTED_IDS:
        missing = EXPECTED_IDS - seen
        unexpected = seen - EXPECTED_IDS
        parts = []
        if missing:
            parts.append("缺少 " + ", ".join(f"0x{can_id:03X}" for can_id in sorted(missing)))
        if unexpected:
            parts.append("多出 " + ", ".join(f"0x{can_id:03X}" for can_id in sorted(unexpected)))
        _fail("CAN ID 集合不匹配: " + "；".join(parts))
    missing_required = REQUIRED_IDS - seen
    if missing_required:
        rendered = ", ".join(f"0x{can_id:03X}" for can_id in sorted(missing_required))
        _fail(f"缺少关键额外项: {rendered}")
    if sum(message["id"] == 0x3E2 for message in messages) != 1:
        _fail("0x3E2 必须且只能出现一次")

    by_id = {message["id"]: message for message in messages}
    for can_id in CONFLICT_IDS:
        if by_id.get(can_id, {}).get("status") != "unsupported_conflict":
            _fail(f"0x{can_id:03X} 必须标记为 unsupported_conflict")
    for can_id in SUPPORTED_IDS:
        if by_id.get(can_id, {}).get("status") != "supported":
            _fail(f"0x{can_id:03X} 必须标记为 supported")
        if not by_id[can_id]["signals"]:
            _fail(f"0x{can_id:03X} 的 supported 状态必须包含信号")

    if [message["id"] for message in messages] != sorted(seen):
        _fail("messages 必须按 CAN ID 升序排列")
    return catalog


def load_catalog(path: Path) -> dict[str, Any]:
    try:
        catalog = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        _fail(f"无法读取目录 {path}: {exc}")
    return validate_catalog(catalog)


def build_header(catalog: dict[str, Any]) -> str:
    ids = [message["id"] for message in catalog["messages"]]
    rows = []
    for offset in range(0, len(ids), 8):
        values = ", ".join(f"0x{can_id:03X}" for can_id in ids[offset : offset + 8])
        rows.append(f"    {values},")
    return (
        "// 此文件由 scripts/generate_analyzer_can_catalog.py 生成，请勿手工修改。\n"
        "#pragma once\n\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n\n"
        "namespace analyzer {\n\n"
        "constexpr uint16_t kAnalyzerCriticalCanIds[] = {\n"
        + "\n".join(rows)
        + "\n};\n\n"
        "constexpr size_t kAnalyzerCriticalCanIdCount =\n"
        "    sizeof(kAnalyzerCriticalCanIds) / sizeof(kAnalyzerCriticalCanIds[0]);\n\n"
        "inline bool isAnalyzerCriticalCanId(uint32_t id)\n"
        "{\n"
        "    size_t low = 0;\n"
        "    size_t high = kAnalyzerCriticalCanIdCount;\n"
        "    while (low < high)\n"
        "    {\n"
        "        const size_t mid = low + (high - low) / 2;\n"
        "        const uint16_t candidate = kAnalyzerCriticalCanIds[mid];\n"
        "        if (candidate == id)\n"
        "            return true;\n"
        "        if (candidate < id)\n"
        "            low = mid + 1;\n"
        "        else\n"
        "            high = mid;\n"
        "    }\n"
        "    return false;\n"
        "}\n\n"
        "}  // namespace analyzer\n"
    )


def build_browser_script(catalog: dict[str, Any]) -> str:
    payload = json.dumps(catalog, ensure_ascii=False, separators=(",", ":"))
    return (
        "// 此文件由 scripts/generate_analyzer_can_catalog.py 生成，请勿手工修改。\n"
        "(function(root, factory) {\n"
        "  var catalog = factory();\n"
        "  if (root) root.ANALYZER_CAN_CATALOG = catalog;\n"
        "  if (typeof module === \"object\" && module.exports) module.exports = catalog;\n"
        "})(typeof globalThis !== \"undefined\" ? globalThis : this, function() {\n"
        f"  return {payload};\n"
        "});\n"
    )


def generated_outputs(catalog: dict[str, Any], header: Path, browser: Path) -> dict[Path, str]:
    return {header: build_header(catalog), browser: build_browser_script(catalog)}


def write_or_check(outputs: dict[Path, str], check: bool) -> list[Path]:
    drifted: list[Path] = []
    for path, expected in outputs.items():
        if check:
            try:
                actual = path.read_text(encoding="utf-8")
            except OSError:
                drifted.append(path)
                continue
            if actual != expected:
                drifted.append(path)
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(expected, encoding="utf-8")
    return drifted


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="生成 Analyzer 69-ID CAN 目录静态文件。")
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG, help="源 JSON 目录路径。")
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER, help="生成的 C++ 头文件路径。")
    parser.add_argument("--browser", type=Path, default=DEFAULT_BROWSER, help="生成的浏览器脚本路径。")
    parser.add_argument("--check", action="store_true", help="只检查生成物是否最新，不写文件。")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        catalog = load_catalog(args.catalog)
    except CatalogValidationError as exc:
        print(f"catalog validation failed: {exc}", file=sys.stderr)
        return 1

    drifted = write_or_check(generated_outputs(catalog, args.header, args.browser), args.check)
    if drifted:
        for path in drifted:
            print(f"generated file is out of date: {path}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
