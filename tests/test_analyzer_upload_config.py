import unittest
from pathlib import Path


class AnalyzerUploadConfigSourceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = Path("src/analyzer/analyzer_upload_config.cpp").read_text(encoding="utf-8")
        cls.header = Path("src/analyzer/analyzer_upload_config.h").read_text(encoding="utf-8")
        cls.analyzer = Path("src/can_analyzer.cpp").read_text(encoding="utf-8")

    def test_persists_config_but_boots_inactive(self):
        self.assertIn('"analyzer_upload"', self.source)
        self.assertIn('"buses"', self.source)
        self.assertIn("result.mode = CanUploadMode::All", self.source)
        self.assertIn("g_uploader.begin(deviceId, false)", self.analyzer)

    def test_url_contract_is_binary_endpoint(self):
        self.assertIn('constexpr const char *kHttpPrefix = "http://"', self.source)
        self.assertIn('constexpr const char *kBatchPath = "/can/batch-bin"', self.source)
        self.assertIn("validPort", self.source)
        self.assertIn("validateAuthority", self.source)

    def test_accepts_filter_and_three_bus_choices(self):
        for mode in ("all", "critical"):
            self.assertIn(f'equalsIgnoreCase(text, "{mode}")', self.source)
        for buses in ("a", "b", "both"):
            self.assertIn(f'equalsIgnoreCase(text, "{buses}")', self.source)


if __name__ == "__main__":
    unittest.main()
