import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from scripts.generate_analyzer_can_catalog import (
    DEFAULT_BROWSER,
    DEFAULT_CATALOG,
    DEFAULT_HEADER,
    SUPPORTED_IDS,
    CatalogValidationError,
    build_browser_script,
    build_header,
    load_catalog,
    main,
    validate_catalog,
)


class AnalyzerCanCatalogTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = load_catalog(DEFAULT_CATALOG)

    def test_catalog_has_exact_expected_shape_and_critical_ids(self):
        messages = self.catalog["messages"]
        ids = [message["id"] for message in messages]
        by_id = {message["id"]: message for message in messages}

        self.assertEqual(69, len(messages))
        self.assertEqual(69, len(set(ids)))
        self.assertEqual(ids, sorted(ids))
        self.assertEqual(1, ids.count(0x3E2))
        self.assertTrue({0x107, 0x212, 0x389}.issubset(ids))
        self.assertEqual("supported", by_id[0x107]["status"])
        self.assertEqual("unsupported_conflict", by_id[0x25D]["status"])
        self.assertEqual("unsupported_conflict", by_id[0x3FE]["status"])
        self.assertEqual("supported", by_id[0x212]["status"])
        self.assertEqual("supported", by_id[0x389]["status"])
        fw9_supported = {
            0x082, 0x0A9, 0x102, 0x103, 0x118, 0x145, 0x20C, 0x21C,
            0x238, 0x243, 0x249, 0x257, 0x25A, 0x266, 0x2E1, 0x2E5,
            0x2F3, 0x31F, 0x321, 0x339, 0x33A, 0x39D, 0x3B6, 0x3D2,
            0x3D8, 0x3E3, 0x3F5, 0x405, 0x4F3, 0x679,
        }
        expected_supported = fw9_supported | {0x107, 0x212, 0x389, 0x3E2}
        self.assertEqual(expected_supported, SUPPORTED_IDS)
        self.assertEqual(
            expected_supported,
            {message["id"] for message in messages if message["status"] == "supported"},
        )

    def test_fw9_rules_and_minimum_dlcs_are_exact(self):
        by_id = {message["id"]: message for message in self.catalog["messages"]}
        expected = {
            0x082: (1, {"UI_tripPlanning": (2, 1, 1, 0)}),
            0x0A9: (1, {"BODY_statusRaw": (0, 8, 1, 0)}),
            0x102: (1, {"VCLEFT_doorFL": (0, 1, 1, 0), "VCLEFT_doorRL": (1, 1, 1, 0)}),
            0x103: (1, {"VCRIGHT_doorFR": (0, 1, 1, 0), "VCRIGHT_doorRR": (1, 1, 1, 0)}),
            0x118: (3, {"DI_gear": (21, 3, 1, 0)}),
            0x145: (4, {"ESP_status": (30, 1, 1, 0)}),
            0x20C: (6, {"VCRIGHT_hvacBlowerRaw": (0, 11, 1, 0), "VCRIGHT_hvacF2Raw": (32, 10, 1, 0)}),
            0x21C: (7, {"Control_D6Raw": (48, 8, 1, 0)}),
            0x238: (2, {"STW_mapDataRaw": (8, 5, 1, 0)}),
            0x243: (1, {"VCRIGHT_hvacStatusRaw": (0, 3, 1, 0)}),
            0x249: (4, {"SCCM_leftStalkRaw": (24, 8, 1, 0)}),
            0x257: (5, {"DI_uiSpeed": (24, 9, 1, 0)}),
            0x25A: (4, {
                "VCSEC_TPMSDisplayPressureFLRaw": (0, 8, 1, 0),
                "VCSEC_TPMSDisplayPressureFRRaw": (8, 8, 1, 0),
                "VCSEC_TPMSDisplayPressureRLRaw": (16, 8, 1, 0),
                "VCSEC_TPMSDisplayPressureRRRaw": (24, 8, 1, 0),
            }),
            0x266: (2, {"DIR_elecPowerRaw": (0, 11, 1, 0)}),
            0x2E1: (1, {"VCFRONT_statusRaw": (0, 8, 1, 0)}),
            0x2E5: (2, {"DIF_elecPowerRaw": (0, 11, 1, 0)}),
            0x2F3: (4, {"UI_hvacRequestRaw": (29, 3, 1, 0)}),
            0x31F: (1, {"PARK_statusRaw": (0, 8, 1, 0)}),
            0x321: (6, {"VCFRONT_ambientRaw": (40, 8, 1, 0)}),
            0x339: (2, {"VCSEC_authenticationRaw": (12, 4, 1, 0)}),
            0x33A: (4, {"UI_soc": (20, 7, 1, 0)}),
            0x39D: (3, {"IBST_statusRaw": (17, 1, 1, 0)}),
            0x3B6: (4, {"DI_odometerRaw": (0, 32, 1, 0)}),
            0x3D2: (8, {"BMS_kwhDischargeTotalRaw": (0, 32, 1, 0), "BMS_kwhChargeTotalRaw": (32, 32, 1, 0)}),
            0x3D8: (2, {"UI_elevation": (0, 14, 1, 0)}),
            0x3E2: (1, {"VCLEFT_brakeLightStatus": (0, 2, 1, 0)}),
            0x3E3: (2, {"VCRIGHT_lightStatusRaw": (8, 1, 1, 0)}),
            0x3F5: (5, {"VCFRONT_lightingRaw": (32, 8, 1, 0)}),
            0x405: (1, {"GTW_vinD0Raw": (0, 8, 1, 0)}),
            0x4F3: (3, {"CAN_4F3_D2Raw": (16, 8, 1, 0)}),
            0x679: (6, {"UI_ambientLightingD5Raw": (40, 8, 1, 0)}),
        }
        for can_id, (dlc, rules) in expected.items():
            with self.subTest(can_id=f"0x{can_id:03X}"):
                message = by_id[can_id]
                self.assertEqual(dlc, message["dlc"])
                self.assertEqual(
                    rules,
                    {signal["name"]: self._rule(signal) for signal in message["signals"]},
                )

    def test_unrepresentable_fw9_rules_remain_listed(self):
        by_id = {message["id"]: message for message in self.catalog["messages"]}

        soc = by_id[0x33A]
        self.assertEqual(["UI_soc"], [signal["name"] for signal in soc["signals"]])
        self.assertNotIn("range", " ".join(signal["name"].lower() for signal in soc["signals"]))
        self.assertIn("D5 != 0xFF", soc["note"])
        self.assertIn("未加入 range", soc["note"])

        for can_id in (0x2B6, 0x37A, 0x3C2, 0x3EA):
            with self.subTest(can_id=f"0x{can_id:03X}"):
                self.assertEqual("listed", by_id[can_id]["status"])
                self.assertEqual([], by_id[can_id]["signals"])
        self.assertIn("mux", by_id[0x3C2]["name"])

    def test_can_helpers_rules_for_added_frames_are_exact(self):
        by_id = {message["id"]: message for message in self.catalog["messages"]}
        ibst = {signal["name"]: signal for signal in by_id[0x107]["signals"]}
        bms = {signal["name"]: signal for signal in by_id[0x212]["signals"]}
        das = {signal["name"]: signal for signal in by_id[0x389]["signals"]}

        self.assertEqual(5, by_id[0x107]["dlc"])
        self.assertEqual(8, by_id[0x212]["dlc"])
        self.assertEqual(7, by_id[0x389]["dlc"])
        self.assertIn("can_helpers.h", by_id[0x107]["note"])
        self.assertIn("车型/版本敏感", by_id[0x212]["note"])
        self.assertIn("不与 current", by_id[0x389]["note"])

        self.assertEqual(
            {
                "IBST_iBoosterStatus": (12, 3, 1, 0),
                "IBST_driverBrakeApply": (16, 2, 1, 0),
                "IBST_internalState": (18, 3, 1, 0),
                "IBST_sInputRodDriver": (21, 12, 0.015625, -5),
            },
            {name: self._rule(signal) for name, signal in ibst.items()},
        )
        self.assertEqual(
            {
                "BMS_hvacPowerRequest": (0, 1, 1, 0),
                "BMS_notEnoughPowerForDrive": (1, 1, 1, 0),
                "BMS_notEnoughPowerForSupport": (2, 1, 1, 0),
                "BMS_preconditionAllowed": (3, 1, 1, 0),
                "BMS_updateAllowed": (4, 1, 1, 0),
                "BMS_activeHeatingWorthwhile": (5, 1, 1, 0),
                "BMS_cpMiaOnHvs": (6, 1, 1, 0),
                "BMS_pcsPwmEnabled": (7, 1, 1, 0),
                "BMS_contactorState": (8, 3, 1, 0),
                "BMS_uiChargeStatus": (11, 3, 1, 0),
                "BMS_ecuLogUploadRequest": (14, 2, 1, 0),
                "BMS_hvState": (16, 3, 1, 0),
                "BMS_isolationResistance": (19, 10, 10, 0),
                "BMS_chargeRequest": (29, 1, 1, 0),
                "BMS_keepWarmRequest": (30, 1, 1, 0),
                "BMS_bmsState": (32, 4, 1, 0),
                "BMS_diLimpRequest": (36, 1, 1, 0),
                "BMS_okToShipByAir": (37, 1, 1, 0),
                "BMS_okToShipByLand": (38, 1, 1, 0),
                "BMS_chgPowerAvailable": (40, 11, 0.125, 0),
                "BMS_chargeRetryCount": (51, 3, 1, 0),
                "BMS_smStateRequest": (56, 4, 1, 0),
            },
            {name: self._rule(signal) for name, signal in bms.items()},
        )
        self.assertEqual(
            {
                "DAS_accSpeedLimit": (0, 10, 0.4, 0),
                "DAS_pmmObstacleSeverity": (10, 3, 1, 0),
                "DAS_pmmLoggingRequest": (13, 1, 1, 0),
                "DAS_activationFailureStatus": (14, 2, 1, 0),
                "DAS_pmmUltrasonicsFaultReason": (16, 3, 1, 0),
                "DAS_pmmRadarFaultReason": (19, 2, 1, 0),
                "DAS_pmmSysFaultReason": (21, 3, 1, 0),
                "DAS_pmmCameraFaultReason": (24, 2, 1, 0),
                "DAS_accReport": (26, 5, 1, 0),
                "DAS_lssState": (31, 3, 1, 0),
                "DAS_radarTelemetry": (34, 2, 1, 0),
                "DAS_robState": (36, 2, 1, 0),
                "DAS_driverInteractionLevel": (38, 2, 1, 0),
                "DAS_ppOffsetDesiredRamp": (40, 8, 0.01, -1.28),
                "DAS_longCollisionWarning": (48, 4, 1, 0),
            },
            {name: self._rule(signal) for name, signal in das.items()},
        )
        self.assertEqual("SNA", das["DAS_accSpeedLimit"]["enum"]["1023"])
        self.assertFalse(any(signal["signed"] for signal in bms.values()))
        self.assertFalse(any(signal["signed"] for signal in das.values()))

    def test_validation_rejects_duplicate_id(self):
        catalog = copy.deepcopy(self.catalog)
        catalog["messages"][-1]["id"] = catalog["messages"][-2]["id"]
        catalog["messages"][-1]["hex"] = catalog["messages"][-2]["hex"]
        with self.assertRaisesRegex(CatalogValidationError, "重复"):
            validate_catalog(catalog)

    def test_validation_rejects_out_of_range_id(self):
        catalog = copy.deepcopy(self.catalog)
        catalog["messages"][-1]["id"] = 0x800
        catalog["messages"][-1]["hex"] = "0x800"
        with self.assertRaisesRegex(CatalogValidationError, "11-bit"):
            validate_catalog(catalog)

    def test_validation_rejects_wrong_exact_id_set(self):
        catalog = copy.deepcopy(self.catalog)
        message = catalog["messages"][0]
        message["id"] = 0x081
        message["hex"] = "0x081"
        with self.assertRaisesRegex(CatalogValidationError, "集合不匹配"):
            validate_catalog(catalog)

    def test_validation_rejects_signal_past_dlc(self):
        catalog = copy.deepcopy(self.catalog)
        signal = catalog["messages"][12]["signals"][0]
        signal["start"] = 63
        signal["length"] = 2
        with self.assertRaisesRegex(CatalogValidationError, "超出 DLC"):
            validate_catalog(catalog)

    def test_validation_rejects_bad_signal_structure(self):
        catalog = copy.deepcopy(self.catalog)
        signal = catalog["messages"][12]["signals"][0]
        signal["signed"] = 0
        with self.assertRaisesRegex(CatalogValidationError, "signed"):
            validate_catalog(catalog)

        catalog = copy.deepcopy(self.catalog)
        catalog["messages"][12]["signals"][0]["enum"] = {"bad": "BAD"}
        with self.assertRaisesRegex(CatalogValidationError, "enum"):
            validate_catalog(catalog)

    def test_validation_accepts_canonical_negative_enum_for_signed_signal(self):
        catalog = copy.deepcopy(self.catalog)
        signal = next(
            signal
            for message in catalog["messages"]
            for signal in message["signals"]
            if signal["signed"]
        )
        signal["enum"] = {"-1": "SNA"}
        self.assertIs(catalog, validate_catalog(catalog))

        signal["enum"] = {"-01": "NON_CANONICAL"}
        with self.assertRaisesRegex(CatalogValidationError, "规范十进制"):
            validate_catalog(catalog)

    def test_generated_files_match_builders(self):
        self.assertEqual(build_header(self.catalog), DEFAULT_HEADER.read_text(encoding="utf-8"))
        self.assertEqual(build_browser_script(self.catalog), DEFAULT_BROWSER.read_text(encoding="utf-8"))

    def test_check_detects_drift(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            header = tmp_path / "analyzer_critical_ids.h"
            browser = tmp_path / "can_catalog.js"
            header.write_text("drift\n", encoding="utf-8")
            browser.write_text(build_browser_script(self.catalog), encoding="utf-8")

            result = main(
                [
                    "--catalog",
                    str(DEFAULT_CATALOG),
                    "--header",
                    str(header),
                    "--browser",
                    str(browser),
                    "--check",
                ]
            )

        self.assertEqual(1, result)

    def test_default_check_command_succeeds(self):
        result = subprocess.run(
            [sys.executable, "scripts/generate_analyzer_can_catalog.py", "--check"],
            cwd=DEFAULT_CATALOG.parents[1],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(0, result.returncode, result.stderr)

    @staticmethod
    def _rule(signal):
        return signal["start"], signal["length"], signal["factor"], signal["offset"]


if __name__ == "__main__":
    unittest.main()
