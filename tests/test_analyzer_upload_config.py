import unittest
from pathlib import Path


class AnalyzerUploadConfigSourceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = Path("src/analyzer/analyzer_upload_config.cpp").read_text(encoding="utf-8")
        cls.header = Path("src/analyzer/analyzer_upload_config.h").read_text(encoding="utf-8")

    def test_defaults_to_off_and_uses_separate_nvs_namespace(self):
        self.assertIn('"analyzer_upload"', self.source)
        self.assertIn("CanUploadMode::Off", self.header)
        self.assertIn("result.mode = CanUploadMode::Off", self.source)

    def test_url_contract_is_http_can_batch_only(self):
        self.assertIn('constexpr const char *kHttpPrefix = "http://"', self.source)
        self.assertIn('constexpr const char *kBatchPath = "/can/batch"', self.source)
        self.assertIn("validPort", self.source)
        self.assertIn("validateAuthority", self.source)
        self.assertIn("pathLength == 10", self.source)

    def test_accepts_only_three_modes(self):
        for mode in ("off", "all", "critical"):
            self.assertIn(f'equalsIgnoreCase(text, "{mode}")', self.source)


if __name__ == "__main__":
    unittest.main()
