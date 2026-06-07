import tempfile
import unittest
from pathlib import Path

from scripts.generate_can_signal_map import build_web_ui_header, parse_dbc_files


class GenerateCanSignalMapTest(unittest.TestCase):
    def test_parse_dbc_files_extracts_frame_metadata_and_signals(self):
        with tempfile.TemporaryDirectory() as tmp:
            dbc = Path(tmp) / "sample.dbc"
            dbc.write_text(
                "\n".join(
                    [
                        'VERSION ""',
                        'BU_: ETH',
                        'BO_ 306 BMS_hvBusStatus: 8 Vector__XXX',
                        ' SG_ BMS_packVoltage : 0|16@1+ (0.01,0) [0|0] "V" Vector__XXX',
                        ' SG_ BMS_packCurrent : 16|15@1- (-0.1,0) [0|0] "A" Vector__XXX',
                        'VAL_ 306 BMS_packCurrent 32768 "SNA";',
                        '',
                    ]
                ),
                encoding="utf-8",
            )

            frames = parse_dbc_files([dbc])

        self.assertEqual(
            frames["306"],
            {
                "id": 306,
                "hex": "0x132",
                "dbc": "sample.dbc",
                "frame_name": "BMS_hvBusStatus",
                "dlc": 8,
                "module": "BMS",
                "meaning": "电池与高压系统",
                "signals": [
                    "BMS_packVoltage",
                    "BMS_packCurrent",
                ],
                "signal_count": 2,
            },
        )

    def test_build_web_ui_header_outputs_macro_literal(self):
        header = build_web_ui_header(
            {
                "306": {
                    "frame_name": "BMS_hvBusStatus",
                    "meaning": "电池与高压系统",
                    "signals": ["BMS_packVoltage"],
                    "dbc": "sample.dbc",
                }
            },
            max_signals=8,
        )

        self.assertTrue(header.startswith("#pragma once\n\n#define CAN_SIGNAL_MAP_JS R\"rawliteral("))
        self.assertIn("const canSignalMap=", header)
        self.assertIn('"306":["BMS_hvBusStatus","电池与高压系统",["BMS_packVoltage"],"sample.dbc"]', header)
        self.assertTrue(header.endswith(")rawliteral\"\n"))

    def test_web_ui_renders_dbc_lookup_columns(self):
        ui = Path("include/web/web_ui.h").read_text(encoding="utf-8")

        self.assertIn("can_signal_map_js.h", ui)
        self.assertIn("<th>帧名</th><th>含义</th><th>DBC字段</th>", ui)
        self.assertIn("function captureInfo", ui)
        self.assertIn("canSignalMap", ui)


if __name__ == "__main__":
    unittest.main()
