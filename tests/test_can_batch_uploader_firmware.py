import unittest
from pathlib import Path


class CanBatchUploaderFirmwareTest(unittest.TestCase):
    def test_firmware_has_can_batch_uploader_wired_into_rx_loop(self):
        uploader = Path("include/can_batch_uploader.h").read_text(encoding="utf-8")
        main_cpp = Path("src/main.cpp").read_text(encoding="utf-8")
        web_server = Path("include/web/web_server.h").read_text(encoding="utf-8")

        self.assertIn("CAN_UPLOAD_BATCH_SIZE = 200", uploader)
        self.assertIn("http://1.116.182.175:48601/can/batch", uploader)
        self.assertIn("canBatchUploader.noteRx", main_cpp)
        self.assertIn("canBatchUploader.loop", main_cpp)
        self.assertIn('upload\\', web_server)

    def test_uploader_loop_does_not_run_synchronous_http_post(self):
        uploader = Path("include/can_batch_uploader.h").read_text(encoding="utf-8")
        loop_body = uploader.split("    void loop()", 1)[1].split("    uint16_t pending()", 1)[0]

        self.assertNotIn("sendBatch();", loop_body)


if __name__ == "__main__":
    unittest.main()
