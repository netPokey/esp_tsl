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
        self.assertIn("xTaskNotifyGive", loop_body)

    def test_uploader_creates_one_background_upload_task(self):
        uploader = Path("include/can_batch_uploader.h").read_text(encoding="utf-8")
        begin_body = uploader.split("    void begin", 1)[1].split("    void noteRx", 1)[0]
        task_body = uploader.split("    static void uploadTask", 1)[1].split("    String buildPayload", 1)[0]

        self.assertIn("uploadTaskHandle_", begin_body)
        self.assertIn("xTaskCreatePinnedToCore", begin_body)
        self.assertIn("pdPASS", begin_body)
        self.assertIn("lastHttpCode_ = -2", begin_body)
        self.assertIn("ulTaskNotifyTake", task_body)
        self.assertNotIn("vTaskDelete", task_body)

    def test_uploader_releases_http_client_on_begin_failure(self):
        uploader = Path("include/can_batch_uploader.h").read_text(encoding="utf-8")
        begin_failure_body = uploader.split("if (!http.begin(CAN_UPLOAD_DEFAULT_URL))", 1)[1].split("http.addHeader", 1)[0]

        self.assertIn("http.end();", begin_failure_body)


if __name__ == "__main__":
    unittest.main()
