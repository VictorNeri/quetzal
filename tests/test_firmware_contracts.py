import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class FirmwareContractTests(unittest.TestCase):
    def test_espnow_replaces_ir_remote_menu(self):
        sketch = (ROOT / "quetzal.ino").read_text(encoding="utf-8")
        self.assertNotIn('"IR Remote"', sketch)
        self.assertIn('"ESP-NOW"', sketch)
        self.assertIn('#include "espnow_test.h"', sketch)
        self.assertTrue((ROOT / "espnow_test.cpp").is_file())
        self.assertTrue((ROOT / "espnow_test.h").is_file())
        self.assertIn("EspNowTest::espNowTestSetup", sketch)
        self.assertIn("EspNowTest::espNowTestCleanup", sketch)

    def test_host_scanner_uses_real_subnet_and_bounded_icmp_probe(self):
        source = (ROOT / "host_scanner.cpp").read_text(encoding="utf-8")
        self.assertIn("WiFi.subnetMask()", source)
        self.assertIn("SOCK_RAW", source)
        self.assertIn("IPPROTO_ICMP", source)
        self.assertIn("close(pingSocket)", source)
        self.assertNotIn("esp_ping_new_session", source)
        self.assertNotIn("client.connect(target, 80, CONNECT_TIMEOUT_MS)", source)
        self.assertIn("networkAddress", source)
        self.assertIn("broadcastAddress", source)

    def test_host_scanner_results_support_paging_and_detail(self):
        source = (ROOT / "host_scanner.cpp").read_text(encoding="utf-8")
        self.assertIn("RESULTS_PER_PAGE", source)
        self.assertIn("listStartIndex", source)
        self.assertIn("scrollResults", source)
        self.assertIn("drawResultDetail", source)
        self.assertIn("resultsTruncated", source)
        self.assertIn("if (!aborted) resultCount++", source)

    def test_espnow_reports_full_v2_frame_length(self):
        source = (ROOT / "espnow_test.cpp").read_text(encoding="utf-8")
        self.assertIn("volatile uint16_t lastLength", source)
        self.assertNotIn("min(length, 255)", source)


if __name__ == "__main__":
    unittest.main()
